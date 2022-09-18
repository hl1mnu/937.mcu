cmake_minimum_required(VERSION 3.10)

# https://techoverflow.net/2019/06/08/a-working-sdcc-stm8-cmake-configuration/

set(CMAKE_C_OUTPUT_EXTENSION ".rel")
set(CMAKE_C_COMPILER "sdcc")
set(CMAKE_SYSTEM_NAME Generic)

# Prevent default configuration
set(CMAKE_C_FLAGS_INIT "")
set(CMAKE_EXE_LINKER_FLAGS_INIT "")

set(CMAKE_C_FLAGS "-mmcs51 --model-small")
set(CMAKE_EXE_LINKER_FLAGS "--code-loc 0x0000")

# set the project name
project(TCSD C)

# add the executible
add_executable(TCSD TCSD.c)
