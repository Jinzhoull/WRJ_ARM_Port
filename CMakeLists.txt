cmake_minimum_required(VERSION 3.20)
project(wrj_arm_port C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(WRJ_ENABLE_NEON "Enable ARM NEON-specific code paths when available" OFF)
option(WRJ_ENABLE_PROFILING "Enable Module3 stage timing and operation counters" OFF)

file(GLOB_RECURSE WRJ_FINGERPRINT_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h.in")
list(SORT WRJ_FINGERPRINT_FILES)
set(WRJ_FINGERPRINT_INPUT "E34_C_PORT_V1\n")
foreach(WRJ_SOURCE_FILE IN LISTS WRJ_FINGERPRINT_FILES)
    file(SHA256 "${WRJ_SOURCE_FILE}" WRJ_FILE_SHA)
    file(RELATIVE_PATH WRJ_RELATIVE_FILE "${CMAKE_CURRENT_SOURCE_DIR}" "${WRJ_SOURCE_FILE}")
    string(APPEND WRJ_FINGERPRINT_INPUT "${WRJ_RELATIVE_FILE}:${WRJ_FILE_SHA}\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WRJ_SOURCE_FILE}")
endforeach()
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" WRJ_CMAKE_SHA)
string(APPEND WRJ_FINGERPRINT_INPUT "CMakeLists.txt:${WRJ_CMAKE_SHA}\n")
string(SHA256 WRJ_SOURCE_FINGERPRINT "${WRJ_FINGERPRINT_INPUT}")
configure_file(include/wrj_port_version.h.in generated/wrj_port_version.h @ONLY)

add_library(wrj_common STATIC
    src/wrj_io.c)
target_include_directories(wrj_common PUBLIC include)
if(WRJ_ENABLE_PROFILING)
    target_sources(wrj_common PRIVATE src/common/perf_timer.c)
    target_compile_definitions(wrj_common PUBLIC WRJ_ENABLE_PROFILING=1)
endif()

add_library(wrj_module3 STATIC
    src/module3/ble_e34.c
    src/module3/ble_filter.c
    src/module4/e34_phy.c
    src/module3/dsp64.c
    src/module3/ble_fast_sync.c
    src/module3/e34_module3.c
    src/module3/module3.c
    src/module3/cfo.c
    src/module3/sync.c
    src/module3/dsp.c
    src/module3/profiles.c
    src/module3/ble_low_snr.c
    src/module3/fft.c)
target_include_directories(wrj_module3 PUBLIC include)
target_include_directories(wrj_module3 PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(wrj_module3 PUBLIC wrj_common)

add_library(wrj_module4 STATIC
    src/module4/e34_protocol.c
    src/module4/e34_ofdm.c
    src/module4/e34_sc.c
    src/module4/e34_openset.c
    src/module4/e34_drone.c
    src/module4/e34_remote.c
    src/module4/e34_module4.c
    src/module4/module4.c
    src/module4/module4_process.c
    src/module4/matlab_flow.c
    src/module4/matlab_statistics.c
    src/module4/module5_handoff.c
    src/module4/physical_recovery.c
    src/module4/profile_byte_recovery.c
    src/module4/ble_remoteid.c
    src/module4/field_parser.c
    src/module4/wideband/wideband_assembler.c
    src/module4/wideband/dji_wideband_parser.c)
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
add_executable(e34_receiver_probe tools/e34_receiver_probe.c)
add_library(wrj_module5 STATIC src/module5/e34_module5.c src/module5/e34_recovery_machine.c)
target_include_directories(wrj_module5 PUBLIC include)
target_link_libraries(wrj_module5 PUBLIC wrj_module4 wrj_module3)
if(NOT MSVC)
    target_compile_options(wrj_module5 PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()
target_link_libraries(e34_receiver_probe PRIVATE wrj_module5)
add_executable(wrj_c99_host tools/e34_receiver_probe.c)
target_link_libraries(wrj_c99_host PRIVATE wrj_module5 wrj_module4 wrj_module3 wrj_common)
if(NOT MSVC)
    target_link_libraries(wrj_c99_host PRIVATE m)
    target_compile_options(wrj_c99_host PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()
target_link_libraries(e34_receiver_probe PRIVATE wrj_module4 wrj_module3 wrj_common)
if(NOT MSVC)
    target_link_libraries(e34_receiver_probe PRIVATE m)
    target_compile_options(e34_receiver_probe PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()
add_executable(m3_e34_probe tools/m3_e34_probe.c)
target_link_libraries(m3_e34_probe PRIVATE wrj_module3 wrj_common)
add_executable(test_module3_frontend tests/test_module3_frontend.c)
target_link_libraries(test_module3_frontend PRIVATE wrj_module3 wrj_common)
if(NOT MSVC)
    target_link_libraries(m3_e34_probe PRIVATE m)
    target_link_libraries(test_module3_frontend PRIVATE m)
    target_compile_options(m3_e34_probe PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    target_compile_options(test_module3_frontend PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()
add_test(NAME module3_frontend COMMAND test_module3_frontend)
add_executable(test_module4 tests/test_module4.c)
target_include_directories(test_module4 PRIVATE include)
target_link_libraries(test_module4 PRIVATE wrj_module4 wrj_module3 wrj_common)
if(NOT MSVC)
    target_compile_options(test_module4 PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    target_link_libraries(test_module4 PRIVATE m)
endif()
add_test(NAME module4_unit COMMAND test_module4)
add_executable(test_e34_receiver tests/test_e34_receiver.c)
target_link_libraries(test_e34_receiver PRIVATE wrj_module5)
if(NOT MSVC)
    target_link_libraries(test_e34_receiver PRIVATE m)
    target_compile_options(test_e34_receiver PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()
add_test(NAME e34_receiver_evidence COMMAND test_e34_receiver)
add_executable(test_host_optimization tests/test_host_optimization.c)
target_link_libraries(test_host_optimization PRIVATE wrj_module5)
if(NOT MSVC)
    target_link_libraries(test_host_optimization PRIVATE m)
endif()
add_test(NAME host_optimization COMMAND test_host_optimization)

add_executable(test_success_rate_v2 tests/test_success_rate_v2.c)
target_link_libraries(test_success_rate_v2 PRIVATE wrj_module5)
if(NOT MSVC)
    target_link_libraries(test_success_rate_v2 PRIVATE m)
endif()
add_test(NAME success_rate_v2 COMMAND test_success_rate_v2)

add_executable(m4_frame_probe tools/m4_frame_probe.c)
target_include_directories(m4_frame_probe PRIVATE include)
target_link_libraries(m4_frame_probe PRIVATE wrj_module4 wrj_module3 wrj_common)
if(NOT MSVC)
    target_compile_options(m4_frame_probe PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    target_link_libraries(m4_frame_probe PRIVATE m)
endif()

add_executable(m4_matlab_frame_probe tools/m4_matlab_frame_probe.c)
target_include_directories(m4_matlab_frame_probe PRIVATE include)
target_link_libraries(m4_matlab_frame_probe PRIVATE wrj_module4 wrj_module3 wrj_common)
if(NOT MSVC)
    target_compile_options(m4_matlab_frame_probe PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    target_link_libraries(m4_matlab_frame_probe PRIVATE m)
endif()

add_executable(m4_matlab_statistics_probe tools/m4_matlab_statistics_probe.c)
target_include_directories(m4_matlab_statistics_probe PRIVATE include)
target_link_libraries(m4_matlab_statistics_probe PRIVATE wrj_module4 wrj_module3 wrj_common)
if(NOT MSVC)
    target_compile_options(m4_matlab_statistics_probe PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    target_link_libraries(m4_matlab_statistics_probe PRIVATE m)
endif()
