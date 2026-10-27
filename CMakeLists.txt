cmake_minimum_required(VERSION 3.18)
project(cfdump CXX)
add_library(cfdump SHARED dump.cpp)
target_link_libraries(cfdump dl)
