cmake_minimum_required(VERSION 3.12)

project(zbaselib)

# set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

set(CMAKE_CXX_FLAGS "-g -O0 ${CMAKE_CXX_FLAGS}")

include_directories(./Include)

add_executable(testProcessLock test/testProcessLock.cpp)


add_executable(testLockFreeRingQueue test/testLockFreeRingQueue.cpp)
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
  target_link_libraries(testLockFreeRingQueue pthread)
endif()


add_executable(testChannel test/testChannel.cpp)
if (CMAKE_SYSTEM_NAME MATCHES "Linux")
  target_link_libraries(testChannel pthread)
endif()
