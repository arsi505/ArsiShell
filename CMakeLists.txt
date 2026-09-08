cmake_minimum_required(VERSION 3.15)
project(ArsiShell CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src)

add_executable(ArsiShell
    src/main.cpp
    src/shell.cpp
    src/shell.h
)
