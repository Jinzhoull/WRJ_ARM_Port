# Architecture

当前工程实现的是 MATLAB 无线协议系统中 Module3 与 Module4 的 C99 子链路。Module1/2 尚未移植；它们提供的 handoff 元数据和 candidate IQ 是当前输入。

```text
现有 Module1/2 结果（只读）
          │
          ├── module12_to_module3_handoff.csv
          └── candidateIqArtifact → CF32 IQ
                         │
                         ▼
                  Module3 C99
       预处理 → 频偏/中心补偿 → profile同步
       → aligned frames / diagnostics
                         │
                         ▼
                  Module4 C99
           real-IQ → byte recovery
       → CRC/packet evidence → supported fields
                         │
                         ▼
           CSV results (Module3 + Module4)
```

Module3 与 Module4 共用有界 workspace。默认容量为 917,504 个复数样本，workspace 约 29.06 MiB；主要 IQ 和处理缓冲区为 float32，FFT 使用独立的 radix-2 实现。Module4 在 Module3 完成后借用复用缓冲区，不进行逐帧堆分配。

`m3_result_t` 提供 CFO、profile、frame、DroneID、SFO、BLE 和置信度诊断；`m4_result_t` 区分 `REAL_IQ` 字节来源、CRC/packet 状态、字段和解析完整性。接口字段和数据格式说明以 `include/` 中的定义为准。

后续完整移植目标为：

```text
Module1 → Module2 → Module3 → Module4 → Module5
                 全链路在 ARM Linux 运行
```

该目标不是当前已实现能力。当前 ARM 只验证了 Module3/4 子链路，不能据此宣称 Module1/2/5 已可在 ARM 运行。
