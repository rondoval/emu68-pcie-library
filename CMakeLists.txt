cmake_minimum_required(VERSION 3.14.0)
project(emu68-pcie-library VERSION 0.7)

include(CMakePackageConfigHelpers)

# Depend on shared common code from the Emu68Common package
find_package(Emu68Common REQUIRED)
find_package(GIC400 REQUIRED)

# Set default install prefix to ./install subdirectory
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install path" FORCE)
endif()

add_subdirectory(pcie.library)
add_subdirectory(lspci)

install(EXPORT Emu68PCIeTargets
    NAMESPACE Emu68PCIe::
    FILE Emu68PCIeTargets.cmake
    DESTINATION lib/cmake/Emu68PCIe
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Emu68PCIeConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/Emu68PCIeConfig.cmake
    INSTALL_DESTINATION lib/cmake/Emu68PCIe
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/Emu68PCIeConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/Emu68PCIeConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/Emu68PCIeConfigVersion.cmake
    DESTINATION lib/cmake/Emu68PCIe
)
