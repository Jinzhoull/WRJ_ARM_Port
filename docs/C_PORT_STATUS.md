# E34 C99 Host 正式状态

FAST 28/53；DEEP 25；MOVED 15；NEW 0；Final 43/53；strict 40/53；SemanticComplete 43/43；CRC/PDU 89；fields 1273；physical 86/462；RemoteID core 7/7、四消息 6/7；DroneID 4/6。

SUCCESS-RATE V2：fresh MATLAB53 后重新导出 comparator-only Golden，fresh C Host53 串行实际 IQ 接收，bytes/原 CRC/fields/route/gate parity 53/53。CTest5/5；GCC15.2.0/CMake3.31.10 MinGW。

C wall 1154.894s、Recovery 787.524s；相对 V1 wall 改善 1.31%。源码 fingerprint `ed462b2865a6724cbaf2de55cb350b8a374411e15b6e4cc6986e6c036f088368`。

DATA Chase 只用实际 demapper 裕量，保留 header/CRC suffix；窗口扩展只按收到合法 header 在同候选连续 IQ 内重解调。三 channel 模型与原 scheduler 预算保留。Receiver 不读 Golden/truth/expected；无候选特判、CRC rewrite 或跨候选证据。

旧 Standalone 6715/6715 文件及文件集合未变，IQ53/53 SHA 一致。M1/M2、Git、ARM=0；未验证板端 RAM、数值或性能。完整报告见[HOST_OPTIMIZATION_REPORT.md](../../docs/HOST_OPTIMIZATION_REPORT.md)。
