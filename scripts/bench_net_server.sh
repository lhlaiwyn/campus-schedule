#!/usr/bin/env bash
# 自研网络库的性能对比，分两个阶段：
#   阶段 A —— 纯 I/O 负载：看单线程是否已经是瓶颈
#   阶段 B —— CPU 密集负载：看多 worker 是否真的用上了多核
# 输出写入项目根目录的 bench_net_server.log

set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_FILE="${PROJECT_DIR}/bench_net_server.log"
URL="http://127.0.0.1:8081/bench?payload=abcdefghij"
WRK_THREADS=4
CONNS=200
DURATION=10s
# 每次请求做多少轮哈希。太小看不出差别，太大会让单线程慢到没必要。
CPU_WORK=2000
cd "${PROJECT_DIR}"

run_server() {
    # $1 = worker 线程数，$2 = 每次请求的 CPU 工作量
    # $3（可选）= "et" 启用边缘触发
    #
    # 每次启动前都先清端口：如果上一个进程没退干净，新进程 bind 会失败直接退出，
    # 而 wrk 仍然打在旧服务器上——测出来的数字就完全不可信（这个坑我们踩过一次）。
    if ss -ltn 2>/dev/null | grep -q ':8081 '; then
        fuser -k 8081/tcp >/dev/null 2>&1
        sleep 1
    fi

    ./build/net_server "$@" > /tmp/net_server.log 2>&1 &
    NET_PID=$!
    sleep 1

    # 打印服务端自己的启动行，证明这一轮压的到底是哪个配置
    echo "  [服务端] $(head -1 /tmp/net_server.log)"
}

stop_server() {
    kill "${NET_PID}" 2>/dev/null
    wait "${NET_PID}" 2>/dev/null
    sleep 1
}

bench() {
    local label="$1"
    local out rps p99 non2xx errs
    out=$(wrk -t"${WRK_THREADS}" -c"${CONNS}" -d"${DURATION}" "${URL}" 2>&1)
    rps=$(echo "${out}" | grep -oP 'Requests/sec:\s+\K[0-9.]+' | head -1)
    p99=$(echo "${out}" | awk '/^ *99%/ {print $2}')
    # 把非 2xx 和 socket 错误也打出来：RPS 再高，如果是错误响应就没意义
    non2xx=$(echo "${out}" | grep -oP 'Non-2xx or 3xx responses:\s+\K\d+' | head -1)
    errs=$(echo "${out}" | grep -oP 'Socket errors:[^\n]*' | head -1)
    printf "  %-12s RPS %-12s P99 %-9s 非2xx %-4s %s\n" \
        "${label}" "${rps:-?}" "${p99:-?}" "${non2xx:-0}" "${errs:-无 socket 错误}"
}

{
    echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "环境: $(nproc) vCPU"
    echo "压测: ${WRK_THREADS} 个 wrk 线程 / ${CONNS} 连接 / ${DURATION}"
    echo

    if ! command -v wrk >/dev/null 2>&1; then
        echo "wrk 未安装，先执行 scripts/install_wrk.sh"
        exit 1
    fi

    if ss -ltn 2>/dev/null | grep -q ':8081 '; then
        fuser -k 8081/tcp >/dev/null 2>&1
        sleep 1
    fi

    echo "========== 阶段 A：纯 I/O 负载（不额外消耗 CPU）=========="
    for workers in 1 4; do
        run_server "${workers}" 0
        bench "${workers} worker"
        stop_server
    done
    echo "  结论：请求太轻时瓶颈不在服务器 CPU，加线程只增加开销"
    echo

    echo "========== 阶段 B：CPU 密集负载（每次请求 ${CPU_WORK} 轮哈希）=========="
    for workers in 1 2 4; do
        run_server "${workers}" "${CPU_WORK}"
        bench "${workers} workers"
        stop_server
    done
    echo "  结论：负载变重后，多 worker 才体现出多核价值"
    echo

    echo "========== 阶段 C：水平触发（LT）vs 边缘触发（ET），4 worker 纯 I/O =========="
    run_server 4 0
    bench "LT"
    stop_server
    run_server 4 0 et
    bench "ET"
    stop_server
} 2>&1 | tee "${LOG_FILE}"

echo
echo "日志已写入: ${LOG_FILE}"
