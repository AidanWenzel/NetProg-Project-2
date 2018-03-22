cmake_minimum_required(VERSION 3.8)
project(RPC)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-ldns_sd")

set(SOURCE_FILES server.c)
add_executable(TFTP ${SOURCE_FILES})