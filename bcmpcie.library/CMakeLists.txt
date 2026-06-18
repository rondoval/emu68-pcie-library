cmake_minimum_required(VERSION 3.14.0)
project(bcmpcie.library VERSION ${CMAKE_PROJECT_VERSION})

# Version string
string(TIMESTAMP CURRENT_DATE "(%d.%m.%Y)")
set(VERSTRING "$VER: ${PROJECT_NAME} ${PROJECT_VERSION} ${CURRENT_DATE}")
string(REGEX REPLACE "\\.library$" "" LIBRARY_BASENAME "${PROJECT_NAME}")

# Find SFDC tool
find_program(SFDC_EXECUTABLE
    NAMES sfdc
    HINTS /opt/m68k-amigaos/bin
    REQUIRED
)

# SFD header generation
set(PCIE_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/include)
set(PCIE_SFD_FILE ${CMAKE_SOURCE_DIR}/sfd/bcmpcie.sfd)
set(PCIE_GENERATED_HEADERS
    ${PCIE_GENERATED_INCLUDE_DIR}/clib/${LIBRARY_BASENAME}_protos.h
    ${PCIE_GENERATED_INCLUDE_DIR}/inline/${LIBRARY_BASENAME}.h
    ${PCIE_GENERATED_INCLUDE_DIR}/pragmas/${LIBRARY_BASENAME}_pragmas.h
    ${PCIE_GENERATED_INCLUDE_DIR}/proto/${LIBRARY_BASENAME}.h
)

add_custom_command(
    OUTPUT ${PCIE_GENERATED_HEADERS}
    COMMAND ${CMAKE_COMMAND}
        -DSFDC_EXECUTABLE=${SFDC_EXECUTABLE}
        -DSFD_FILE=${PCIE_SFD_FILE}
        -DOUTPUT_DIR=${PCIE_GENERATED_INCLUDE_DIR}
        -DOUTPUT_BASENAME=${LIBRARY_BASENAME}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateSfdHeaders.cmake
    DEPENDS
        ${PCIE_SFD_FILE}
        ${CMAKE_SOURCE_DIR}/cmake/GenerateSfdHeaders.cmake
    VERBATIM
)

add_custom_target(pcie_library_sfd_headers ALL DEPENDS ${PCIE_GENERATED_HEADERS})

# Dependencies
find_package(Emu68Common CONFIG REQUIRED)
find_package(GIC400 CONFIG REQUIRED)
find_package(mailbox CONFIG REQUIRED)

# Compile definitions and options
add_compile_definitions(
    PRIVATE
        LIBRARY_IDSTRING="${VERSTRING}"
        LIBRARY_VERSION=${PROJECT_VERSION_MAJOR}
        LIBRARY_REVISION=${PROJECT_VERSION_MINOR}
        LIBRARY_NAME="${PROJECT_NAME}"
)

add_compile_options(
    -Os
    -m68040
    -fomit-frame-pointer
    -Wall
    -Wextra
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wmissing-prototypes
    -Wstrict-prototypes
)

# Debug-output backend defines (EMU68_DEBUG_BACKEND, see emu68-common).
emu68_debug_backend_definitions()

add_link_options(
    -ffreestanding
    -nostdlib
    -nostartfiles
    -Wall
    -Wextra
    -s
    -Wl,-e,_doNotExecute
)

# Source files
file(GLOB pcie_core_sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/pcie/src/*.c)

# Targets
add_library(pcie_headers INTERFACE)

add_executable(pcie_library
    src/pcie_main.c
    src/pcie_find.c
    src/pcie_board.c
    src/pcie_irq.c
    src/pcie_io.c
    src/pcie_mem.c
    ${pcie_core_sources}
    src/pcie_end.c
)

target_include_directories(pcie_headers
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PCIE_GENERATED_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# Both pcie/include/pci.h and libraries/openpci.h define the same PCI config-space
# constants with semantically identical but syntactically different values.
# Mark both include roots as SYSTEM to suppress the resulting redefinition warnings.
target_include_directories(pcie_library SYSTEM
    PRIVATE
        ${CMAKE_SOURCE_DIR}/pcie/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(pcie_library
    PRIVATE
        ${PCIE_GENERATED_INCLUDE_DIR}
        $<TARGET_PROPERTY:mailbox::mailbox,INTERFACE_INCLUDE_DIRECTORIES>
)

add_dependencies(pcie_library pcie_library_sfd_headers)
add_dependencies(pcie_headers pcie_library_sfd_headers)

target_link_libraries(pcie_library
    PRIVATE
        Emu68Common::common
        GIC400::gic400_headers
        gcc
)

# ROM-able: no writable .data/.bss may sneak in. For the serial backend this instead
# links debug.lib (+ __divsi3 glue) and skips the check.
emu68_debug_backend_finalize(pcie_library ROMABLE)

set_target_properties(pcie_library PROPERTIES
    OUTPUT_NAME bcmpcie
    SUFFIX ".library"
)

# Install headers
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include
    PATTERN "pcie_private.h" EXCLUDE
)

install(
    DIRECTORY ${PCIE_GENERATED_INCLUDE_DIR}/
    DESTINATION include
)

# Install targets
install(
    TARGETS pcie_headers pcie_library
    EXPORT Emu68PCIeTargets
    RUNTIME DESTINATION LIBS
)

install(
    EXPORT Emu68PCIeTargets
    NAMESPACE Emu68PCIe::
    FILE Emu68PCIeTargets.cmake
    DESTINATION lib/cmake/Emu68PCIe
)

# CMake package config
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/Emu68PCIeConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/Emu68PCIeConfig.cmake
    INSTALL_DESTINATION lib/cmake/Emu68PCIe
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/Emu68PCIeConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/Emu68PCIeConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/Emu68PCIeConfigVersion.cmake
    DESTINATION lib/cmake/Emu68PCIe
)
