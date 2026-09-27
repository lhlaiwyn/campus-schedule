#!/usr/bin/env bash
# 优化后的复测：验证 TCP_NODELAY 是否解决了 keep-alive 下的高延迟问题
# 输出写入项目根目录的 bench_after.log

set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_FILE="${PROJECT_DIR}/bench_after.log"
SERVER_LOG="/tmp/campus_server.log"
BASE="http://127.0.0.1:8080"
QUERY="${BASE}/api/courses?semester=2026-2027-1"
cd "${PROJECT_DIR}"

run_ab() {
    local label="$1"
    shift
    echo "--- ${label} ---"
    ab "$@" 2>&1 | grep -E \
        'Complete requests|Failed requests|Non-2xx|Requests per second|Time per request|^ *[0-9]+%'
    echo
}

{
    echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "CPU 核心: $(nproc)    内存: $(free -h | awk 'NR==2 {print $2}')"
    echo "本次已开启 TCP_NODELAY + 线程池 64"
    echo

    if ss -ltn 2>/dev/null | grep -q ':8080 '; then
        fuser -k 8080/tcp >/dev/null 2>&1
        sleep 1
    fi

    # 固定用 cpp-httplib 引擎，保证这份历史数据可复现（对比见 bench_engine.sh）
    env CAMPUS_HTTP_ENGINE=httplib ./build/campus_server > "${SERVER_LOG}" 2>&1 &
    SERVER_PID=$!
    sleep 2
    echo "服务 PID=${SERVER_PID}"
    echo

    echo "########## 1. keep-alive，单并发（看单请求真实延迟）##########"
    run_ab "1.1 /api/courses  并发 1    300 请求" -n 300 -c 1 -k "${QUERY}"

    echo "########## 2. keep-alive，50 并发（优化前：179 QPS / P50 44ms）##########"
    run_ab "2.1 /api/version  并发 50  3000 请求"  -n 3000 -c 50 -k "${BASE}/api/version"
    run_ab "2.2 /api/courses  并发 50  3000 请求"  -n 3000 -c 50 -k "${QUERY}"
    run_ab "2.3 /api/courses  并发 50 10000 请求"  -n 10000 -c 50 -k "${QUERY}"

    echo "########## 3. keep-alive，200 并发（优化前：89 QPS / P99 26s）##########"
    run_ab "3.1 /api/courses  并发 200 5000 请求"  -n 5000 -c 200 -k "${QUERY}"
    run_ab "3.2 /api/courses  并发 200 20000 请求" -n 20000 -c 200 -k "${QUERY}"

    echo "########## 4. keep-alive，500 并发（压力上限）##########"
    run_ab "4.1 /api/courses  并发 500 20000 请求" -n 20000 -c 500 -k "${QUERY}"

    echo "########## 5. 关闭 keep-alive 作为对照 ##########"
    run_ab "5.1 /api/courses  并发 50  3000 请求（无 -k）" -n 3000 -c 50 "${QUERY}"

    echo "########## 服务端日志 ##########"
    cat "${SERVER_LOG}"

    kill ${SERVER_PID} 2>/dev/null
    sleep 1
    echo
    echo "服务已停止"
} 2>&1 | tee "${LOG_FILE}"

echo
echo "日志已写入: ${LOG_FILE}"
