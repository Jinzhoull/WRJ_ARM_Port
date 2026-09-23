# Build, Test and Run

所有操作都从工程根目录开始。Windows 与 Linux 的 PC 构建分别使用 `build-pc/windows/` 和 `build-pc/linux/`，避免跨系统共用不兼容的 CMake cache；ARM 构建固定使用 `build-arm/`。

## PC：Windows

Release build：

```bat
tools\build_pc.bat
```

启用单元测试：

```bat
set RUN_TESTS=1
tools\build_pc.bat
```

也可在完成 build 后单独运行：

```bat
ctest --test-dir build-pc\windows -C Release --output-on-failure
```

脚本从自身位置推导工程路径，不依赖写死的工程绝对路径。

## PC：Ubuntu/Linux

```sh
./tools/build_pc.sh
```

构建并运行 CTest：

```sh
RUN_TESTS=1 ./tools/build_pc.sh
```

单独运行测试：

```sh
ctest --test-dir build-pc/linux --output-on-failure
```

当前 CTest 包含 Module4 单元测试。Module3 没有单独的 CTest target；可用 DroneID 013 做一次端到端 smoke test，输出放在构建目录而不是正式 `results/`：

```sh
./build-pc/linux/wrj_arm_port \
  --handoff data/module12_to_module3_handoff.csv \
  --candidate sim_dji_droneid_013_M001 \
  --iq data/cases/sim_dji_droneid_013_M001.cf32 \
  --out "build-pc/linux/smoke-results/droneid013-$(date +%Y%m%d-%H%M%S)"
```

要运行别的 candidate，需同时提供 handoff 中的 candidate ID 和对应 CF32 文件。不要把 smoke-test 输出写进已归档的正式结果目录。

## ARMv7 cross-build

在 Ubuntu 安装并使用 `arm-linux-gnueabihf` 工具链：

```sh
./tools/build_arm.sh
```

输出为 `build-arm/wrj_arm_port`。当前成功验证的参数保持为 ARMv7、hard-float、静态链接、Release `-O2`；toolchain 细节在 `cmake/armv7-linux-gnueabihf.cmake`。

## 部署一个 case 到开发板

默认主机为 SSH alias `analog-board`，板端根目录为 `/root/wrj_arm_test`。上传程序、单份 handoff 和一个明确的 IQ 输入：

```sh
HANDOFF_FILE=data/module12_to_module3_handoff.csv \
IQ_FILE=data/cases/sim_dji_droneid_013_M001.cf32 \
./tools/deploy_board.sh
```

运行同一 case：

```sh
HANDOFF_NAME=data/module12_to_module3_handoff.csv \
CANDIDATE_ID=sim_dji_droneid_013_M001 \
IQ_NAME=data/sim_dji_droneid_013_M001.cf32 \
./tools/run_board_test.sh
```

部署脚本只上传，不运行程序。运行脚本每次使用新的时间戳结果目录，将控制台日志保存到 `results/arm/logs/`，并把板端 CSV 复制到 `results/arm/`，避免覆盖已归档验证结果。板端参数、默认目录和静态链接原因见 [ARM_DEPLOYMENT.md](ARM_DEPLOYMENT.md)。

## 验证报告工具

`tools/validate_module3.py` 对比现有 C Module3 输出与外部 MATLAB 正式表格；默认读取 `results/pc/module3_regression/cases/`，并写入 `docs/MODULE3_VALIDATION.md`。该命令会重生成验证文档，只应在有意更新正式报告时运行；可用 `--output` 指向临时路径进行检查。

`tools/validate_module4.py --results results/pc/module4_regression/cases` 汇总 Module4 CSV；需要 MATLAB RemoteID candidate-IQ 表时再传 `--matlab-remoteid`。它不会运行 IQ 样本。
