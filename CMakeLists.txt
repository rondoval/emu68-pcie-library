cmake_minimum_required(VERSION 3.14.0)
project(emu68-pcie-library VERSION 2.6)

# Dependencies
find_package(Emu68Common 2.1 REQUIRED) # 2.1: emu68_sfd_headers()
find_package(GIC400 1.3 REQUIRED)

# Set default install prefix to ./install subdirectory
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install path" FORCE)
endif()

# Subdirectories
add_subdirectory(bcmpcie.library)
add_subdirectory(openpci.library)
add_subdirectory(lspci)
