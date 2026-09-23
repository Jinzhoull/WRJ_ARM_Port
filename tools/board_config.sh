#!/usr/bin/env bash

# Non-secret defaults for the ARM development board.
BOARD_HOST="${BOARD_HOST:-analog-board}"
BOARD_ROOT="${BOARD_ROOT:-/root/wrj_arm_test}"

# Expected settings for the validated ARM deployment. build_arm.sh keeps its
# existing Release/-O2/static defaults; run_board_test.sh records these values
# when no CMake cache is available to report the effective build settings.
ARM_BUILD_TYPE="${ARM_BUILD_TYPE:-Release}"
ARM_OPTIMIZATION="${ARM_OPTIMIZATION:--O2}"
ARM_TARGET="${ARM_TARGET:-ARMv7 Linux hard-float}"
ARM_LINK_MODE="${ARM_LINK_MODE:-static}"
