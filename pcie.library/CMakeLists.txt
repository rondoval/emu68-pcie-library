cmake_minimum_required(VERSION 3.14.0)
project(pcie.library VERSION ${CMAKE_PROJECT_VERSION})

add_compile_options(-ffreestanding -nostdlib -nostartfiles -Os -m68040 -fomit-frame-pointer -Wall -Wno-unused-function -Wno-shift-count-overflow -DDEBUG)
file(GLOB_RECURSE pcie_sources src/*.c include/*.h)
add_library(pcie.library STATIC ${pcie_sources})

target_include_directories(pcie.library PUBLIC include)
target_link_libraries(pcie.library common)
