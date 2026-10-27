// CounterFlame 1.3.0.1 - in-process il2cpp dumper (arm64).
// Waits for libil2cpp.so + Assembly-CSharp to come up, then writes dump.cs with every
// class / field offset / method RVA straight from the live runtime. Works even when
// global-metadata.dat is renamed, split or encrypted on disk, because the VM has
// already decoded it by the time we read it.
// Output: /sdcard/Android/data/<pkg>/files/dump.cs  (falls back to /data/data/<pkg>/files/)
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string>

#define IL2CPP_LIB "libil2cpp.so"

struct Api {
    void*       (*domain_get)();
    void**      (*domain_get_assemblies)(void*, size_t*);
    void*       (*assembly_get_image)(void*);
    const char* (*image_get_name)(void*);
    size_t      (*image_get_class_count)(void*);
    void*       (*image_get_class)(void*, size_t);
    const char* (*class_get_name)(void*);
    const char* (*class_get_namespace)(void*);
    void*       (*class_get_parent)(void*);
    void*       (*class_get_declaring_type)(void*);
    void*       (*class_get_fields)(void*, void**);
    void*       (*class_get_methods)(void*, void**);
    const char* (*field_get_name)(void*);
    size_t      (*field_get_offset)(void*);
    int         (*field_get_flags)(void*);
    void*       (*field_get_type)(void*);
    char*       (*type_get_name)(void*);
    const char* (*method_get_name)(void*);
    uint32_t    (*method_get_param_count)(void*);
    uint32_t    (*method_get_flags)(void*, uint32_t*);
    void*       (*thread_attach)(void*);
    void        (*il2cpp_free)(void*);
} A;

static bool resolve(void* h) {
#define R(n, sym) A.n = (decltype(A.n))dlsym(h, sym); if (!A.n) return false;
    R(domain_get, "il2cpp_domain_get")
    R(domain_get_assemblies, "il2cpp_domain_get_assemblies")
    R(assembly_get_image, "il2cpp_assembly_get_image")
    R(image_get_name, "il2cpp_image_get_name")
    R(image_get_class_count, "il2cpp_image_get_class_count")
    R(image_get_class, "il2cpp_image_get_class")
    R(class_get_name, "il2cpp_class_get_name")
    R(class_get_namespace, "il2cpp_class_get_namespace")
    R(class_get_parent, "il2cpp_class_get_parent")
    R(class_get_declaring_type, "il2cpp_class_get_declaring_type")
    R(class_get_fields, "il2cpp_class_get_fields")
    R(class_get_methods, "il2cpp_class_get_methods")
    R(field_get_name, "il2cpp_field_get_name")
    R(field_get_offset, "il2cpp_field_get_offset")
    R(field_get_flags, "il2cpp_field_get_flags")
    R(field_get_type, "il2cpp_field_get_type")
    R(type_get_name, "il2cpp_type_get_name")
    R(method_get_name, "il2cpp_method_get_name")
    R(method_get_param_count, "il2cpp_method_get_param_count")
    R(method_get_flags, "il2cpp_method_get_flags")
    R(thread_attach, "il2cpp_thread_attach")
#undef R
    A.il2cpp_free = (decltype(A.il2cpp_free))dlsym(h, "il2cpp_free"); // optional
    return true;
}

static uintptr_t lib_base() {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512]; uintptr_t lo = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strstr(line, IL2CPP_LIB)) continue;
        uintptr_t s = strtoull(line, nullptr, 16);
        if (!lo || s < lo) lo = s;
    }
    fclose(f);
    return lo;
}

static std::string pkg_name() {
    char buf[256] = {0};
    FILE* f = fopen("/proc/self/cmdline", "r");
    if (f) { fread(buf, 1, sizeof buf - 1, f); fclose(f); }
    std::string s(buf);
    size_t c = s.find(':'); if (c != std::string::npos) s.resize(c);
    return s;
}

static FILE* open_out() {
    std::string p = pkg_name();
    const char* roots[] = { "/sdcard/Android/data/", "/storage/emulated/0/Android/data/", "/data/data/", "/data/user/0/" };
    for (auto r : roots) {
        std::string dir = std::string(r) + p + "/files";
        mkdir(dir.c_str(), 0777);
        FILE* f = fopen((dir + "/dump.cs").c_str(), "w");
        if (f) return f;
    }
    return nullptr;
}

static bool skip_image(const char* n) {
    static const char* skip[] = { "mscorlib", "System", "Mono.", "netstandard", "Newtonsoft", "Firebase", "Google", "Unity.Burst", nullptr };
    for (int i = 0; skip[i]; i++) if (!strncmp(n, skip[i], strlen(skip[i]))) return true;
    return false;
}

static std::string full_name(void* k) {
    std::string n = A.class_get_name(k);
    void* d = A.class_get_declaring_type(k);
    if (d) return full_name(d) + "." + n;
    const char* ns = A.class_get_namespace(k);
    return (ns && *ns) ? std::string(ns) + "." + n : n;
}

static void dump(FILE* out, uintptr_t base) {
    size_t asmCount = 0;
    void** asms = A.domain_get_assemblies(A.domain_get(), &asmCount);
    for (size_t a = 0; a < asmCount; a++) {
        void* img = A.assembly_get_image(asms[a]);
        const char* iname = A.image_get_name(img);
        if (!iname || skip_image(iname)) continue;
        fprintf(out, "// ===== %s =====\n", iname);
        size_t cc = A.image_get_class_count(img);
        for (size_t c = 0; c < cc; c++) {
            void* k = A.image_get_class(img, c);
            if (!k) continue;
            void* par = A.class_get_parent(k);
            fprintf(out, "\nclass %s", full_name(k).c_str());
            if (par) fprintf(out, " : %s", full_name(par).c_str());
            fprintf(out, "\n{\n");
            void* it = nullptr; void* f;
            while ((f = A.class_get_fields(k, &it))) {
                char* tn = A.type_get_name(A.field_get_type(f));
                fprintf(out, "    %s0x%zX  %s  %s;\n", (A.field_get_flags(f) & 0x10) ? "static " : "",
                        A.field_get_offset(f), tn ? tn : "?", A.field_get_name(f));
                if (tn && A.il2cpp_free) A.il2cpp_free(tn);
            }
            it = nullptr; void* m;
            while ((m = A.class_get_methods(k, &it))) {
                uint32_t iflags = 0;
                uint32_t fl = A.method_get_flags(m, &iflags);
                uintptr_t p = *(uintptr_t*)m;   // MethodInfo::methodPointer is the first member
                if (p > base && p - base < 0x10000000) fprintf(out, "    RVA 0x%zX  ", (size_t)(p - base));
                else fprintf(out, "    RVA ----     ");
                fprintf(out, "%s%s(%u args)\n", (fl & 0x10) ? "static " : "", A.method_get_name(m), A.method_get_param_count(m));
            }
            fprintf(out, "}\n");
        }
    }
    fprintf(out, "\n// DONE\n");
}

static bool ready() {
    void* h = dlopen(IL2CPP_LIB, RTLD_NOLOAD | RTLD_NOW);
    if (!h || !resolve(h)) return false;
    void* dom = A.domain_get();
    if (!dom) return false;
    A.thread_attach(dom);
    size_t n = 0;
    void** asms = A.domain_get_assemblies(dom, &n);
    for (size_t i = 0; asms && i < n; i++) {
        const char* nm = A.image_get_name(A.assembly_get_image(asms[i]));
        if (nm && !strncmp(nm, "Assembly-CSharp", 15) && A.image_get_class_count(A.assembly_get_image(asms[i])) > 0) return true;
    }
    return false;
}

static void* worker(void*) {
    for (int i = 0; i < 600; i++) {            // wait up to 5 min for the VM
        if (ready()) break;
        usleep(500000);
        if (i == 599) return nullptr;
    }
    sleep(20);                                  // let the game finish booting
    uintptr_t base = lib_base();
    FILE* out = open_out();
    if (!out) return nullptr;
    dump(out, base);
    fclose(out);
    return nullptr;
}

__attribute__((constructor)) static void init() {
    pthread_t t;
    pthread_create(&t, nullptr, worker, nullptr);
    pthread_detach(t);
}
