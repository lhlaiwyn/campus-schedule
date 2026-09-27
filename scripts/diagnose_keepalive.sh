#!/usr/bin/env bash
# 定位 keep-alive 下的 5 秒长尾：把可能的原因一个个切开
#   A 基线（复现问题） / B 不碰数据库 / C 每轮重启服务 / D 关闭 keep-alive
# 每轮前后都打印服务端线程数与内存，线程膨胀与否是最关键的证据。
# 输出写入项目根目录的 diagnose.log

set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_FILE="${PROJECT_DIR}/diagnose.log"
SERVER_LOG="/tmp/campus_server.log"
BASE="http://127.0.0.1:8080"
ROUNDS=4
N=3000
C=100
cd "${PROJECT_DIR}"

start_server() {
    if [ -n "${SERVER_PID:-}" ]; then
        kill "${SERVER_PID}" 2>/dev/null
        sleep 1
    fi
    # 固定用 cpp-httplib 引擎，保证这份历史数据可复现（对比见 bench_engine.sh）
    env CAMPUS_HTTP_ENGINE=httplib ./build/campus_server > "${SERVER_LOG}" 2>&1 &
    SERVER_PID=$!
    sleep 3
}

stop_server() {
    if [ -n "${SERVER_PID:-}" ]; then
        kill "${SERVER_PID}" 2>/dev/null
        sleep 1
    fi
}

# 服务端关键状态：线程数（线程-每连接模型下最关键的指标）与内存
probe() {
    local tag="$1"
    local threads=0
    if [ -n "${SERVER_PID:-}" ] && [ -d "/proc/${SERVER_PID}/task" ]; then
        threads=$(ls "/proc/${SERVER_PID}/task" | wc -l)
    fi
    local mem_used mem_avail
    mem_used=$(free -m | awk 'NR==2 {print $3}')
    mem_avail=$(free -m | awk 'NR==2 {print $7}')
    printf "    [%s] 服务端线程=%-5s  WSL内存 已用 %s MB / 可用 %s MB\n" \
        "$tag" "$threads" "$mem_used" "$mem_avail"
}

summarize() {
    local label="$1"
    shift
    local out qps failed p99
    out=$(ab "$@" 2>&1)
    qps=$(echo "$out" | grep -oP 'Requests per second:\s+\K[0-9.]+' | head -1)
    failed=$(echo "$out" | grep -oP 'Failed requests:\s+\K\d+' | head -1)
    p99=$(echo "$out" | awk '/^ *99%/ {print $2}')
    printf "  %-30s QPS %-9s 失败 %-5s P99 %-8s\n" "$label" "${qps:-?}" "${failed:-?}" "${p99:-?}"
}

run_block() {
    local title="$1" url="$2" keep_alive="$3" restart_each="$4"
    echo "########## ${title} ##########"
    local r
    for r in $(seq 1 "${ROUNDS}"); do
        if [ "${restart_each}" = "yes" ]; then
            start_server
            echo "  （本轮已重启服务）"
        fi
        probe "第${r}轮开始前"
        if [ "${keep_alive}" = "yes" ]; then
            summarize "第${r}轮" -n "${N}" -c "${C}" -k "${url}"
        else
            summarize "第${r}轮（无 keep-alive）" -n "${N}" -c "${C}" "${url}"
        fi
        probe "第${r}轮结束后"
        echo
    done
}

{
    echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "环境: $(nproc) vCPU / $(free -h | awk 'NR==2 {print $2}') 内存"
    echo "参数: 每轮 ${N} 请求 / ${C} 并发 / ${ROUNDS} 轮"
    echo

    if ss -ltn 2>/dev/null | grep -q ':8080 '; then
        fuser -k 8080/tcp >/dev/null 2>&1
        sleep 1
    fi

    echo "=========== A. 基线：复现问题（/api/courses，keep-alive）==========="
    start_server
    echo "服务 PID=${SERVER_PID}"
    echo
    run_block "A. 基线 /api/courses（keep-alive）" "${BASE}/api/courses" yes no

    echo "=========== B. 排除数据库（/api/version，keep-alive）==========="
    run_block "B. 不碰数据库 /api/version" "${BASE}/api/version" yes no

    echo "=========== C. 每轮重启服务（/api/courses，keep-alive）==========="
    run_block "C. 每轮重启 /api/courses" "${BASE}/api/courses" yes yes

    echo "=========== D. 关闭 keep-alive（/api/courses）==========="
    start_server
    run_block "D. 无 keep-alive /api/courses" "${BASE}/api/courses" no no

    stop_server
    echo
    echo "完成"
} 2>&1 | tee "${LOG_FILE}"

echo
echo "日志已写入: ${LOG_FILE}"
