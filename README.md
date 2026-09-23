# WRJ ARM Port

MATLAB 无线协议分析系统的 C99 / ARM Linux 移植工程。当前从 Module1/2 生成的 handoff CSV 与 CF32 IQ 输入开始，在 C 中运行 Module3 频偏补偿与同步，以及 Module4 IQ→Byte 和字段解析。

当前覆盖 Wideband、DroneID、RemoteID BLE、Control/Burst 和 Unknown 等接收路径。这里的“支持”表示对应的同步、恢复或解析路径可运行；Wideband、DroneID 和 Unknown 的协议语义尚未完整验证，不能把 `partial_parse` 当作完整厂商协议解析。

## 工程结构

```text
cmake/       ARMv7 toolchain
include/     C interfaces and data types
src/         Module3, Module4, shared I/O and CLI
tests/       C unit tests
tools/       PC/ARM build, board deployment and validators
data/        one handoff CSV and 11 validated CF32 cases
results/pc/  preserved PC result batches
results/arm/ preserved ARM board validation results
build-pc/    host builds: linux/ and windows/
build-arm/   Ubuntu ARMv7 cross-build
docs/        architecture, build, status and validation records
```

## PC 编译与测试

Windows：

```bat
tools\build_pc.bat
```

Ubuntu/Linux：

```sh
./tools/build_pc.sh
```

运行单元测试时设置 `RUN_TESTS=1`（Windows 环境变量或 Linux shell 环境变量）。完整命令见 [BUILD_AND_RUN.md](docs/BUILD_AND_RUN.md)。

## Development Workflow

Windows 是日常源码开发和 PC 验证环境；变更经 Git commit 后推送到 GitHub。Ubuntu 通过 GitHub 拉取代码，负责 ARMv7 交叉编译和部署；`analog-board` 用于最终运行及性能验证。详细步骤见 [DEVELOPMENT_WORKFLOW.md](docs/DEVELOPMENT_WORKFLOW.md)。

## ARM 编译、部署与运行

Ubuntu 上执行 `./tools/build_arm.sh`，生成 ARMv7 hard-float 静态 `-O2` 程序。使用 `./tools/deploy_board.sh` 上传程序和显式指定的测试输入，再用 `./tools/run_board_test.sh` 运行单个 case。板端结果会复制到 `results/arm/`。具体命令见 [BUILD_AND_RUN.md](docs/BUILD_AND_RUN.md)。

## 当前验证

- DroneID 013 与 RemoteID 005 已在 Zynq ARMv7 板端运行。
- Autel Wideband 001、DJI Wideband 018 和 RemoteID 028 也完成板端代表性运行。
- [ARM_VALIDATION.md](docs/ARM_VALIDATION.md) 记录了五个 case 的 PC/ARM 对比结论。清理时发现现存 MinGW PC 归档与 ARM 归档在 DroneID 013 多个 CSV 及 DJI Wideband 018 的 `module4_bytes.csv` 上不完全一致；新建 Ubuntu PC 的 DroneID 013 smoke 输出则与对应 ARM CSV 一致。两边原始结果均保留，细节记于 [PROJECT_CLEANUP_PLAN.md](docs/PROJECT_CLEANUP_PLAN.md)。
- RemoteID 005 恢复四个 CRC24 有效 PDU；RemoteID 028 仍为零 CRC 候选；Wideband/DroneID 输出仍为 partial parse。

详细指标和环境记录见 [ARM_VALIDATION.md](docs/ARM_VALIDATION.md)、[MODULE3_VALIDATION.md](docs/MODULE3_VALIDATION.md)、[MODULE4_VALIDATION.md](docs/MODULE4_VALIDATION.md) 与 [REAL_IQ_BYTE_ALIGNMENT.md](docs/REAL_IQ_BYTE_ALIGNMENT.md)。

## 已知限制

- Module1、Module2 和 Module5 尚未移植；当前程序读取 Module1/2 handoff 与 IQ 文件。
- Wideband/DroneID 的厂商帧头、校验、均衡/FEC 等协议语义未完成验证。
- RemoteID 028 的低信噪比路径在 ARM 上耗时约 289 秒，仍无 CRC24 有效候选。
- ARM GCC 9 构建有现存 sign-conversion warnings；AArch64 未验证。

当前移植状态与后续工作见 [PORTING_STATUS.md](docs/PORTING_STATUS.md)。
