cmake_minimum_required(VERSION 3.14.0)
project(openpci.library VERSION 45.12 LANGUAGES C ASM)

string(TIMESTAMP CURRENT_DATE "(%d.%m.%Y)")
set(VERSTRING "$VER: ${PROJECT_NAME} ${PROJECT_VERSION} ${CURRENT_DATE}")

add_compile_options(
    -Os -m68040 -fomit-frame-pointer
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow
    -Wmissing-prototypes -Wstrict-prototypes
)
add_link_options(
    -ffreestanding -nostdlib -nostartfiles
    -Wall -Wextra -s
    -Wl,-e,_doNotExecute
)

add_executable(openpci_library
    src/openpci_main.c
    src/openpci_trampolines.S
    src/openpci_end.c
)

target_compile_definitions(openpci_library PRIVATE
    LIBRARY_NAME="${PROJECT_NAME}"
    LIBRARY_IDSTRING="${VERSTRING}"
    LIBRARY_VERSION=${PROJECT_VERSION_MAJOR}
    LIBRARY_REVISION=${PROJECT_VERSION_MINOR}
)

target_include_directories(openpci_library PRIVATE
    ${CMAKE_SOURCE_DIR}/bcmpcie.library/include
)

target_link_libraries(openpci_library PRIVATE gcc)

set_target_properties(openpci_library PROPERTIES
    OUTPUT_NAME openpci
    SUFFIX ".library"
)

install(TARGETS openpci_library
    RUNTIME DESTINATION LIBS
)
