# ARM Phase7 入口

当前已通过 C99 Host 对 E34 MATLAB 的正式53候选验收；本轮未进行ARM交叉构建、部署或板端执行。历史板端产物已清理，不能把历史验证当作当前 E34 接收链的ARM验收。

后续在有 ARMv7 hard-float 工具链的 Linux 主机运行 `tools/build_arm.sh`，使用 `cmake/armv7-linux-gnueabihf.cmake`，正式接收入口为 `wrj_c99_host`。先验证ABI/运行库、workspace内存、FFT/CFO精度与原CRC/字段一致性，再执行53候选数值/性能验收；不在板端读取 comparator-only Golden。

既有 board_config/deploy/run 脚本属于下一阶段需复核的部署工具；开始部署前需确认板端环境、目标二进制和运行参数。本轮未使用这些脚本，未宣称ARM实时性能。
