cmake_minimum_required(VERSION 3.14.0)
project(pcie.library VERSION ${CMAKE_PROJECT_VERSION})

add_compile_options(-ffreestanding -nostdlib -nostartfiles -Os -m68040 -fomit-frame-pointer -Wall -Wno-unused-function -Wno-shift-count-overflow -DDEBUG)
file(GLOB_RECURSE pcie_sources CONFIGURE_DEPENDS src/*.c)
add_library(pcie.library STATIC ${pcie_sources})

target_include_directories(pcie.library
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
)

target_link_libraries(pcie.library
	PUBLIC
		Emu68Common::common
		GIC400::gic400_headers
)

set_target_properties(pcie.library PROPERTIES
	EXPORT_NAME pcie
)

install(TARGETS pcie.library
	EXPORT Emu68PCIeTargets
	ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
	DESTINATION include
)
