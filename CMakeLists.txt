cmake_minimum_required(VERSION 3.17)
project(unix_command_line_interface)

set(CMAKE_CXX_STANDARD 11)

add_executable(unix_command_line_interface main.cpp colors.cpp prompt.cpp parser.cpp builtins.cpp process.cpp)
target_link_libraries(unix_command_line_interface -lreadline)