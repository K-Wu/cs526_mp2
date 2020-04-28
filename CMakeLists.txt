cmake_minimum_required(VERSION 3.1)
project(SLPVectorizer-kdw)

set(CMAKE_C_FLAGS_DEBUG "-g -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -DDEBUG")

set( ENV{CC} /usr/bin/cc )
set( ENV{CXX} /usr/bin/c++ )

find_package(LLVM REQUIRED CONFIG)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

add_subdirectory(pass)  # Use your pass name here.
