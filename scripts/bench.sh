#!/usr/bin/env bash
# 分层压测：定位瓶颈究竟在 HTTP 层、WSL 网络、连接池，还是 MySQL 本身
# 输出同时写入项目根目录的 bench.log

set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_FILE="${PROJECT_DIR}/bench.log"
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

start_server() {
    if ss -ltn 2>/dev/null | grep -q ':8080 '; then
        fuser -k 8080/tcp >/dev/null 2>&1
        sleep 1
    fi
    # 固定用 cpp-httplib 引擎，保证这份历史数据可复现（对比见 bench_engine.sh）
    env CAMPUS_HTTP_ENGINE=httplib "$@" ./build/campus_server > "${SERVER_LOG}" 2>&1 &
    SERVER_PID=$!
    sleep 2
    grep -E '连接池就绪|服务已启动' "${SERVER_LOG}" || echo "!! 服务启动异常"
}

stop_server() {
    kill ${SERVER_PID} 2>/dev/null
    sleep 1
}

{
    echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "CPU 核心: $(nproc)    内存: $(free -h | awk 'NR==2 {print $2}')"
    echo "ab 版本: $(ab -V 2>/dev/null | head -1)"
    echo
    echo "服务端默认配置：连接池 8 条，httplib 默认线程池"
    echo

    echo "########## A. 纯 HTTP 层（完全不经数据库）##########"
    start_server
    run_ab "A1  /api/version   并发 1    300 请求"  -n 300  -c 1   -k "${BASE}/api/version"
    run_ab "A2  /api/version   并发 50  3000 请求"  -n 3000 -c 50  -k "${BASE}/api/version"
    run_ab "A3  /api/version   并发 200 5000 请求"  -n 5000 -c 200 -k "${BASE}/api/version"

    echo "########## B. 完整数据库查询（纯查询接口）##########"
    run_ab "B1  /api/courses   并发 1    300 请求"  -n 300  -c 1   -k "${QUERY}"
    run_ab "B2  /api/courses   并发 10  1000 请求"  -n 1000 -c 10  -k "${QUERY}"
    run_ab "B3  /api/courses   并发 50  3000 请求"  -n 3000 -c 50  -k "${QUERY}"
    run_ab "B4  /api/courses   并发 200 5000 请求"  -n 5000 -c 200 -k "${QUERY}"

    echo "########## C. 关闭 keep-alive 对比（看连接复用值不值）##########"
    run_ab "C1  /api/courses   并发 50  3000 请求（无 -k）" -n 3000 -c 50 "${QUERY}"
    stop_server

    echo "########## D. 直接压 MySQL，完全绕开后端 ##########"
    if command -v mysqlslap >/dev/null 2>&1; then
        mysqlslap \
            --user=campus --password=campus_dev_2026 --host=127.0.0.1 \
            --concurrency=8 --iterations=5 --number-of-queries=1000 \
            --query="SELECT c.id, c.name, c.code FROM courses c LEFT JOIN course_sessions s ON s.course_id = c.id WHERE c.semester = '2026-2027-1'" \
            2>&1 | grep -viE 'warning'
    else
        echo "mysqlslap 未安装，跳过"
    fi
    echo

    echo "########## E. 对照实验：连接池 8 -> 32 ##########"
    start_server CAMPUS_DB_POOL_SIZE=32
    run_ab "E1  /api/courses   并发 50  3000 请求（pool=32）"  -n 3000 -c 50  -k "${QUERY}"
    run_ab "E2  /api/courses   并发 200 5000 请求（pool=32）"  -n 5000 -c 200 -k "${QUERY}"
    stop_server

    echo "########## 服务端日志（pool=32 那次）##########"
    cat "${SERVER_LOG}"

    echo
    echo "完成"
} 2>&1 | tee "${LOG_FILE}"

echo
echo "日志已写入: ${LOG_FILE}"
