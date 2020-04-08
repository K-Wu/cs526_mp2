cmake_minimum_required(VERSION 3.1)
project(SLPVectorizer-kdw)

set(CMAKE_C_FLAGS_DEBUG "-g -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -DDEBUG")

set( ENV{CC} /opt/rh/devtoolset-7/root/usr/bin/gcc )
set( ENV{CXX} /opt/rh/devtoolset-7/root/usr/bin/g++ )

find_package(LLVM REQUIRED CONFIG)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

add_subdirectory(pass)  # Use your pass name here.