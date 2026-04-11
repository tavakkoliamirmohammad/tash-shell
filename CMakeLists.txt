cmake_minimum_required(VERSION 3.17)
project(amish VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Find readline portably
find_library(READLINE_LIB readline REQUIRED)
find_path(READLINE_INCLUDE readline/readline.h)

add_executable(amish main.cpp colors.cpp)

target_compile_options(amish PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(amish PRIVATE ${READLINE_INCLUDE})
target_link_libraries(amish PRIVATE ${READLINE_LIB})

set_target_properties(amish PROPERTIES OUTPUT_NAME "shell.out")
