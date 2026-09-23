# ARMv7 Deployment Validation

Date: 2026-09-23

## Host and board

- Host: Ubuntu 20.04.6 LTS, x86_64
- Board: Linaro 14.04, Linux 4.14.0, Xilinx Zynq, `armv7l`
- Board ABI: ARM EABI5, hard-float, VFP register arguments
- Board libc: EGLIBC 2.19, `/lib/ld-linux-armhf.so.3`
- Board memory: approximately 497 MiB RAM, no swap

## Host package repair

The blocking package conflict was repaired using an explicit Focal-only
transaction. The complete native binutils family was restored to
`2.34-6ubuntu1.11`; native GCC/G++/CPP and `build-essential` were restored to
the Focal set; and the ARMv7 hard-float cross toolchain was installed.

Installed ARM tools include:

- `gcc-arm-linux-gnueabihf` 4:9.3.0-1ubuntu2
- `gcc-9-arm-linux-gnueabihf` 9.4.0-1ubuntu1~20.04.2cross2
- `binutils-arm-linux-gnueabihf` 2.34-6ubuntu1.11
- `libc6-dev-armhf-cross` 2.31-0ubuntu9.9cross1

The transaction removed only the conflicting `gcc-11` and `g++-11` packages.
`dpkg --audit`, `apt-get check`, and the native CMake/ctest check pass after
the transaction.

The host still contains unrelated newer packages, including host libc 2.35,
CMake 3.22, and some GCC 12 runtime packages. They were intentionally not
downgraded in this ARM toolchain repair because doing so would be a separate,
high-risk system-wide rollback.

## ARM build

Toolchain file: `cmake/armv7-linux-gnueabihf.cmake`

Compiler flags:

```text
-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard -O2
```

The dynamic build was rejected for board deployment because it requires
`GLIBC_2.27` and `GLIBC_2.29`, while the board provides EGLIBC 2.19.

The default build script now creates a static ARM executable in
`build-arm/wrj_arm_port`:

```text
./tools/build_arm.sh
```

Binary inspection confirms:

- ELF 32-bit ARM EABI5
- ARMv7
- VFPv3
- `Tag_ABI_VFP_args: VFP registers`
- no dynamic section

## Board smoke test and representative cases

Deployed to:

```text
/root/wrj_arm_test/bin/
```

The board-side `test_module4` completed successfully:

```text
module4 tests passed
real 0m0.003s
user 0m0.003s
sys  0m0.001s
```

The final non-instrumented `build-arm/wrj_arm_port` binary was also run via
`tools/run_board_test.sh` for DroneID 013 and RemoteID 005; both returned 0 and
produced the results summarized below.

The main executable also started successfully and returned the expected usage
status 2 when invoked without arguments. No crash or OOM occurred. Board RSS
was sampled from `/proc/<pid>/status` (`VmHWM`). Per-module times below use a
temporary linker wrapper around `m3_run` and `m4_run`; total wall and CPU times
come from the board process monitor. PC times are from the native GCC 9.4
build on the Ubuntu x86_64 host.

| Candidate | Profile / status | Module3 (ARM) | Module4 (ARM) | Total ARM | CPU ARM | Peak RSS ARM | Total PC / Peak RSS PC |
|---|---|---:|---:|---:|---:|---:|---:|
| DroneID 013 | `DJI_DroneID_ZC_CP` / `ok`, M4 `partial` | 4.347 s | 1.534 s | 6.011 s | 5.870 s | 9.7 MiB | 0.27 s / 11.3 MiB |
| RemoteID 005 | `RemoteID_BLE_GFSK` / `ok`, M4 `parsed` | 34.215 s | 3.792 s | 38.172 s | 38.030 s | 13.1 MiB | 1.24 s / 14.9 MiB |
| Autel Wideband 001 | `Autel_Wideband_CP` / `ok`, M4 `partial` | 19.340 s | 0.192 s | 19.825 s | 19.670 s | 20.4 MiB | 1.66 s / 22.3 MiB |
| DJI Wideband 018 | `DJI_Wideband_CP` / `ok`, M4 `partial` | 22.344 s | 0.378 s | 23.014 s | 22.870 s | 23.7 MiB | 1.75 s / 25.1 MiB |
| RemoteID 028 | `RemoteID_BLE_GFSK` / `ok`, M4 `crc_failed` | 288.967 s | <0.001 s | 289.135 s | 288.860 s | 9.1 MiB | 8.86 s / 10.8 MiB |

Final measurements use the static ARMv7 Release build at `-O2`. The host/board
output CSV files were compared for all five candidates. For each
case, `module3_frames.csv`, `module3_result.csv`, `module4_bytes.csv`,
`module4_fields.csv`, `module4_packets.csv`, and `module4_result.csv` are
byte-for-byte identical between PC and ARM.

Case details:

- DroneID 013: CFO 127222.672 Hz, confidence 0.914463, 9 frames of 19760
  samples, first start 14941. Module4 emitted 96 REAL_IQ bytes and remains
  `partial_parse`; checksum is unverified.
- RemoteID 005: CFO -1169564.750 Hz, confidence 0.980000, 16 frames of 13917
  samples, first start 10424. Four distinct CRC24-valid PDUs were recovered;
  UAS ID `UAS000185`, latitude 31.2260243, longitude 121.4682469, altitude
  131.0 m, speed 2.25 m/s, heading 117 degrees.
- Autel Wideband 001: CFO -3715.550 Hz, confidence 0.994028, 20 frames of
  1152 samples, first start 103642; 96 bytes per frame. Parse remains partial
  and CRC16 is unverified.
- DJI Wideband 018: CFO -158882.500 Hz, confidence 0.944292, 20 frames of
  2208 samples, first start 66556; 96 bytes per frame. Parse remains partial
  and CRC16 is unverified.
- RemoteID 028: CFO -1329814.875 Hz, confidence 0.958465, 9 frames of 16861
  samples, first start 6607. No CRC24-valid upstream candidate was found;
  Module4 emitted zero bytes. This matches the known PC failure.

All five outputs are under
`/root/wrj_arm_test/results/first-run-20260923/o2/` on the board; a byte-preserving
copy of those case directories and their `run.log` files is archived in
`results/arm/first-run-20260923-o2/`. All processes returned 0, and none crashed
or ran out of memory. ARM runtimes
are substantially longer than PC runtimes, especially for BLE recovery on
RemoteID 028. The current validation establishes ARM/PC numerical and output
parity; it does not resolve the existing RemoteID 028 low-SNR gap or turn
Wideband/DroneID `partial_parse` results into protocol validation.
