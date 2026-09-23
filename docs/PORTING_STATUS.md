# Porting Status

状态依据现有 C 实现、PC 验证表和 2026-09-23 ARMv7 实机记录整理。详细逐 case 数据见 [MODULE3_VALIDATION.md](MODULE3_VALIDATION.md)、[MODULE4_VALIDATION.md](MODULE4_VALIDATION.md)、[REAL_IQ_BYTE_ALIGNMENT.md](REAL_IQ_BYTE_ALIGNMENT.md) 与 [ARM_VALIDATION.md](ARM_VALIDATION.md)。

## Module3

### 已完成

- 读取 Module1/2 handoff CSV 和 CF32 candidate IQ；输入保留原始 candidate 元数据。
- float32 预处理、candidate-centre translation、频谱中心/CFO 估计与补偿、有限整数 CFO 假设、numerology/profile 选择。
- Wideband CP 同步和 frame diagnostics；DroneID ZC/CP 与绝对时序路径；RemoteID BLE 同步、残余 CFO/CRC24 诊断；Control/Burst、Unknown repetition 与有界 profile fallback。
- 结果输出包含 profile、CFO、frame、confidence、DroneID、BLE、SFO 等诊断。默认 917,504 复数样本 workspace 约 29.06 MiB，buffer 复用。

### 当前验证与待优化

- Module3 对 11 个代表输入均可运行。现有 C/MATLAB 报告为 4 PASS、7 WARN、0 FAIL；Wideband 通过 profile/status、frame 长度/count 和首帧起点检查，但两个 case 的 20 帧覆盖为 19/20。
- 非 Wideband 路径已实现并可运行，但部分 frame grid/CFO 与 MATLAB 不完全一致；DroneID candidate population/confidence 仍需校准，不能用放宽生产阈值替代 parity。
- RemoteID 028 低 SNR 输入当前仍无 CRC-valid 候选。ARM 总耗时 289.135 秒，是下一阶段最明显的性能热点；另需分析 RemoteID 005（38.172 秒）和 Wideband（约 20–23 秒）的 ARM 热点。

## Module4

### 已完成

- RemoteID BLE GFSK/access-address、dewhitening、CRC24、distinct PDU 和 Basic ID/Location/System/Operator ID 解析。
- Wideband CP/FFT/QPSK、DroneID ZC/CP/QPSK real-IQ byte recovery，以及 Unknown 有界结构候选输出。
- 输出明确标记 `REAL_IQ` 来源、CRC/packet 证据和 parse completeness；不以 MATLAB synthetic validation harness 字节替代 IQ 解码失败结果。
- 与 MATLAB `iqRecoveredBytes` 的同帧对比：Autel Wideband 001 95.16%，DroneID 013 100%，DroneID 022 99.83%，DJI Wideband 018 85.23%（消除未决整帧 180° QPSK polarity 后为 100%）。这些数字只验证 receiver-byte 对齐，不代表厂商协议正确。

### 待完善与证据边界

- RemoteID 005 的 4 个 distinct CRC24-valid PDU 可解析出 Basic ID/Location；RemoteID 028 因 Module3 无有效上游 CRC candidate，Module4 输出 0 bytes。
- Wideband/DroneID 仍为 `partial_parse`：厂商 framing、已验证 CRC/header、pilot equalization/interleaving/FEC 尚不完整。Unknown 输出只是结构候选，不赋予未经证明的语义。
- MATLAB Module4 的部分 field/template 指标来自 synthetic harness；当前 C 验证只使用 real-IQ observations，不能把 synthetic metrics 当作 real-IQ 协议正确性的证明。

## ARM

- Ubuntu 20.04 x86_64 使用 `arm-linux-gnueabihf` 交叉编译；目标 Xilinx Zynq ARMv7 hard-float，静态 Release `-O2`。板端 EGLIBC 2.19 与新主机动态 glibc 版本不兼容，因此部署静态二进制。
- Module4 unit test 和五个代表 case 已在板端运行。`ARM_VALIDATION.md` 记录五个 case 的 PC/ARM byte-for-byte 对比结论。当前 workspace 中可用的 PC 归档来自 MinGW；与 ARM 归档比对时，DroneID 013 的多个 CSV 和 DJI Wideband 018 的 `module4_bytes.csv` 存在差异。清理时新建的 Ubuntu PC DroneID 013 smoke 输出与 ARM 对应 CSV 一致。所有原始批次均保留，未重算或覆盖正式结果；审计细节见 `PROJECT_CLEANUP_PLAN.md`。
- 总耗时：DroneID 013 6.011 s；RemoteID 005 38.172 s；Autel Wideband 001 19.825 s；DJI Wideband 018 23.014 s；RemoteID 028 289.135 s。板端 RSS、CPU 时间及完整 case 结论见 [ARM_VALIDATION.md](ARM_VALIDATION.md)。
- GCC 9 仍有已有 sign-conversion warnings；构建成功。AArch64 未验证。

## 尚未移植

- Module1：候选发现/前级处理。
- Module2：分类和 handoff 生成。
- Module5：后续协议/应用层处理。

当前程序要求已有 Module1/2 结果，不代表这些模块已经能够在 ARM 上运行。

## 下一阶段

1. 分析并改善 ARM Module3 的 RemoteID 低 SNR 恢复和主要耗时路径，同时保持输出与验证阈值。
2. 改善 DroneID candidate population/confidence 与 Module3/MATLAB frame-grid 差异。
3. 在独立协议参考可用时完善 Module4 Wideband/DroneID framing、integrity 和字段语义验证。
4. 移植并验证 Module5；之后再规划 Module1/2 的端到端 ARM 移植。
