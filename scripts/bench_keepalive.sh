#!/usr/bin/env bash
# 专门验证 keep-alive 长尾停顿：连续跑 6 轮，中间不冷却。
# 如果 keep-alive 超时是根因，把超时放宽到 30 秒后这些轮次应该都是 0 失败。
# 输出写入项目根目录的 bench_keepalive.log

set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_FILE="${PROJECT_DIR}/bench_keepalive.log"
SERVER_LOG="/tmp/campus_server.log"
BASE="http://127.0.0.1:8080"
QUERY="${BASE}/api/courses"
cd "${PROJECT_DIR}"

{
    echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "环境: $(nproc) vCPU / $(free -h | awk 'NR==2 {print $2}') 内存"
    echo "配置: TCP_NODELAY + 线程池 256 + backlog 1024 + keep-alive 超时 30s"
    echo "方式: 连续 6 轮，每轮 100 并发 5000 请求，中间不冷却（故意制造复用碰撞）"
    echo

    if ss -ltn 2>/dev/null | grep -q ':8080 '; then
        fuser -k 8080/tcp >/dev/null 2>&1
        sleep 1
    fi

    # 固定用 cpp-httplib 引擎，保证这份历史数据可复现（对比见 bench_engine.sh）
    env CAMPUS_HTTP_ENGINE=httplib ./build/campus_server > "${SERVER_LOG}" 2>&1 &
    SERVER_PID=$!
    sleep 3
    echo "服务 PID=${SERVER_PID}"
    echo

    total_failed=0
    for round in 1 2 3 4 5 6; do
        echo "--- 第 ${round} 轮  100 并发 5000 请求 ---"
        output=$(ab -n 5000 -c 100 -k "${QUERY}" 2>&1)
        echo "${output}" | grep -E \
            'Complete requests|Failed requests|Non-2xx|Requests per second|Time per request|^ *[0-9]+%|^ *\(Connect'
        failed=$(echo "${output}" | grep -oP 'Failed requests:\s+\K\d+')
        total_failed=$((total_failed + ${failed:-0}))
        echo
    done

    echo "================================================"
    echo " 六轮失败请求合计: ${total_failed}"
    echo "================================================"
    echo
    echo "--- 服务端日志 ---"
    cat "${SERVER_LOG}"

    kill ${SERVER_PID} 2>/dev/null
    sleep 1
    echo
    echo "服务已停止"
} 2>&1 | tee "${LOG_FILE}"

echo
echo "日志已写入: ${LOG_FILE}"
