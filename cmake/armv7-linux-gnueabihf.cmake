set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_ASM_COMPILER arm-linux-gnueabihf-gcc)

set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_RELEASE_INIT "-O2 -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_INIT "")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
