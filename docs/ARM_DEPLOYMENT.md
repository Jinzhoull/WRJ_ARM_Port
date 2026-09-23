# ARMv7 Deployment Environment

## Target

- Board: Xilinx Zynq, dual-core ARMv7 (`armv7l`)
- ABI: ARM EABI5 hard-float, VFP register arguments; VFPv3/NEON available
- OS: Linaro 14.04, Linux 4.14
- RAM: approximately 497 MiB, no swap
- SSH host alias: `analog-board`
- Board root: `/root/wrj_arm_test`

默认主机和板端目录定义在 `tools/board_config.sh`，可通过 `BOARD_HOST`、`BOARD_ROOT` 环境变量覆盖。配置文件不含 SSH key、登录密码或 sudo 密码。

## Build and compatibility

Ubuntu host uses `arm-linux-gnueabihf-gcc` and `cmake/armv7-linux-gnueabihf.cmake`。当前经板端验证的程序为 ARMv7 hard-float、static、Release `-O2`，构建目录是 `build-arm/`。

板端 EGLIBC 为 2.19。Ubuntu 动态链接版本要求 `GLIBC_2.27`/`GLIBC_2.29`，与目标板不兼容，因此当前部署使用静态链接。不要改为 AArch64，也不要在未重新验证兼容性的情况下更改 ABI 或编译参数。

## Deploy / run flow

```text
Ubuntu tools/build_arm.sh
        ↓
tools/deploy_board.sh  →  /root/wrj_arm_test/bin/ + data/
        ↓
tools/run_board_test.sh → board results/ → local results/arm/
```

统一命令、单个 case 的上传/运行示例和结果位置见 [BUILD_AND_RUN.md](BUILD_AND_RUN.md)。运行脚本不会覆盖同名板端输出：默认输出名带时间戳，成功后复制 CSV 到本地 `results/arm/`，运行日志放在 `results/arm/logs/`。

实机运行时间、RSS、ARM/PC 输出一致性及已知功能限制见 [ARM_VALIDATION.md](ARM_VALIDATION.md)。
