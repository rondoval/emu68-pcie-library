cmake_minimum_required(VERSION 3.14.0)
project(emu68-pcie-library VERSION 1.1)

# Dependencies
find_package(Emu68Common REQUIRED)
find_package(GIC400 REQUIRED)

# Set default install prefix to ./install subdirectory
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install path" FORCE)
endif()

# Subdirectories
add_subdirectory(bcmpcie.library)
add_subdirectory(openpci.library)
add_subdirectory(lspci)
