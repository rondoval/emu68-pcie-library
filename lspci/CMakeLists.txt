cmake_minimum_required(VERSION 3.14.0)
project(lspci VERSION ${CMAKE_PROJECT_VERSION})

# Version stamp ($VER:) embedded in the binary.  CURRENT_DATE is not inherited
# from the pcie-library parent scope, so derive it here as the sibling libraries do.
string(TIMESTAMP CURRENT_DATE "(%d.%m.%Y)")

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
)
add_link_options(
    -s
)

# Debug-output backend defines (EMU68_DEBUG_BACKEND, see emu68-common).
emu68_debug_definitions()

# Executable
add_executable(lspci
    src/pcie_list.c
)

target_compile_definitions(lspci PRIVATE
    VERSTAG="$VER: lspci ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR} ${CURRENT_DATE}"
)

target_link_libraries(lspci
    PRIVATE
        Emu68Common::common
)

# Hosted CLI program: for the serial backend, link debug.lib (KPrintF). libc
# supplies __divsi3, so no glue and no ROM check.
emu68_debug_backend_finalize(lspci)

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
