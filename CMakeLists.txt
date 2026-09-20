cmake_minimum_required(VERSION 3.20)
project(ShellX LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(shellx
    src/main.cpp
    src/parser.cpp
    src/executor.cpp
    src/builtins.cpp
    src/redirection.cpp
    src/pipeline.cpp
    src/signals.cpp
    src/jobs.cpp
)
target_include_directories(shellx PRIVATE include)
target_compile_options(shellx PRIVATE -Wall -Wextra -Wpedantic)
