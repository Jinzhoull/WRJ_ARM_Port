cmake_minimum_required(VERSION 3.20)
project(wrj_arm_port C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(WRJ_ENABLE_NEON "Enable ARM NEON-specific code paths when available" OFF)

add_library(wrj_common STATIC
    src/wrj_io.c)
target_include_directories(wrj_common PUBLIC include)

add_library(wrj_module3 STATIC
    src/module3/module3.c
    src/module3/cfo.c
    src/module3/sync.c
    src/module3/dsp.c
    src/module3/profiles.c
    src/module3/fft.c)
target_include_directories(wrj_module3 PUBLIC include)
target_link_libraries(wrj_module3 PUBLIC wrj_common)

add_library(wrj_module4 STATIC
    src/module4/module4.c
    src/module4/physical_recovery.c
    src/module4/profile_byte_recovery.c
    src/module4/ble_remoteid.c
    src/module4/field_parser.c)
target_include_directories(wrj_module4 PUBLIC include)
target_link_libraries(wrj_module4 PUBLIC wrj_module3 wrj_common)

add_executable(wrj_arm_port src/main.c)
target_include_directories(wrj_arm_port PRIVATE include)
target_link_libraries(wrj_arm_port PRIVATE wrj_module3 wrj_module4 wrj_common)

if(NOT MSVC)
    target_compile_options(wrj_common PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    target_compile_options(wrj_module3 PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    target_compile_options(wrj_module4 PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    target_compile_options(wrj_arm_port PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    target_link_libraries(wrj_arm_port PRIVATE m)
endif()

if(WRJ_ENABLE_NEON)
    target_compile_definitions(wrj_module3 PUBLIC WRJ_ENABLE_NEON=1)
endif()

enable_testing()
add_executable(test_module4 tests/test_module4.c)
target_include_directories(test_module4 PRIVATE include)
target_link_libraries(test_module4 PRIVATE wrj_module4 wrj_module3 wrj_common)
if(NOT MSVC)
    target_compile_options(test_module4 PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    target_link_libraries(test_module4 PRIVATE m)
endif()
add_test(NAME module4_unit COMMAND test_module4)

add_executable(m4_frame_probe tools/m4_frame_probe.c)
target_include_directories(m4_frame_probe PRIVATE include)
target_link_libraries(m4_frame_probe PRIVATE wrj_module4 wrj_module3 wrj_common)
if(NOT MSVC)
    target_compile_options(m4_frame_probe PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    target_link_libraries(m4_frame_probe PRIVATE m)
endif()
