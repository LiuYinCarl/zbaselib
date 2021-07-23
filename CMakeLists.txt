cmake_minimum_required(VERSION 3.12)

project(zbaselib)

set(CMAKE_CXX_STANDARD 11)

include_directories(./Include)

add_executable(zbaselib test/testProcessLock.cpp)

