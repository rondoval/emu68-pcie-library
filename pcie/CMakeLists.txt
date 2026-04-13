cmake_minimum_required(VERSION 3.14.0)
project(pcie VERSION ${CMAKE_PROJECT_VERSION})

find_package(mailbox CONFIG REQUIRED)

add_compile_options(-ffreestanding -nostdlib -nostartfiles -Os -m68040 -fomit-frame-pointer -Wall -Wconversion -Wsign-conversion -Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wno-unused-function -Wno-shift-count-overflow -DDEBUG)
file(GLOB_RECURSE pcie_sources CONFIGURE_DEPENDS src/*.c)
add_library(pcie STATIC ${pcie_sources})

target_include_directories(pcie
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
	PRIVATE
		$<TARGET_PROPERTY:mailbox::mailbox,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(pcie
	PUBLIC
		Emu68Common::common
		GIC400::gic400_headers
		gcc
)

set_target_properties(pcie PROPERTIES
	EXPORT_NAME pcie
)

install(TARGETS pcie
	EXPORT Emu68PCIeTargets
	ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
	DESTINATION include
)
