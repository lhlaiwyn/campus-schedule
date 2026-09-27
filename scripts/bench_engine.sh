#!/usr/bin/env bash
# 两个 HTTP 引擎的同机对比：自研 epoll 网络库 vs cpp-httplib。
#
# 为什么要做这个对比：业务代码完全一样（api/dispatcher.cpp），
# 唯一变量就是 HTTP 引擎，所以测出来的差异一定是引擎差异。
# 输出写入项目根目录的 bench_engine.log
#
# 两组实验：
#   一、只测框架本身：/api/health，不碰数据库
#   二、真实业务路径：/api/courses 带 token，每次真的查 MySQL（缓存关闭）
#
# 第二组故意扫了自研引擎的 worker 数（1 / 4 / 16）：
# 事件循环里跑阻塞式 MySQL 查询时，「worker 数」就等于「同时在飞的查询数」，
# 它直接决定吞吐上限。用数据把这个因果关系钉住，而不是嘴上解释。
#
# 用法：scripts/bench_engine.sh [all|http|db]   （默认 all；db = 只跑查库那一组）

set -uo pipefail

MODE="${1:-all}"

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# 日志按模式分开存：不然「只跑 db」会把之前「跑 http」的原始数据覆盖掉，
# 报告里的表格就没法对着日志复核了。
LOG_FILE="${PROJECT_DIR}/bench_engine_${MODE}.log"
SUMMARY_FILE="/tmp/campus_bench_engine_summary_${MODE}.txt"
BASE="http://127.0.0.1:8080"
URL_HEALTH="${BASE}/api/health"
URL_COURSES="${BASE}/api/courses?semester=2026-2027-1"
THREADS=4
CONNS=100
DURATION=10s
ROUNDS=3
cd "${PROJECT_DIR}"

if ! command -v wrk >/dev/null 2>&1; then
    echo "wrk 未安装，请先执行 scripts/install_wrk.sh"
    exit 1
fi

token=""
LAST_RPS="?"

# 每个引擎各登录一次：令牌本身与引擎无关，但重登能保证连的就是当前进程
login() {
    local response
    response=$(curl -sS -X POST "${BASE}/api/auth/login" \
        -H 'Content-Type: application/json' \
        -d '{"studentId":"2024001","password":"secret123","adapter":"mock"}')
    token=$(echo "${response}" | jq -r '.token // empty' 2>/dev/null)
}

# 等 8080 真正空出来再启动。上一次压测的进程退出、内核回收监听端口都需要时间，
# 抢跑的话新进程 bind 会失败并直接退出，压测工具却会一直打空气——
# 这是「跑完不知道量的是谁」的经典坑。
ensure_port_free() {
    local i
    for i in $(seq 1 20); do
        if ! ss -ltn 2>/dev/null | grep -q ':8080 '; then
            return 0
        fi
        fuser -k 8080/tcp >/dev/null 2>&1
        sleep 0.5
    done
    echo "!! 8080 端口一直没释放，先确认有没有残留进程"
    return 1
}

# 启动后轮询等就绪；起不来就把服务端日志打出来，别让数据变成谜
wait_ready() {
    local log="$1"
    local i
    for i in $(seq 1 20); do
        if curl -sS --max-time 1 "${BASE}/api/health" >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done
    echo "!! 服务 20 秒内没就绪，服务端日志如下："
    sed 's/^/    /' "${log}"
    return 1
}

# start_server 引擎 worker数 日志 额外环境变量
start_server() {
    local engine="$1"
    local workers="$2"
    local log="$3"
    local extra_env="${4:-}"
    if [ -n "${SERVER_PID:-}" ]; then
        kill "${SERVER_PID}" 2>/dev/null
        sleep 1
    fi
    local attempt
    for attempt in 1 2; do
        ensure_port_free || return 1
        # shellcheck disable=SC2086
        env CAMPUS_HTTP_ENGINE="${engine}" CAMPUS_HTTP_WORKERS="${workers}" ${extra_env} \
            ./build/campus_server > "${log}" 2>&1 &
        SERVER_PID=$!
        if wait_ready "${log}"; then
            return 0
        fi
        echo "    （第 ${attempt} 次启动失败，清理端口后重试）"
        kill "${SERVER_PID}" 2>/dev/null
        sleep 1
    done
    return 1
}

# 自检：确认连上的就是预期引擎。压测最怕「打到上一个没退干净的进程」，
# 那样数据全是假的却看不出来。
verify_engine() {
    local expected="$1"
    local reported
    reported=$(curl -sS "${BASE}/api/version" | jq -r '.httpEngine // "?"')
    if [ "${reported}" != "${expected}" ]; then
        echo "!! 引擎自检失败：期望 ${expected}，实际 ${reported}（可能有旧进程占着 8080）"
        echo "   性能数据在此情况下不可信，已中止。"
        exit 1
    fi
    echo "引擎自检通过: httpEngine=${reported}"
}

run_wrk() {
    local label="$1"
    shift
    local out rps avg p99 non2xx errs
    out=$(wrk "$@" 2>&1)
    rps=$(echo "${out}" | grep -oP 'Requests/sec:\s+\K[0-9.]+' | head -1)
    avg=$(echo "${out}" | awk '/^ *Latency/ {print $2; exit}')
    p99=$(echo "${out}" | awk '/^ *99%/ {print $2}')
    non2xx=$(echo "${out}" | grep -oP 'Non-2xx or 3xx responses:\s+\K\d+' | head -1)
    errs=$(echo "${out}" | grep -oP 'Socket errors:[^\n]*' | head -1)
    LAST_RPS="${rps:--}"
    printf "  %-34s RPS %-11s 平均 %-9s P99 %-8s 非2xx %-4s %s\n" \
        "${label}" "${rps:-?}" "${avg:-?}" "${p99:-?}" "${non2xx:-0}" "${errs:-无 socket 错误}"
}

# run_block 标题 引擎 worker数 url 是否带token 额外环境变量
run_block() {
    local title="$1"
    local engine="$2"
    local workers="$3"
    local url="$4"
    local use_auth="$5"
    local extra_env="${6:-}"
    local log="/tmp/campus_bench_$(echo "${title}${url}" | md5sum | cut -c1-6).log"

    echo "########## ${title} ##########"
    start_server "${engine}" "${workers}" "${log}" "${extra_env}"
    echo "服务 PID=${SERVER_PID}"
    verify_engine "${engine}"
    login
    if [ -z "${token}" ]; then
        echo "!! 登录失败，拿不到 token，跳过这个 block"
        return
    fi

    local rps_list=""
    local r
    for r in $(seq 1 "${ROUNDS}"); do
        if [ -n "${use_auth}" ]; then
            run_wrk "第${r}轮" --latency -t"${THREADS}" -c"${CONNS}" -d"${DURATION}" \
                -H "Authorization: Bearer ${token}" "${url}"
        else
            run_wrk "第${r}轮" --latency -t"${THREADS}" -c"${CONNS}" -d"${DURATION}" "${url}"
        fi
        rps_list="${rps_list} ${LAST_RPS}"
    done
    printf '%-40s |%s\n' "${title}" "${rps_list}" >> "${SUMMARY_FILE}"
    echo "  服务端日志:"
    sed 's/^/    /' "${log}" | head -6
    echo
}

{
    rm -f "${SUMMARY_FILE}"

    echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "环境: $(nproc) vCPU / $(free -h | awk 'NR==2 {print $2}') 内存"
    echo "参数: wrk ${THREADS} 线程 / ${CONNS} 连接 / 每轮 ${DURATION} / 每块 ${ROUNDS} 轮"
    echo "说明: 两个引擎共用同一份业务代码（api/dispatcher.cpp），唯一变量是 HTTP 引擎"
    echo

    echo "本次模式: ${MODE}"
    echo

    if [ "${MODE}" = "all" ] || [ "${MODE}" = "http" ]; then
        echo "=========== 一、只测 HTTP 框架本身（/api/health，不查库）==========="
        echo "目的：把「网络库/事件循环」和「数据库」彻底分开，先看清引擎自己的上限。"
        echo
        run_block "自研 epoll（1 worker）探活" net 1 "${URL_HEALTH}" ""
        run_block "自研 epoll（4 worker）探活" net 4 "${URL_HEALTH}" ""
        run_block "自研 epoll（16 worker）探活" net 16 "${URL_HEALTH}" ""
        run_block "cpp-httplib（线程池）探活" httplib "" "${URL_HEALTH}" ""
    fi

    if [ "${MODE}" = "all" ] || [ "${MODE}" = "db" ]; then
        echo "=========== 二、真实业务路径（/api/courses 带 token，每次查库、缓存关闭）==========="
        echo "目的：事件循环里跑的是阻塞式 MySQL 查询，此时「worker 数」=「同时在飞的查询数」，"
        echo "      它才是吞吐上限。所以这里扫 1 / 4 / 16 个 worker，再看连接池放大的效果。"
        echo
        run_block "自研 epoll（1 worker）查课表" net 1 "${URL_COURSES}" "auth"
        run_block "自研 epoll（4 worker）查课表" net 4 "${URL_COURSES}" "auth"
        run_block "自研 epoll（16 worker）查课表" net 16 "${URL_COURSES}" "auth"
        run_block "自研 epoll（16 worker + 16 连接池）查课表" net 16 "${URL_COURSES}" "auth" \
            "CAMPUS_DB_POOL_SIZE=16"
        run_block "cpp-httplib（线程池）查课表" httplib "" "${URL_COURSES}" "auth"
    fi

    echo "=========== 汇总（每块 3 轮的 RPS）==========="
    cat "${SUMMARY_FILE}"
    echo
    echo "说明：对比结论必须同时看 RPS、P99、非 2xx 和 socket 错误——"
    echo "      只看 RPS 很容易被「失败请求也算进去」骗到。"
    echo
    kill "${SERVER_PID}" 2>/dev/null
    sleep 1
    echo "服务已停止"
} 2>&1 | tee "${LOG_FILE}"

echo
echo "日志已写入: ${LOG_FILE}"
