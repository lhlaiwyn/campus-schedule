#!/usr/bin/env bash
# 用 wrk 复核 keep-alive 长尾问题。
#
# wrk 是多线程事件驱动压测工具，不会像 ab 那样为每条连接开一个线程，
# 因此可以判断「5 秒长尾」是 ab 的测量假象还是服务端的真实问题。
# 输出写入项目根目录的 bench_wrk.log

set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_FILE="${PROJECT_DIR}/bench_wrk.log"
SERVER_LOG="/tmp/campus_server.log"
BASE="http://127.0.0.1:8080"
URL="${BASE}/api/courses"
THREADS=4
CONNS=100
DURATION=10s
ROUNDS=4
cd "${PROJECT_DIR}"

if ! command -v wrk >/dev/null 2>&1; then
    echo "wrk 未安装，请先执行 scripts/install_wrk.sh"
    exit 1
fi

start_server() {
    if [ -n "${SERVER_PID:-}" ]; then
        kill "${SERVER_PID}" 2>/dev/null
        sleep 1
    fi
    # 这一组历史数据是在 cpp-httplib 引擎上测的，固定引擎才能复现；
    # 两个引擎的同机对比见 scripts/bench_engine.sh
    env CAMPUS_HTTP_ENGINE=httplib ./build/campus_server > "${SERVER_LOG}" 2>&1 &
    SERVER_PID=$!
    sleep 3
}

threads_of_server() {
    if [ -n "${SERVER_PID:-}" ] && [ -d "/proc/${SERVER_PID}/task" ]; then
        ls "/proc/${SERVER_PID}/task" | wc -l
    else
        echo 0
    fi
}

run_wrk() {
    local label="$1"
    shift
    local out rps p99 avg errs non2xx
    out=$(wrk "$@" 2>&1)
    rps=$(echo "${out}" | grep -oP 'Requests/sec:\s+\K[0-9.]+' | head -1)
    avg=$(echo "${out}" | awk '/^ *Latency/ {print $2; exit}')
    p99=$(echo "${out}" | awk '/^ *99%/ {print $2}')
    non2xx=$(echo "${out}" | grep -oP 'Non-2xx or 3xx responses:\s+\K\d+' | head -1)
    errs=$(echo "${out}" | grep -oP 'Socket errors:[^\n]*' | head -1)
    printf "  %-26s RPS %-11s 平均 %-9s P99 %-10s 非2xx %-4s %s\n" \
        "${label}" "${rps:-?}" "${avg:-?}" "${p99:-?}" "${non2xx:-0}" "${errs:-无 socket 错误}"
}

run_block() {
    local title="$1"
    shift
    echo "########## ${title} ##########"
    local r
    for r in $(seq 1 "${ROUNDS}"); do
        printf "  [第%s轮前] 服务端线程=%s\n" "$r" "$(threads_of_server)"
        run_wrk "第${r}轮" -t"${THREADS}" -c"${CONNS}" -d"${DURATION}" "$@"
    done
    echo
}

{
    echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "环境: $(nproc) vCPU / $(free -h | awk 'NR==2 {print $2}') 内存"
    echo "参数: ${THREADS} 线程 / ${CONNS} 连接 / 每轮 ${DURATION} / ${ROUNDS} 轮"
    echo "wrk 版本: $(wrk --version 2>&1 | head -1)"
    echo

    if ss -ltn 2>/dev/null | grep -q ':8080 '; then
        fuser -k 8080/tcp >/dev/null 2>&1
        sleep 1
    fi

    echo "=========== W1. keep-alive（wrk 默认就是长连接）==========="
    start_server
    echo "服务 PID=${SERVER_PID}"
    run_block "W1 keep-alive 连续 ${ROUNDS} 轮" "${URL}"

    echo "=========== W2. 关闭 keep-alive 作为对照 ==========="
    run_block "W2 短连接连续 ${ROUNDS} 轮" -H "Connection: close" "${URL}"

    echo "=========== W3. 每轮重启服务 + keep-alive ==========="
    local_r_loop() {
        local r
        for r in $(seq 1 "${ROUNDS}"); do
            start_server
            echo "  （已重启服务）"
            printf "  [第%s轮前] 服务端线程=%s\n" "$r" "$(threads_of_server)"
            run_wrk "第${r}轮" -t"${THREADS}" -c"${CONNS}" -d"${DURATION}" "${URL}"
        done
    }
    echo "########## W3. 每轮重启服务 + keep-alive ##########"
    local_r_loop
    echo

    kill "${SERVER_PID}" 2>/dev/null
    sleep 1
    echo
    echo "--- 服务端日志 ---"
    cat "${SERVER_LOG}"
    echo
    echo "完成"
} 2>&1 | tee "${LOG_FILE}"

echo
echo "日志已写入: ${LOG_FILE}"
