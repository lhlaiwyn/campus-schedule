#!/usr/bin/env bash
# 冒烟测试：单元测试 -> 鉴权 -> 业务接口 -> HTTP 集成 -> 限流 -> 短压测
# 输出同时写入项目根目录的 smoke_test.log

set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_FILE="${PROJECT_DIR}/smoke_test.log"
SERVER_LOG="/tmp/campus_server.log"
MOCK_PORTAL_LOG="/tmp/mock_portal.log"
BASE="http://127.0.0.1:8080"
PORTAL="http://127.0.0.1:9090"
SEMESTER="2026-2027-1"
HTTP_SEMESTER="2026-2027-3"
cd "${PROJECT_DIR}"

ok() { echo "  [OK]   $1"; }
bad() { echo "  [失败] $1"; }

# 断言计数器：脚本最后给出明确的通过/失败数，不需要人工看日志找问题
PASSED=0
FAILED=0

check_status() {
    local label="$1"
    local expected="$2"
    shift 2
    local actual
    actual=$(curl -sS -o /dev/null -w '%{http_code}' "$@")
    if [ "${actual}" = "${expected}" ]; then
        ok "${label}（HTTP ${actual}）"
        PASSED=$((PASSED + 1))
    else
        bad "${label}：期望 HTTP ${expected}，实际 ${actual}"
        FAILED=$((FAILED + 1))
    fi
}

{
    echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "环境: $(nproc) vCPU / $(free -h | awk 'NR==2 {print $2}') 内存"
    echo

    echo "===== 1/6 单元测试详情 ====="
    ./build/campus_tests --gtest_color=no
    echo

    echo "===== 2/6 启动服务 ====="
    if ss -ltn 2>/dev/null | grep -q ':8080 '; then
        fuser -k 8080/tcp >/dev/null 2>&1
        sleep 1
    fi
    # 冒烟测试里显式开启 Redis 缓存；默认配置是关闭的，保证没装 Redis 也能跑。
    # HTTP 引擎默认是自研的 epoll 网络库（CAMPUS_HTTP_ENGINE=net），
    # 显式写出来是为了让这一份日志能说明「测的到底是哪个引擎」。
    CAMPUS_CACHE_ENABLED=true CAMPUS_HTTP_ENGINE=net \
        ./build/campus_server > "${SERVER_LOG}" 2>&1 &
    SERVER_PID=$!
    sleep 2
    if ! kill -0 ${SERVER_PID} 2>/dev/null; then
        echo "!! 服务启动失败，日志如下"
        cat "${SERVER_LOG}"
        exit 1
    fi
    echo "服务已启动，PID=${SERVER_PID}"
    echo "引擎自检: $(curl -sS "${BASE}/api/version" | jq -c '{httpEngine,version}')"
    echo

    echo "===== 3/6 公开接口 ====="
    echo "--- GET /api/health ---"
    curl -sS "${BASE}/api/health"; echo
    echo "--- GET /api/version ---"
    curl -sS "${BASE}/api/version"; echo
    echo

    echo "===== 4/6 鉴权 ====="
    echo "--- 不带 token 访问受保护接口（应 401）---"
    curl -sS -w '\nHTTP %{http_code}\n' "${BASE}/api/courses"

    echo "--- 伪造 token（应 401）---"
    curl -sS -w '\nHTTP %{http_code}\n' \
        -H 'Authorization: Bearer forged.token.value' "${BASE}/api/courses"

    echo "--- Authorization 格式不对（应 401）---"
    curl -sS -w '\nHTTP %{http_code}\n' \
        -H 'Authorization: token-without-bearer' "${BASE}/api/courses"

    echo "--- 登录：密码错误（应 400，并透出教务系统的原因）---"
    curl -sS -w '\nHTTP %{http_code}\n' -X POST "${BASE}/api/auth/login" \
        -H 'Content-Type: application/json' \
        -d '{"studentId":"2024001","password":"wrong-password","adapter":"mock"}'

    echo "--- 登录：成功 ---"
    login_response=$(curl -sS -X POST "${BASE}/api/auth/login" \
        -H 'Content-Type: application/json' \
        -d '{"studentId":"2024001","password":"secret123","adapter":"mock"}')
    echo "${login_response}"
    TOKEN=$(echo "${login_response}" | jq -r '.token // empty' 2>/dev/null)
    if [ -n "${TOKEN}" ]; then
        ok "拿到 JWT，长度 ${#TOKEN}"
    else
        bad "登录没有返回 token，后面的用例会全部失败"
    fi
    AUTH="Authorization: Bearer ${TOKEN}"

    echo "--- 带 token 访问（应 200）---"
    curl -sS -o /dev/null -w 'HTTP %{http_code}\n' -H "${AUTH}" "${BASE}/api/courses"
    echo

    echo "===== 5/6 业务接口（带 token）====="
    echo "--- 清理上一次运行留下的数据 ---"
    for course_id in $(curl -sS -H "${AUTH}" "${BASE}/api/courses" | jq -r '.items[].id'); do
        curl -sS -o /dev/null -X DELETE -H "${AUTH}" "${BASE}/api/courses/${course_id}"
    done
    echo "已清理"

    echo
    echo "--- POST /api/courses 新增一门课 ---"
    created_response=$(curl -sS -X POST "${BASE}/api/courses" \
        -H "${AUTH}" -H 'Content-Type: application/json' \
        -d '{
              "name": "操作系统", "code": "CS2001", "teacher": "张老师",
              "credits": 3.5, "semester": "2026-2027-1",
              "sessions": [
                {"dayOfWeek": 2, "startPeriod": 3, "endPeriod": 4,
                 "weeks": {"from": 1, "to": 16, "parity": 0},
                 "location": "教学楼A301"}
              ]
            }')
    echo "${created_response}"
    created_id=$(echo "${created_response}" | jq -r '.id // empty' 2>/dev/null)
    echo "（新建课程 id = ${created_id:-解析失败}）"

    echo
    echo "--- POST 再新增一门（数据结构，单周）---"
    curl -sS -X POST "${BASE}/api/courses" \
        -H "${AUTH}" -H 'Content-Type: application/json' \
        -d '{
              "name": "数据结构", "code": "CS2002", "teacher": "李老师",
              "credits": 4.0, "semester": "2026-2027-1",
              "sessions": [
                {"dayOfWeek": 4, "startPeriod": 1, "endPeriod": 2,
                 "weeks": {"from": 1, "to": 16, "parity": 1},
                 "location": "教学楼B102"}
              ]
            }'
    echo

    echo
    echo "--- 重复课程号（唯一键冲突，应 409）---"
    curl -sS -w '\nHTTP %{http_code}\n' -X POST "${BASE}/api/courses" \
        -H "${AUTH}" -H 'Content-Type: application/json' \
        -d '{"name":"另一门课","code":"CS2001","semester":"2026-2027-1"}'

    echo "--- GET /api/courses?semester=${SEMESTER} ---"
    curl -sS -H "${AUTH}" "${BASE}/api/courses?semester=${SEMESTER}" | head -c 800; echo

    echo
    echo "--- GET /api/courses/${created_id}（用新建的 id，不写死 1）---"
    curl -sS -H "${AUTH}" "${BASE}/api/courses/${created_id}"; echo

    echo
    echo "--- 参数校验：空课程名（应 400）---"
    curl -sS -o /dev/null -w 'HTTP %{http_code}\n' -X POST "${BASE}/api/courses" \
        -H "${AUTH}" -H 'Content-Type: application/json' -d '{"name":"","semester":""}'

    echo "--- 时间冲突：同一天同一节（应 400，并给出具体冲突位置）---"
    curl -sS -w '\nHTTP %{http_code}\n' -X POST "${BASE}/api/courses" \
        -H "${AUTH}" -H 'Content-Type: application/json' \
        -d '{
              "name": "冲突测试", "code": "CS9999", "semester": "2026-2027-1",
              "sessions": [
                {"dayOfWeek": 1, "startPeriod": 1, "endPeriod": 2, "weeks": {"from":1,"to":16,"parity":0}},
                {"dayOfWeek": 1, "startPeriod": 2, "endPeriod": 3, "weeks": {"from":1,"to":16,"parity":0}}
              ]
            }'

    echo "--- 查询不存在的课程（应 404）---"
    curl -sS -o /dev/null -w 'HTTP %{http_code}\n' -H "${AUTH}" "${BASE}/api/courses/999999"

    echo "--- 访问不存在的接口（应 404）---"
    curl -sS "${BASE}/api/nope"; echo

    echo
    echo "--- 缓存效果验证 ---"
    cache_before=$(curl -sS "${BASE}/api/health" | jq -c '{cacheBackend,cacheHits,cacheMisses,cacheHitRate}')
    echo "  读取前: ${cache_before}"
    hits_before=$(curl -sS "${BASE}/api/health" | jq -r '.cacheHits')

    for _ in 1 2 3; do
        curl -sS -o /dev/null -H "${AUTH}" "${BASE}/api/courses?semester=${SEMESTER}"
    done

    cache_after=$(curl -sS "${BASE}/api/health" | jq -c '{cacheBackend,cacheHits,cacheMisses,cacheHitRate,cacheInvalidations}')
    echo "  三次读取后: ${cache_after}"
    hits_after=$(curl -sS "${BASE}/api/health" | jq -r '.cacheHits')

    if [ "${hits_after}" -gt "${hits_before}" ]; then
        ok "缓存生效：命中数 ${hits_before} -> ${hits_after}"
        PASSED=$((PASSED + 1))
    else
        bad "缓存没有命中：命中数一直停在 ${hits_before}"
        FAILED=$((FAILED + 1))
    fi

    echo "--- 写操作之后缓存应失效，下一次读会重新查库 ---"
    invalidations_before=$(curl -sS "${BASE}/api/health" | jq -r '.cacheInvalidations')
    curl -sS -o /dev/null -X POST "${BASE}/api/courses" \
        -H "${AUTH}" -H 'Content-Type: application/json' \
        -d '{"name":"编译原理","code":"CS2005","semester":"2026-2027-1"}'
    invalidations_after=$(curl -sS "${BASE}/api/health" | jq -r '.cacheInvalidations')
    if [ "${invalidations_after}" -gt "${invalidations_before}" ]; then
        ok "写操作让缓存失效：失效次数 ${invalidations_before} -> ${invalidations_after}"
        PASSED=$((PASSED + 1))
    else
        bad "写操作没有让缓存失效"
        FAILED=$((FAILED + 1))
    fi

    echo
    echo "--- POST /api/sync（mock 适配器，本地已有 CS2001/CS2002，预期更新 2 + 新增 1）---"
    curl -sS -X POST "${BASE}/api/sync" \
        -H "${AUTH}" -H 'Content-Type: application/json' \
        -d "{\"semester\":\"${SEMESTER}\",\"adapter\":\"mock\"}"
    echo

    echo
    echo "--- POST /api/sync 再同步一次（课表没变，预期变动 0）---"
    curl -sS -X POST "${BASE}/api/sync" \
        -H "${AUTH}" -H 'Content-Type: application/json' \
        -d "{\"semester\":\"${SEMESTER}\",\"adapter\":\"mock\"}"
    echo

    echo
    echo "--- 启动模拟教务系统（独立进程）---"
    if ss -ltn 2>/dev/null | grep -q ':9090 '; then
        fuser -k 9090/tcp >/dev/null 2>&1
        sleep 1
    fi
    ./build/mock_portal > "${MOCK_PORTAL_LOG}" 2>&1 &
    MOCK_PID=$!
    sleep 2
    if kill -0 ${MOCK_PID} 2>/dev/null; then
        ok "模拟教务系统已启动，PID=${MOCK_PID}"
    else
        bad "模拟教务系统启动失败"
        cat "${MOCK_PORTAL_LOG}"
    fi

    echo "--- GET 教务系统验证码接口 ---"
    curl -sS "${PORTAL}/jwgl/captcha"; echo

    echo
    echo "--- 用 http 适配器登录（真实跨进程 HTTP + sha256 密码哈希）---"
    http_login=$(curl -sS -X POST "${BASE}/api/auth/login" \
        -H 'Content-Type: application/json' \
        -d '{"studentId":"2024001","password":"secret123","adapter":"http"}')
    echo "${http_login}"
    HTTP_TOKEN=$(echo "${http_login}" | jq -r '.token // empty' 2>/dev/null)
    HTTP_AUTH="Authorization: Bearer ${HTTP_TOKEN}"

    echo
    echo "--- http 适配器同步课表（预期新增 3 门）---"
    curl -sS -X POST "${BASE}/api/sync" \
        -H "${HTTP_AUTH}" -H 'Content-Type: application/json' \
        -d "{\"semester\":\"${HTTP_SEMESTER}\",\"adapter\":\"http\"}"
    echo

    echo
    echo "--- 用 mock 的 token 去走 http 适配器（会话按适配器隔离，应 401）---"
    curl -sS -w '\nHTTP %{http_code}\n' -X POST "${BASE}/api/sync" \
        -H "${AUTH}" -H 'Content-Type: application/json' \
        -d "{\"semester\":\"${HTTP_SEMESTER}\",\"adapter\":\"http\"}"

    echo "--- http 适配器 + 错误密码登录（应 400）---"
    curl -sS -w '\nHTTP %{http_code}\n' -X POST "${BASE}/api/auth/login" \
        -H 'Content-Type: application/json' \
        -d '{"studentId":"2024001","password":"wrong-password","adapter":"http"}'

    echo "--- http 适配器 + 不存在的学号（应 400）---"
    curl -sS -w '\nHTTP %{http_code}\n' -X POST "${BASE}/api/auth/login" \
        -H 'Content-Type: application/json' \
        -d '{"studentId":"9999999","password":"secret123","adapter":"http"}'

    echo
    kill ${MOCK_PID} 2>/dev/null
    sleep 1
    echo "模拟教务系统已停止，其日志："
    cat "${MOCK_PORTAL_LOG}"
    echo

    echo "--- 自研 HTTP 服务器（epoll）冒烟测试 ---"
    # 同一连接上连发两个请求（keep-alive）：边缘触发下如果没把数据读到 EAGAIN，
    # 第二个请求就会卡住——这是最有效的 ET 正确性检查。
    check_net_server() {
        local label="$1"
        shift
        if ss -ltn 2>/dev/null | grep -q ':8081 '; then
            fuser -k 8081/tcp >/dev/null 2>&1
            sleep 1
        fi
        ./build/net_server "$@" > /tmp/net_server.log 2>&1 &
        local server_pid=$!
        sleep 1

        local response
        response=$(curl -sS --max-time 5 \
            "http://127.0.0.1:8081/first" "http://127.0.0.1:8081/second" 2>/dev/null)
        echo "  ${label}"
        echo "    响应: ${response}"
        if echo "${response}" | grep -q '"path":"/first"' &&
           echo "${response}" | grep -q '"path":"/second"'; then
            ok "${label}"
            PASSED=$((PASSED + 1))
        else
            bad "${label}（同一连接上的两个请求没有都拿到响应）"
            FAILED=$((FAILED + 1))
        fi
        kill ${server_pid} 2>/dev/null
        sleep 1
        echo "    服务端日志: $(head -1 /tmp/net_server.log)"
    }

    check_net_server "4 线程 + 水平触发（LT）" 4 0
    check_net_server "4 线程 + 边缘触发（ET）" 4 0 et
    echo

    echo "===== 断言汇总 ====="
    check_status "公开接口 /api/health" 200 "${BASE}/api/health"
    check_status "不带 token 访问受保护接口" 401 "${BASE}/api/courses"
    check_status "伪造 token" 401 \
        -H 'Authorization: Bearer forged.token.value' "${BASE}/api/courses"
    check_status "Authorization 缺少 Bearer 前缀" 401 \
        -H 'Authorization: no-bearer-here' "${BASE}/api/courses"
    check_status "登录：密码错误" 400 -X POST \
        -H 'Content-Type: application/json' \
        -d '{"studentId":"2024001","password":"wrong-password","adapter":"mock"}' \
        "${BASE}/api/auth/login"
    check_status "登录：密码过短" 400 -X POST \
        -H 'Content-Type: application/json' \
        -d '{"studentId":"2024001","password":"123","adapter":"mock"}' \
        "${BASE}/api/auth/login"
    check_status "登录：学号不存在" 400 -X POST \
        -H 'Content-Type: application/json' \
        -d '{"studentId":"9999999","password":"secret123","adapter":"mock"}' \
        "${BASE}/api/auth/login"
    check_status "登录：成功" 200 -X POST \
        -H 'Content-Type: application/json' \
        -d '{"studentId":"2024001","password":"secret123","adapter":"mock"}' \
        "${BASE}/api/auth/login"
    check_status "登录：未知适配器" 400 -X POST \
        -H 'Content-Type: application/json' \
        -d '{"studentId":"2024001","password":"secret123","adapter":"nonexistent"}' \
        "${BASE}/api/auth/login"
    check_status "新增课程：重复课程号" 409 -X POST \
        -H "${AUTH}" -H 'Content-Type: application/json' \
        -d '{"name":"重复课","code":"CS2001","semester":"2026-2027-1"}' \
        "${BASE}/api/courses"
    check_status "新增课程：空课程名" 400 -X POST \
        -H "${AUTH}" -H 'Content-Type: application/json' \
        -d '{"name":"","semester":""}' "${BASE}/api/courses"
    check_status "新增课程：课程内部时间冲突" 400 -X POST \
        -H "${AUTH}" -H 'Content-Type: application/json' \
        -d '{"name":"冲突","code":"CS8888","semester":"2026-2027-1","sessions":[{"dayOfWeek":3,"startPeriod":1,"endPeriod":2,"weeks":{"from":1,"to":16,"parity":0}},{"dayOfWeek":3,"startPeriod":2,"endPeriod":3,"weeks":{"from":1,"to":16,"parity":0}}]}' \
        "${BASE}/api/courses"
    check_status "查询不存在的课程" 404 -H "${AUTH}" "${BASE}/api/courses/999999"
    check_status "访问不存在的接口" 404 "${BASE}/api/nope"
    check_status "同步课表（mock 适配器）" 200 -X POST \
        -H "${AUTH}" -H 'Content-Type: application/json' \
        -d "{\"semester\":\"${SEMESTER}\",\"adapter\":\"mock\"}" \
        "${BASE}/api/sync"
    echo
    echo "===== 断言结果：通过 ${PASSED} 项，失败 ${FAILED} 项 ====="
    if [ "${FAILED}" -ne 0 ]; then
        echo "!! 有断言未通过，请检查上面的 [失败] 行"
    fi
    echo

    echo "===== 6/6 短压测（确认并发可用性，不是性能基准）====="
    echo "说明：准确的性能数据请跑 scripts/bench_wrk.sh；这里短跑容易受长尾影响。"
    if command -v ab >/dev/null 2>&1; then
        echo "--- 50 并发 / 3000 请求（带 JWT）---"
        ab -n 3000 -c 50 -k -H "${AUTH}" \
            "${BASE}/api/courses?semester=${SEMESTER}" 2>&1 \
            | grep -E 'Complete requests|Failed requests|Non-2xx|Requests per second|Time per request|^ *[0-9]+%'
    else
        echo "ab 未安装，跳过"
    fi
    echo

    echo "===== 限流验证（放在最后，避免影响前面的用例）====="
    echo "--- 对写接口连打 80 次（突发 40 / 每秒补 20，应出现 429）---"
    for _ in $(seq 1 80); do
        curl -sS -o /dev/null -w '%{http_code}\n' -X POST "${BASE}/api/courses" \
            -H "${AUTH}" -H 'Content-Type: application/json' \
            -d '{"name":"","semester":""}'
    done | sort | uniq -c | while read -r count code; do
        echo "  状态码 ${code}: ${count} 次"
    done

    echo
    echo "--- 服务端日志 ---"
    cat "${SERVER_LOG}"

    kill ${SERVER_PID} 2>/dev/null
    sleep 1
    echo

    echo "===== 7/7 双引擎一致性（同一套业务代码，两个 HTTP 引擎分别跑一遍）====="
    echo "说明：路由、鉴权、JSON 转换都在 api/dispatcher.cpp 里，与 HTTP 库无关。"
    echo "      这里用同一组断言分别打「自研 epoll 网络库」和「cpp-httplib 对照组」，"
    echo "      确认两边行为一致，也确认默认引擎确实是自研实现（拿 /api/version 自检）。"
    echo

    check_engine() {
        local engine="$1"
        local workers="$2"
        local log="/tmp/campus_engine_${engine}.log"

        if ss -ltn 2>/dev/null | grep -q ':8080 '; then
            fuser -k 8080/tcp >/dev/null 2>&1
            sleep 1
        fi
        CAMPUS_HTTP_ENGINE="${engine}" CAMPUS_HTTP_WORKERS="${workers}" \
            ./build/campus_server > "${log}" 2>&1 &
        local pid=$!
        sleep 2

        if ! kill -0 ${pid} 2>/dev/null; then
            bad "${engine}：进程没起来"
            FAILED=$((FAILED + 1))
            cat "${log}"
            return
        fi

        # 先确认连上的就是预期引擎。压测和接口测试最容易踩的坑就是
        # 「上一个进程没退干净，结果量到的是旧服务」，这一句专门用来防它。
        local reported
        reported=$(curl -sS "${BASE}/api/version" | jq -r '.httpEngine // "?"')
        if [ "${reported}" = "${engine}" ]; then
            ok "${engine}：/api/version 上报 httpEngine=${reported}${workers:+, workers=${workers}}"
            PASSED=$((PASSED + 1))
        else
            bad "${engine}：预期 httpEngine=${engine}，实际 ${reported}（可能打到了别的进程）"
            FAILED=$((FAILED + 1))
        fi
        echo "    启动日志: $(head -1 "${log}")"

        check_status "${engine} 探活 /api/health" 200 "${BASE}/api/health"
        check_status "${engine} 不带 token 访问受保护接口" 401 "${BASE}/api/courses"
        check_status "${engine} 访问不存在的接口" 404 "${BASE}/api/nope"
        check_status "${engine} 路径参数不是数字" 404 "${BASE}/api/courses/abc"

        local login token
        login=$(curl -sS -X POST "${BASE}/api/auth/login" \
            -H 'Content-Type: application/json' \
            -d '{"studentId":"2024001","password":"secret123","adapter":"mock"}')
        token=$(echo "${login}" | jq -r '.token // empty' 2>/dev/null)
        if [ -z "${token}" ]; then
            bad "${engine}：登录拿不到 token：${login}"
            FAILED=$((FAILED + 1))
            kill ${pid} 2>/dev/null
            sleep 1
            return
        fi

        check_status "${engine} 带 token 查询课表（走真实查库）" 200 \
            -H "Authorization: Bearer ${token}" \
            "${BASE}/api/courses?semester=${SEMESTER}"
        check_status "${engine} 查询不存在的课程" 404 \
            -H "Authorization: Bearer ${token}" "${BASE}/api/courses/999999"
        check_status "${engine} 请求体 JSON 非法（应 400）" 400 -X POST \
            -H "Authorization: Bearer ${token}" -H 'Content-Type: application/json' \
            -d '{bad json' "${BASE}/api/courses"

        # 写路径：新增 -> 查询 -> 删除。走一遍才能说明引擎对请求体、
        # 响应头（Content-Type/Retry-After）都处理正确，不只是 GET 能用。
        sleep 3   # 等限流桶回满：前面刚做过限流用例，桶是空的
        local created created_id
        created=$(curl -sS -X POST "${BASE}/api/courses" \
            -H "Authorization: Bearer ${token}" -H 'Content-Type: application/json' \
            -d "{\"name\":\"引擎一致性测试\",\"code\":\"ENG-${engine}\",\"semester\":\"${SEMESTER}\"}")
        created_id=$(echo "${created}" | jq -r '.id // empty' 2>/dev/null)
        if [ -n "${created_id}" ]; then
            ok "${engine}：POST 新增课程成功（id=${created_id}）"
            PASSED=$((PASSED + 1))
        else
            bad "${engine}：POST 新增课程失败：${created}"
            FAILED=$((FAILED + 1))
        fi
        if [ -n "${created_id}" ]; then
            check_status "${engine} 删除刚建的课程" 200 -X DELETE \
                -H "Authorization: Bearer ${token}" "${BASE}/api/courses/${created_id}"
        fi

        # 超过 1KB 的请求体：curl 会先只发请求头并等 "100 Continue"。
        # 服务器不回这个中间响应的话，curl 要等到自己的 1 秒超时才发 body。
        # 所以这里同时断言状态码和耗时——耗时才是真正的证据。
        local big_body timing code elapsed
        big_body="{$(printf 'x%.0s' $(seq 1 1200))"
        timing=$(curl -sS -o /dev/null -w '%{http_code} %{time_total}' -X POST \
            -H "Authorization: Bearer ${token}" -H 'Content-Type: application/json' \
            -d "${big_body}" "${BASE}/api/courses")
        code=${timing% *}
        elapsed=${timing#* }
        if [ "${code}" = "400" ] && awk "BEGIN{exit !(${elapsed} < 0.9)}"; then
            ok "${engine}：1.2KB 请求体（触发 Expect: 100-continue）${elapsed}s 内返回 400"
            PASSED=$((PASSED + 1))
        else
            bad "${engine}：1.2KB 请求体返回 ${code}，耗时 ${elapsed}s（期望 400 且 < 0.9s）"
            FAILED=$((FAILED + 1))
        fi

        kill ${pid} 2>/dev/null
        sleep 1
        echo
    }

    check_engine net 4
    check_engine httplib ""

    echo "===== 双引擎合计：通过 ${PASSED} 项，失败 ${FAILED} 项 ====="
    if [ "${FAILED}" -ne 0 ]; then
        echo "!! 有断言未通过，请检查上面的 [失败] 行"
    fi
    echo
    echo "完成"
} 2>&1 | tee "${LOG_FILE}"

echo
echo "日志已写入: ${LOG_FILE}"
