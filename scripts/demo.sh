#!/usr/bin/env bash
# 引导式演示：把「启动 → 登录 → 同步 → 查询 → 校验 → 限流」完整走一遍，并解释每一步在干什么。
#
# 用法：
#   bash scripts/demo.sh          # 跑完自动停服务
#   bash scripts/demo.sh --keep   # 跑完保持服务运行（方便你用浏览器点）
#
# 前置：MySQL 已启动且已初始化（sudo bash scripts/init_db.sh）

set -uo pipefail

KEEP=0
if [ "${1:-}" = "--keep" ]; then
    KEEP=1
fi

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASE="http://127.0.0.1:8080"
SEMESTER="2026-2027-1"
SERVER_LOG="/tmp/campus_demo_server.log"
cd "${PROJECT_DIR}"

bold() { printf '\n\033[1m%s\033[0m\n' "$1"; }
note() { printf '   \033[2m%s\033[0m\n' "$1"; }
json() { jq -c . 2>/dev/null || cat; }
fail() { printf '!! %s\n' "$1"; exit 1; }

cleanup() {
    if [ "${KEEP}" -eq 0 ] && [ -n "${SERVER_PID:-}" ]; then
        kill "${SERVER_PID}" 2>/dev/null
        printf '\n服务已停止\n'
    fi
}
trap cleanup EXIT

bold "第 0 步：检查环境"
note "数据库端口 3306 是否在监听"
if ss -ltn 2>/dev/null | grep -q ':3306 '; then
    echo "   [OK] MySQL 在运行"
else
    fail "MySQL 没在运行。先执行：sudo bash scripts/init_db.sh"
fi
command -v jq >/dev/null 2>&1 || note "（没有 jq，输出会以原始 JSON 显示）"

bold "第 1 步：编译"
if [ ! -x ./build/campus_server ]; then
    note "还没编译过，先跑一次构建（约 1 分钟）"
    bash scripts/build.sh >/dev/null
fi
echo "   [OK] 可执行文件就绪: ./build/campus_server"

bold "第 2 步：启动服务"
note "默认引擎是自研 epoll 网络库（CAMPUS_HTTP_ENGINE=httplib 可切成对照组）"
if ss -ltn 2>/dev/null | grep -q ':8080 '; then
    fuser -k 8080/tcp >/dev/null 2>&1
    sleep 1
fi
# setsid：让服务脱离当前会话。这样用 --keep 演示完、wsl.exe 退出时，
# 后台服务不会被挂断（否则你去浏览器点开就是「连接被拒绝」）。
setsid ./build/campus_server > "${SERVER_LOG}" 2>&1 < /dev/null &
SERVER_PID=$!
for _ in $(seq 1 30); do
    curl -sS --max-time 1 "${BASE}/api/health" >/dev/null 2>&1 && break
    sleep 1
done
if ! curl -sS --max-time 3 "${BASE}/api/health" >/dev/null 2>&1; then
    echo "--- 服务端日志 ---"
    cat "${SERVER_LOG}"
    fail "服务没起来"
fi
echo "   [OK] 服务已监听 http://127.0.0.1:8080 （PID=${SERVER_PID}）"
echo "   启动日志:"
sed 's/^/      /' "${SERVER_LOG}" | head -8

bold "第 3 步：看引擎与健康状态（这两个接口不需要鉴权）"
echo "   \$ curl ${BASE}/api/version"
curl -sS "${BASE}/api/version" | json | sed 's/^/   /'
note "httpEngine 说明这次跑的是哪个 HTTP 引擎——压测脚本靠它自检，避免量到残留进程"
echo
echo "   \$ curl ${BASE}/api/health"
curl -sS "${BASE}/api/health" | json | sed 's/^/   /'
note "dbPoolIdle/dbPoolSize 是连接池状态；cache* 是缓存命中统计（实测值）"

bold "第 4 步：受保护接口不带 token 会被拒"
echo "   \$ curl -i ${BASE}/api/courses"
curl -sS -w '\n   → HTTP %{http_code}\n' "${BASE}/api/courses" | json | sed 's/^/   /'

bold "第 5 步：用教务账号换本服务的 JWT"
note "演示账号 2024001 / secret123（内置 mock 适配器）"
echo "   \$ curl -X POST ${BASE}/api/auth/login -d '{\"studentId\":\"2024001\",...}'"
LOGIN=$(curl -sS -X POST "${BASE}/api/auth/login" \
    -H 'Content-Type: application/json' \
    -d '{"studentId":"2024001","password":"secret123","adapter":"mock"}')
echo "${LOGIN}" | json | sed 's/^/   /'
TOKEN=$(echo "${LOGIN}" | jq -r '.token // empty' 2>/dev/null)
[ -n "${TOKEN}" ] || fail "登录失败，看上面的响应"
AUTH="Authorization: Bearer ${TOKEN}"
note "密码只在这一次请求里出现，既不落库也不写进令牌；后续都用这个 JWT"

bold "第 6 步：从教务系统同步课表"
echo "   \$ curl -X POST ${BASE}/api/sync -H \"\$AUTH\" -d '{\"semester\":\"${SEMESTER}\",\"adapter\":\"mock\"}'"
curl -sS -X POST "${BASE}/api/sync" -H "${AUTH}" -H 'Content-Type: application/json' \
    -d "{\"semester\":\"${SEMESTER}\",\"adapter\":\"mock\"}" | json | sed 's/^/   /'
note "fetchEntries=教务系统返回的条目数，inserted/updated/changes 是同步结果"

bold "第 7 步：再同步一次（课表没变，应该 0 变动——这就是幂等）"
curl -sS -X POST "${BASE}/api/sync" -H "${AUTH}" -H 'Content-Type: application/json' \
    -d "{\"semester\":\"${SEMESTER}\",\"adapter\":\"mock\"}" \
    | jq -c '{inserted,updated,changeCount}' 2>/dev/null | sed 's/^/   /'

bold "第 8 步：查询课表"
echo "   \$ curl -H \"\$AUTH\" '${BASE}/api/courses?semester=${SEMESTER}'"
curl -sS -H "${AUTH}" "${BASE}/api/courses?semester=${SEMESTER}" \
    | jq -c '{total, courses: [.items[] | {id, name, code, sessions: (.sessions | length)}]}' 2>/dev/null \
    | sed 's/^/   /'

bold "第 9 步：新增一门课（手动添加的课不会被同步覆盖）"
NEW=$(curl -sS -X POST "${BASE}/api/courses" -H "${AUTH}" -H 'Content-Type: application/json' \
    -d "{\"name\":\"演示课程\",\"code\":\"DEMO-1\",\"teacher\":\"我自己\",\"credits\":2.0,
         \"semester\":\"${SEMESTER}\",
         \"sessions\":[{\"dayOfWeek\":3,\"startPeriod\":5,\"endPeriod\":6,
         \"weeks\":{\"from\":1,\"to\":16,\"parity\":0},\"location\":\"教三-201\"}]}")
echo "${NEW}" | jq -c '{id, name, code, source, weeklyPeriods}' 2>/dev/null | sed 's/^/   /'
NEW_ID=$(echo "${NEW}" | jq -r '.id // empty' 2>/dev/null)
note "source=manual 表示手动添加；同步时这类课不会被误报「已移除」"

bold "第 10 步：故意犯两个错，看错误处理"
echo "   ① 重复课程号（唯一键冲突）→ 应 409"
curl -sS -w '\n   → HTTP %{http_code}\n' -X POST "${BASE}/api/courses" \
    -H "${AUTH}" -H 'Content-Type: application/json' \
    -d "{\"name\":\"重复的课\",\"code\":\"DEMO-1\",\"semester\":\"${SEMESTER}\"}" \
    | json | sed 's/^/   /'
echo "   ② 同一门课内部时间冲突（周一第 1-2 节 与 第 2-3 节）→ 应 400，并指出冲突位置"
curl -sS -w '\n   → HTTP %{http_code}\n' -X POST "${BASE}/api/courses" \
    -H "${AUTH}" -H 'Content-Type: application/json' \
    -d "{\"name\":\"冲突演示\",\"code\":\"DEMO-2\",\"semester\":\"${SEMESTER}\",
         \"sessions\":[{\"dayOfWeek\":1,\"startPeriod\":1,\"endPeriod\":2,\"weeks\":{\"from\":1,\"to\":16}},
                      {\"dayOfWeek\":1,\"startPeriod\":2,\"endPeriod\":3,\"weeks\":{\"from\":1,\"to\":16}}]}" \
    | json | sed 's/^/   /'
note "错误体固定是 {code, error, message}：code 与 HTTP 状态码一致，error 给程序判断，message 给人看"

bold "第 11 步：清理刚才手动加的课"
if [ -n "${NEW_ID}" ]; then
    curl -sS -X DELETE -H "${AUTH}" "${BASE}/api/courses/${NEW_ID}" | json | sed 's/^/   /'
fi

bold "第 12 步：限流（写接口 20 次/秒、突发 40）"
note "连打 60 次新增请求，观察 400/429 的比例"
for _ in $(seq 1 60); do
    curl -sS -o /dev/null -w '%{http_code}\n' -X POST "${BASE}/api/courses" \
        -H "${AUTH}" -H 'Content-Type: application/json' -d '{"name":"","semester":""}'
done | sort | uniq -c | while read -r count code; do
    echo "   状态码 ${code}: ${count} 次"
done
note "429 就是被限流；响应头带 Retry-After 告诉客户端等多久"

bold "演示结束"
echo "   服务端日志：${SERVER_LOG}"
if [ "${KEEP}" -eq 1 ]; then
    echo "   服务仍在 http://127.0.0.1:8080 运行（你可以用浏览器打开 /api/health、/api/version）"
    echo "   停止：在 WSL 里执行 fuser -k 8080/tcp"
else
    echo "   服务将在脚本退出时自动停止"
fi
