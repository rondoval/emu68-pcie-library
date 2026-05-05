cmake_minimum_required(VERSION 3.14.0)
project(lspci VERSION ${CMAKE_PROJECT_VERSION})

# Compile and link options
add_compile_options(
    -Os
    -Wall
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wmissing-prototypes
    -Wstrict-prototypes
    -Wno-unused-function
    -Wno-shift-count-overflow
    -DDEBUG
)
add_link_options(
    -s
)

# Executable
add_executable(lspci
    src/pcie_list.c
)

target_link_libraries(lspci
    Emu68Common::common
)

# pcie_library_sfd_headers generates proto/bcmpcie.h (and clib/, inline/, pragmas/) into
# the build tree. Reference it directly so lspci can build before the install step
# copies those headers into the install prefix.
add_dependencies(lspci pcie_library_sfd_headers)

target_include_directories(lspci
    PRIVATE
        # SFD-generated pcie headers (proto/bcmpcie.h etc.)
        ${CMAKE_BINARY_DIR}/bcmpcie.library/generated/include
        # bcmpcie.library public headers (libraries/pcie.h, libraries/pci_constants.h, libraries/openpci.h, libraries/pcitags.h)
        ${CMAKE_SOURCE_DIR}/bcmpcie.library/include
)

install(
    PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/lspci
    DESTINATION C
)
