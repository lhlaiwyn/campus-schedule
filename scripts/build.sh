#!/usr/bin/env bash
# 配置、编译并运行单元测试（普通用户运行即可）
# 输出会同时写入项目根目录的 build.log

set -o pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_FILE="${PROJECT_DIR}/build.log"
cd "${PROJECT_DIR}"

{
    echo "项目目录: ${PROJECT_DIR}"
    echo "时间:     $(date '+%Y-%m-%d %H:%M:%S')"
    echo "编译器:   $(g++ --version | head -1)"
    echo

    echo "===== 1/3 配置 CMake ====="
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
    echo

    echo "===== 2/3 编译 ====="
    cmake --build build
    echo

    echo "===== 3/3 运行单元测试 ====="
    ctest --test-dir build --output-on-failure
} 2>&1 | tee "${LOG_FILE}"

STATUS=$?
echo
echo "日志已写入: ${LOG_FILE}"
echo "总退出码: ${STATUS}    （0 表示全部通过）"
exit ${STATUS}

