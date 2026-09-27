#!/usr/bin/env bash
# 修好 backlog 之后的最终压测，用于产出写进简历的真实数据
# 输出写入项目根目录的 bench_final.log

set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_FILE="${PROJECT_DIR}/bench_final.log"
SERVER_LOG="/tmp/campus_server.log"
BASE="http://127.0.0.1:8080"
QUERY="${BASE}/api/courses?semester=2026-2027-1"
cd "${PROJECT_DIR}"

run_ab() {
    local label="$1"
    shift
    echo "--- ${label} ---"
    ab "$@" 2>&1 | grep -E \
        'Complete requests|Failed requests|Non-2xx|Requests per second|Time per request|^ *[0-9]+%|^ *\(Connect'
    echo
}

{
    echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "环境: WSL2 Ubuntu 24.04，$(nproc) vCPU / $(free -h | awk 'NR==2 {print $2}') 内存"
    echo "优化项: TCP_NODELAY + 线程池 64 + listen backlog 1024"
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

    echo "########## 1. 50 并发 ##########"
    run_ab "1.1  5000 请求"  -n 5000  -c 50  -k "${QUERY}"

    echo "########## 2. 100 并发 ##########"
    run_ab "2.1  8000 请求"  -n 8000  -c 100 -k "${QUERY}"

    echo "########## 3. 200 并发（连跑两次，验证可复现）##########"
    run_ab "3.1  10000 请求" -n 10000 -c 200 -k "${QUERY}"
    run_ab "3.2  10000 请求（重复）" -n 10000 -c 200 -k "${QUERY}"

    echo "########## 4. 500 并发（压力上限）##########"
    run_ab "4.1  20000 请求" -n 20000 -c 500 -k "${QUERY}"

    echo "########## 5. 对照：关闭 keep-alive ##########"
    run_ab "5.1  5000 请求（无 -k）" -n 5000 -c 50 "${QUERY}"

    echo "########## 服务端日志 ##########"
    cat "${SERVER_LOG}"

    kill ${SERVER_PID} 2>/dev/null
    sleep 1
    echo
    echo "服务已停止"
} 2>&1 | tee "${LOG_FILE}"

echo
echo "日志已写入: ${LOG_FILE}"
