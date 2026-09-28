# Module3 C/MATLAB CFO 对齐修复报告

## 范围与口径

使用同一批 11 个已导出的候选 CF32、正式 MATLAB `module3_sync_summary.csv` 和 Windows Release 构建。下表均为 C 减 MATLAB，单位 Hz。Total CFO 对比 MATLAB 的 `totalBasebandCorrectionHz`；`residualCfoHz` 在非 BLE 路径是补偿后的诊断残差，不应再加到 Total CFO。旧数据保存在 `build-pc/windows/cfo-diagnosis/cfo_diagnosis.csv`，新数据在同目录的 `new_cfo_diagnosis.csv`。

## 原因与修改

1. `src/module3/cfo.c` 的 `m3_boxcar_same()` 在首个输出位置只加入窗口末端一个值，漏掉前半窗；随后每个平滑值都少了同一段数据。这与 MATLAB `conv(...,'same')` 的零填充窗口不一致，改变了低信噪比候选的占用带边缘和谱中心。现已初始化完整首窗，其余谱中心公式、FFT 长度、采样率、整数 CFO 搜索及门限未变。DroneID013 的粗 CFO 差值因此从 +50,041.85 降至 −675.82 Hz。
2. `src/module3/module3.c` 将 BLE 接收机选出的残余补偿同时写进分数 CFO 和残余 CFO。现把它只记在残余字段，并在 Total CFO 中计入一次；实际 IQ 补偿、候选范围和选择规则不变。RemoteID028 的分数 CFO 字段由 −250 kHz 改为 0，但 Total CFO 没有因此改善。

尝试过把 Module1/2 的粗频偏提示加入现有 BLE ±250 kHz 搜索区间；RemoteID028 仍为 0 个 CRC 有效 PDU，故撤销该尝试。另试过按已选帧重新估计 Control004 的 CP 相位；Total CFO 差值扩大至约 +7.64 kHz，故撤销。两项试验均未进入最终源码。

## 五个重点候选：修改前 → 修改后差值

| 候选 | 粗 CFO | 分数 CFO | 残余 CFO | Total CFO |
| --- | ---: | ---: | ---: | ---: |
| DroneID013 | +50,041.85 → −675.82 | +9,978.03 → +634.92 | +2,272.17 → +2,272.17 | +60,019.88 → −40.90 |
| DroneID022 | +768.56 → +781.94 | −615.22 → −628.60 | −1,725.87 → −1,725.87 | +153.34 → +153.34 |
| RemoteID005 | +693.06 → +693.06 | 0 → 0 | 0 → 0 | +693.06 → +693.06 |
| RemoteID028 | −447.69 → −446.44 | −249,999.98 → 0（字段口径） | −90,946.46 → −90,946.46 | −91,394.15 → −91,392.90 |
| DJI Control004 | 0 → 0 | +3,769.55 → +3,769.55 | −7,756.05 → −7,756.05 | +3,769.56 → +3,769.56 |

这五例的整数 CFO 偏移均为 0，未修改整数消歧。DroneID022 的粗/分数差值互相抵消；RemoteID005 的 693.06 Hz 粗估计差值没有安全、已验证的定义修正。Control004 是盲 CP 路径的精估计差异，当前试验未证明可安全消除。RemoteID028 的根本差异仍是接收证据：C 为 0 个 CRC24 有效 PDU，MATLAB 为 5 个，因此两端选中的 BLE 残余 CFO 不同；不能只重写输出数字。

## 11 例回归与 Module4

- Windows Release 构建成功；CTest 1/1 通过。11/11 候选均成功执行 Module3→Module4；Module3 验证仍为 4 PASS、7 WARN、0 FAIL。
- 四个 Wideband case 的六份 Module3/4 正式 CSV 均与修改前 SHA-256 一致。它们相对 MATLAB 的 Total CFO 差值仍为 +0.896、+1.091、−3.958、−1.183 Hz。
- Module4 的 11 个解析状态、完成标志及 CRC 有效包数均不变。44 份 Module4 CSV 中 35 份逐字节一致；Autel Control009、DroneID013、DroneID022 的 9 份 CSV 因补偿后 IQ/帧选择变化而改变。三个候选仍为 `partial_parse`，字段数仍为 2；DroneID013 帧/包数由 9 增至 10。不能称 Module4 输出完全一致。
- RemoteID005 六份 Module3/4 CSV 与修改前逐字节一致：4 个 CRC24 有效 PDU，UAS ID `UAS000185`，纬度 `31.22602430`，经度 `121.46824690`，高度 131.000 m，速度 2.250 m/s，航向 117.000°，仍为完整解析。
- RemoteID028 仍为 `crc_failed`；本轮没有通过改字段伪造解析成功。

原 11 例结果已保留在 `build-pc/windows/full-correctness-validation-before-cfo-fix/`，最新结果在 `build-pc/windows/full-correctness-validation/`。本轮只修复了已证实的谱平滑实现偏差和 BLE 字段重复归属；其余三类未对齐项需要后续独立验证，不能宣称已全部修复。
