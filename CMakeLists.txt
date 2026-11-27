cmake_minimum_required(VERSION 3.10)
project(ll.externals)

if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Set the default build type to MinSizeRel" FORCE)
endif()
message(STATUS "Current build type is: ${CMAKE_BUILD_TYPE}")

# Set a default value for MAX_SDK_BASE_PATH and allow overrides
set(MAX_SDK_BASE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/max-sdk-base" CACHE PATH "Path to max-sdk-base")

# Set the path for writing Max objects
set(C74_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/externals" CACHE PATH "Build path")

# Include the Max SDK headers
include_directories(${MAX_SDK_BASE_PATH}/include)

# Add subdirectories for each of the Max MSP objects
add_subdirectory(source/ll.2dslider)
add_subdirectory(source/ll.fastforward)
add_subdirectory(source/ll.filewatchers)
add_subdirectory(source/ll.mcwaveform)
add_subdirectory(source/ll.number)
add_subdirectory(source/ll.preset)
add_subdirectory(source/ll.slishi)
add_subdirectory(source/ll.menu)
add_subdirectory(source/ll.zip)

if(APPLE)
   add_subdirectory(source/ll.trackpad)
endif()
