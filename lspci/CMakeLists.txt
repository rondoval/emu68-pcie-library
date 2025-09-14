cmake_minimum_required(VERSION 3.14.0)
project(lspci VERSION 1.0.0)

add_compile_options(-Os -Wall -Wno-unused-function -Wno-shift-count-overflow -DDEBUG)
add_link_options(-s)

add_executable(lspci
    src/pcie_list.c
)

target_link_libraries(lspci common pcie.library)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/lspci DESTINATION .)
