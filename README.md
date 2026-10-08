# E34 C99 Host

当前行为标准为上级九文件 MATLAB 主工程及 `../results/`。Receiver 只读取 actual capture-domain CF32、allowlist handoff 与自身计算结果；Golden 由进程外 validator/comparator 读取。

```powershell
& tools/build_e34_host.ps1
& results_c_validation/final/build/wrj_c99_host.exe host handoff.csv candidate_id actual.cf32 output_dir
python tools/compare_e34_host.py --run-name final
python tools/validate_e34_m3.py --reuse-probes --executable results_c_validation/final/build/m3_e34_probe.exe
python tools/verify_e34_integrity.py
```

保存的正式 C 输出位于 `results_c_validation/host53/c_final_host/`；acquisition inputs 与 comparator-only Golden 分开。m3_core 保留12核心/36前处理和CFO阶段证据；final 保留当前构建、CTest 与完整性结果。仅保留这三个正式验收分支。

完整 receiver fresh 复现可用 `validate_e34_receiver.py --mode host --run-name final`；平常离线复查无需重跑。便携 GCC15.2.0/CMake3.31.10 位于 `tools/host_toolchain/`。本轮src/include/CMakeLists含有界软列表、cache与profiling改动，.git保留，无提交；fresh53和CTest4/4已完成。

当前入口、指标、指纹与范围见 [C_PORT_STATUS.md](docs/C_PORT_STATUS.md)。下一阶段 ARM Phase7 的交叉构建入口为 `tools/build_arm.sh`；本轮未运行 ARM，旧 Standalone 只读。
