# WRJ_ARM_Port 工程整理计划

审计日期：2026-09-23

## 整理边界

- 只整理构建目录、脚本、数据目录、结果目录和文档；不修改 `src/`、`include/`、`tests/`、`CMakeLists.txt` 中的算法或接口。
- 不改写正式验证指标或现有 PC/ARM CSV 内容；结果按原文件内容迁移。
- 当前目录不是 Git 仓库，没有 `.git` 元数据可用于恢复。不能确认用途的文件保留。
- 所有 11 个 CF32 输入都在现有 `MODULE3_VALIDATION.md` 的 11-case 验证集中；因此全部保留。ARM 五个代表样本是其中子集，不是唯一可保留的测试集。

## 审计摘要（整理前）

- 根目录有 `build/`、`build-mingw/`、`build-armv7/`、`build-armv7-static/` 四个构建目录，没有 `build-pc/` 或 `build-arm/`。
- `build/` 只有不完整的 NMake/CMake 配置缓存；`build-mingw/` 有 MinGW Windows 构建产物；`build-armv7/` 是文档记录的 ARMv7 hard-float `-O2` 静态验证构建；`build-armv7-static/` 的缓存使用 `-O3` Release 参数，不是文档所记录的最终验证配置。
- 迁移后验证发现：CMake cache 将构建目录的绝对路径写入 `CMakeCache.txt`。`build-armv7/` 与 MinGW 构建移动到新目录后不能直接复用；ARM 旧 cache 已备份后在 `build-arm/` 成功重新配置构建。MinGW 旧产物也不能作为 `build-pc/windows/` 的有效 cache 使用，Windows 脚本必须从干净目录配置。
- `data/cases/` 有 11 个各自不同的 CF32 文件（约 39 MiB）；handoff CSV 只有一份。`tools/data/cases/` 是空目录。
- `results/` 有三个不相同的 PC 结果批次：较早的根级 case 输出、Module3 `regression/`、Module4 `module4_regression/`。Module3/4 文档和验证脚本分别使用后两者，故三者均保留并分层归类。
- 开发板上的 ARM 首次验证结果存在于 `/root/wrj_arm_test/results/first-run-20260923/o2/`，约 184 KiB；计划只复制到工程，不删除或改写板端原件。
- 发现的文档包括 `ARM_FIRST_RUN.md`、Module3/4 gap 文档以及简短的 `VALIDATION.md`。算法移植前的 gap 表格已过时；有价值的未决问题、数据来源边界和复现说明会先合并到当前状态/构建文档，再移除旧文档。
- 除构建缓存中的 CMake 日志/探测文件外，没有发现工程根目录中的 `.tmp`、`.bak`、`.old`、`.log`、`.pyc`、压缩包或独立调试 ELF。构建目录外的验证文件均保留。

## 保留

- `CMakeLists.txt`、`include/`、`src/`、`tests/` 和 `cmake/armv7-linux-gnueabihf.cmake` 原样保留。
- `data/module12_to_module3_handoff.csv` 保留一份；`data/cases/` 下全部 11 个唯一 CF32 输入保留，继续采用现有文件名，避免破坏脚本和命令行路径。
- 正式结果 CSV 原样保留：根级旧批次、Module3 regression 批次、Module4 regression 批次以及从板端复制的五个 ARM case 目录和原 `run.log`。
- `tools/m4_frame_probe.c` 是 `validate_real_iq_bytes.py` 使用的探针源文件，不是临时文件，保留。
- `MODULE3_VALIDATION.md`、`MODULE4_VALIDATION.md`、`REAL_IQ_BYTE_ALIGNMENT.md` 中的正式指标与结论不重算、不改写。

## 移动与重命名

- `build-mingw/` 曾迁入 `build-pc/windows/` 以审计 cache；因其 CMake cache 固定记录旧的绝对目录，不能复用。确认其中只有可重建的 MinGW binary/object/cache 后，移除该 stale 子目录；Windows 脚本在 Windows 主机上从干净的 `build-pc/windows/` 配置。
- `build-armv7/` → `build-arm/`，其参数与 `ARM_FIRST_RUN.md` 记载的已验证配置一致；更新 ARM 脚本默认目录为 `build-arm/` 并重新配置/构建。
- `results/regression/` → `results/pc/module3_regression/cases/`。
- `results/module4_regression/` → `results/pc/module4_regression/cases/`。
- 根级 `results/<case>/` → `results/pc/module3_initial/<case>/`，作为较早且内容不同的结果批次保留，不与正式 regression 文件覆盖或混合。
- 板端 `/root/wrj_arm_test/results/first-run-20260923/o2/` 只读复制至 `results/arm/first-run-20260923-o2/`。
- `docs/ARM_FIRST_RUN.md` → `docs/ARM_VALIDATION.md`。仅更新构建目录/文档链接；验证数字和结果内容不变。

## 文档合并/整理

- `MODULE3_PORT_GAP.md` 中当前仍有效的 Module3 未决项与 ARM 性能热点合并到 `PORTING_STATUS.md`；已实现功能和旧“待移植”叙述以当前验证文档为准。
- `MODULE4_PORT_GAP.md` 中 `REAL_IQ` 与 synthetic harness 的证据边界、RemoteID 028 零 CRC、Wideband/DroneID partial parse 等当前仍有效结论合并到 `PORTING_STATUS.md`。
- `VALIDATION.md` 的 Module3 验证脚本/输出说明并入 `BUILD_AND_RUN.md`。
- 整理 `README.md`、`ARCHITECTURE.md`、`PORTING_STATUS.md`、`MODULE3_VALIDATION.md`、`MODULE4_VALIDATION.md`、`REAL_IQ_BYTE_ALIGNMENT.md` 的路径引用；正式验证数据本身不变。
- 新建唯一操作命令入口 `BUILD_AND_RUN.md` 和板端配置说明 `ARM_DEPLOYMENT.md`；首次实机结果归档为 `ARM_VALIDATION.md`。
- `data/README.md` 更新为当前 11-case 与 handoff 说明。

## 新增或整理脚本

- 新增 `tools/build_pc.sh`（Ubuntu/Linux，输出 `build-pc/linux/`）和 `tools/build_pc.bat`（Windows，输出 `build-pc/windows/`）。两个平台共用 `build-pc/` 顶层但不共用不兼容的 CMake cache。
- 整理 `tools/build_arm.sh`，默认输出 `build-arm/`；保持 ARMv7、hard-float、静态链接、`-O2` 和当前 toolchain 参数。
- 新增 `tools/board_config.sh`，只提供 `analog-board` 和 `/root/wrj_arm_test` 默认值，不存储任何凭据。
- 整理部署/运行脚本以读取板端默认配置；运行日志及从开发板取回的输出写入 `results/arm/`，每次使用新时间戳路径避免覆盖既有结果。
- 更新 `tools/validate_module3.py` 的默认 PC regression 路径为新位置。
- 新增 `.gitignore` 仅忽略临时文件、日志、Python 缓存和 CMake 生成文件；明确不忽略 `build-pc/`、`build-arm/`，不涉及 Git 提交策略（本目录无 Git 仓库）。

## 删除（仅以下已确认对象）

- `build/`：只有不完整 CMake/NMake cache 与配置探测文件，无可执行文件或唯一源码/测试数据；配置入口由新 PC 构建脚本取代。
- `build-armv7-static/`：仅为可重建的生成物，缓存为 `-O3`，不是已记录并部署验证的 `-O2` ARM 构建；长期配置由 toolchain 文件和 ARM 脚本保存，已验证的 `build-armv7/` 会迁入 `build-arm/`。
- `build-pc/windows/` 中已迁入的旧 MinGW build：cache 指向迁移前目录；只有构建生成物，Windows 构建脚本会在该路径重新创建所有产物。
- 完成信息迁移后删除 `docs/MODULE3_PORT_GAP.md`、`docs/MODULE4_PORT_GAP.md`、`docs/VALIDATION.md`，避免保留过时重复入口。
- 删除空目录 `tools/data/cases/`。
- 不删除任何 CF32、handoff CSV、验证结果 CSV、ARM 日志、源码或无法判断用途的文件。

## 整理后验证

1. Ubuntu PC Release 构建至 `build-pc/linux/`，运行 `ctest`。
2. 运行现有 Module4 单元测试，并用单个 DroneID 013 输入做 Module3/端到端 smoke test；输出放在 build 目录，不覆盖正式结果。
3. 运行 `tools/build_arm.sh`，确认 `build-arm/wrj_arm_port` 是静态 ARMv7 hard-float ELF。
4. 不重跑全部样本，不重跑 RemoteID 028 的 289 秒板端测试，不改正式验证 CSV/指标。

## 预计顶层结构

```text
WRJ_ARM_Port/
├── build-pc/                 # linux/ 与 windows/ 各自使用本机 CMake cache
├── build-arm/                # Ubuntu ARMv7 hard-float 静态构建
├── cmake/ include/ src/ tests/
├── data/cases/               # 11 个唯一验证输入 + 单份 handoff CSV
├── results/pc/               # initial、Module3、Module4 PC 批次
├── results/arm/              # 五个首次实机 case 和运行日志
├── tools/
└── docs/
```

## 整理执行中的结果归档核对

- 板端五个 case 目录从 `analog-board` 只读复制到 `results/arm/first-run-20260923-o2/`；逐文件 SHA-256 与板端一致。
- 现存 `results/pc/module4_regression/cases/` 是 MinGW Release 归档（`MODULE4_VALIDATION.md` 有记录）。与板端五个 case 直接比较时，RemoteID 005、Autel Wideband 001、RemoteID 028 的 CSV 相同；DroneID 013 的多个 CSV、DJI Wideband 018 的 `module4_bytes.csv` 不同。ARM 归档的 `run.log` 是额外板端日志。
- 本轮 Ubuntu GCC 9.4 PC Release 的 DroneID 013 smoke CSV 与板端对应 CSV 相同（忽略只有板端存在的 `run.log`）。没有为消除归档差异而重跑/覆盖正式结果，也没有改写 `ARM_VALIDATION.md` 的既有指标；因此不同构建来源的原始结果分开保留。
- 可选 HDF5 real-IQ validator 因当前环境未安装 Python `h5py` 无法启动；本轮未安装依赖或重跑该正式验证。

## 整理后验证记录

- Ubuntu PC Release build 成功；`ctest`：1/1 passed。
- DroneID 013 PC smoke test 成功（Module3 `ok`、Module4 `partial`），输出只写在 `build-pc/linux/smoke-results/`。
- ARM build 成功；产物为 ELF32 ARM EABI5 hard-float、VFPv3、无 dynamic section；CMake flags 为 `-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard -O2`，static link。
- Shell 脚本语法和三个 Python validator 的 AST 语法检查通过。Windows `.bat` 未在 Ubuntu 执行；未重新部署或运行开发板 case。
- 源码、`include/`、`tests/`、`CMakeLists.txt` 和 ARM toolchain 文件未修改；未重跑全部样本，未重跑 RemoteID 028 的板端长测。
