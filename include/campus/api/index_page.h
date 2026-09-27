#pragma once

namespace campus {

// 网页版课表看板。
//
// 为什么直接内嵌成字符串，而不是放 web/ 目录按文件读：
// 那样就得处理「运行时工作目录变了、Docker 里没拷这个文件」之类的问题。
// 内嵌进二进制后，无论在 WSL 里跑还是在容器里跑，`GET /` 都一定可用。
//
// 页面逻辑很薄：登录拿 JWT -> 同步 -> 拉课表 -> 把课次画成周课表表格。
// 所有数据都来自本服务已有的 REST 接口，没有额外后门。
inline constexpr const char* kIndexHtml = R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>校园课表</title>
<style>
  :root {
    --ink: #1a1a1a; --grey: #6b7280; --line: #e5e7eb;
    --accent: #1f3864; --bg: #f6f7f9; --card: #ffffff;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; background: var(--bg); color: var(--ink);
    font-family: "Microsoft YaHei", "PingFang SC", "Helvetica Neue", Arial, sans-serif;
    font-size: 14px;
  }
  .wrap { max-width: 1120px; margin: 0 auto; padding: 20px 16px 48px; }
  header { display: flex; align-items: baseline; gap: 12px; flex-wrap: wrap; margin-bottom: 16px; }
  h1 { font-size: 20px; margin: 0; color: var(--accent); }
  .badge {
    font-size: 12px; color: var(--grey); border: 1px solid var(--line);
    background: var(--card); border-radius: 999px; padding: 2px 10px;
  }
  .card {
    background: var(--card); border: 1px solid var(--line); border-radius: 10px;
    padding: 14px 16px; margin-bottom: 14px;
  }
  .row { display: flex; flex-wrap: wrap; gap: 10px 14px; align-items: flex-end; }
  label { display: flex; flex-direction: column; gap: 4px; font-size: 12px; color: var(--grey); }
  input, select {
    font: inherit; padding: 6px 8px; border: 1px solid var(--line);
    border-radius: 6px; background: #fff; color: var(--ink); min-width: 120px;
  }
  button {
    font: inherit; padding: 7px 14px; border-radius: 6px; border: 1px solid var(--accent);
    background: var(--accent); color: #fff; cursor: pointer;
  }
  button.ghost { background: #fff; color: var(--accent); }
  button:disabled { opacity: .5; cursor: default; }
  .status { color: var(--grey); font-size: 13px; }
  .err {
    display: none; background: #fdecec; border: 1px solid #f5b5b5; color: #9b1c1c;
    border-radius: 8px; padding: 10px 12px; margin-bottom: 14px; white-space: pre-wrap;
  }
  h2 { font-size: 15px; margin: 0 0 10px; color: var(--accent); }
  table { border-collapse: collapse; width: 100%; background: var(--card); }
  .grid th, .grid td { border: 1px solid var(--line); padding: 6px; vertical-align: top; }
  .grid th { background: #eef1f6; font-size: 12px; color: var(--accent); height: 30px; }
  .grid td.period { width: 46px; text-align: center; color: var(--grey); font-size: 12px; background: #fafbfc; }
  .grid td.slot { width: 12.5%; height: 52px; }
  .cell { border-radius: 6px; padding: 5px 7px; height: 100%; line-height: 1.35; }
  .cell .n { font-weight: 600; font-size: 13px; }
  .cell .m { font-size: 11px; color: #334155; opacity: .85; }
  .cell.inactive { opacity: .32; }
  .list td, .list th { border-bottom: 1px solid var(--line); padding: 7px 8px; text-align: left; font-size: 13px; }
  .list th { color: var(--grey); font-weight: 500; font-size: 12px; }
  .tag { font-size: 11px; border-radius: 4px; padding: 1px 6px; background: #eef1f6; color: #334155; }
  .muted { color: var(--grey); }
  .hint { font-size: 12px; color: var(--grey); margin-top: 8px; line-height: 1.6; }
  code { background: #f1f3f6; padding: 1px 5px; border-radius: 4px; font-size: 12px; }
</style>
</head>
<body>
<div class="wrap">
  <header>
    <h1>校园课表</h1>
    <span class="badge" id="engine">引擎: 读取中…</span>
    <span class="badge" id="semesterBadge">学期: -</span>
  </header>

  <div class="err" id="err"></div>

  <div class="card">
    <div class="row">
      <label>学号
        <input id="studentId" value="2024001" autocomplete="username">
      </label>
      <label>密码
        <input id="password" type="password" value="secret123" autocomplete="current-password">
      </label>
      <label>教务适配器
        <select id="adapter">
          <option value="mock">mock（进程内模拟）</option>
          <option value="http">http（独立进程模拟教务系统）</option>
        </select>
      </label>
      <label>学期
        <input id="semester" value="2026-2027-1">
      </label>
      <button id="loginBtn">登录并同步课表</button>
      <span class="status" id="status">还没登录</span>
    </div>
    <div class="hint">
      密码只用于向教务系统换一次登录令牌，既不入库也不写进 JWT；
      之后页面拿着 JWT 调课表接口。演示账号 <code>2024001 / secret123</code>。
    </div>
  </div>

  <div class="card">
    <div class="row">
      <label>查看第几周
        <input id="week" type="number" min="1" max="30" value="1">
      </label>
      <label>&nbsp;
        <span><input type="checkbox" id="filterWeek"> 按周次过滤（单双周）</span>
      </label>
      <button class="ghost" id="syncBtn">重新同步</button>
      <button class="ghost" id="reloadBtn">刷新课表</button>
      <span class="status" id="syncStatus"></span>
    </div>
  </div>

  <div class="card">
    <h2>周课表</h2>
    <div id="gridWrap" class="muted">登录后这里会画出你的课表。</div>
  </div>

  <div class="card">
    <h2>课程列表</h2>
    <div id="courseWrap" class="muted">暂无数据</div>
  </div>

  <div class="card" id="changesCard" style="display:none">
    <h2>本次同步检测到的变动</h2>
    <div id="changesWrap"></div>
  </div>

  <div class="hint">
    这张页面用的就是下面的 REST 接口：<code>POST /api/auth/login</code> →
    <code>POST /api/sync</code> → <code>GET /api/courses</code>。
    同一套接口 Android 客户端也能直接用。
  </div>
</div>

<script>
const $ = (id) => document.getElementById(id);
let token = "";

function showError(msg) {
  const box = $("err");
  box.style.display = "block";
  box.textContent = msg;
}
function clearError() { $("err").style.display = "none"; }

async function api(path, options = {}) {
  const headers = Object.assign({}, options.headers || {});
  if (token) headers["Authorization"] = "Bearer " + token;
  if (options.body) headers["Content-Type"] = "application/json";
  const response = await fetch(path, Object.assign({}, options, { headers }));
  const text = await response.text();
  let data = null;
  try { data = text ? JSON.parse(text) : null; } catch (e) { data = { raw: text }; }
  if (!response.ok) {
    const message = data && data.message ? data.message : ("HTTP " + response.status);
    throw new Error("HTTP " + response.status + " · " + message);
  }
  return data;
}

// 课次在某一周是否上课：先看周次区间，再看单双周
function activeInWeek(weeks, week) {
  if (week === null || week === undefined) return true;
  if (week < weeks.from || week > weeks.to) return false;
  if (weeks.parity === 1) return week % 2 === 1;
  if (weeks.parity === 2) return week % 2 === 0;
  return true;
}

const PALETTE = [
  ["#e8f0fe", "#1a4b8c"], ["#e6f7ec", "#1b6b3a"], ["#fff4e5", "#8a5200"],
  ["#f3e8ff", "#5b21b6"], ["#e0f2fe", "#0c4a6e"], ["#fde8e8", "#8f1d1d"],
  ["#fef9c3", "#713f12"], ["#e0e7ff", "#3730a3"],
];
function colorFor(code) {
  let hash = 0;
  for (const ch of String(code || "")) hash = (hash * 31 + ch.charCodeAt(0)) % 997;
  return PALETTE[hash % PALETTE.length];
}

function renderGrid(courses, filterWeek) {
  const sessions = [];
  for (const course of courses) {
    for (const session of (course.sessions || [])) sessions.push({ session, course });
  }
  if (sessions.length === 0) {
    $("gridWrap").innerHTML =
      '<span class="muted">这个学期还没有排课的课次（只有课程基本信息）。</span>';
    return;
  }

  const maxPeriod = sessions.reduce((m, x) => Math.max(m, x.session.endPeriod), 8);
  const days = ["一", "二", "三", "四", "五", "六", "日"];
  const placed = {};
  for (const { session, course } of sessions) {
    placed[session.startPeriod + "-" + session.dayOfWeek] = { session, course };
  }

  let html = '<table class="grid"><thead><tr><th>节次</th>';
  for (const day of days) html += "<th>周" + day + "</th>";
  html += "</tr></thead><tbody>";

  for (let period = 1; period <= maxPeriod; period++) {
    html += "<tr><td class='period'>" + period + "</td>";
    for (let day = 1; day <= 7; day++) {
      const item = placed[period + "-" + day];
      if (item) {
        const { session, course } = item;
        const span = Math.max(1, session.endPeriod - session.startPeriod + 1);
        const active = activeInWeek(session.weeks, filterWeek);
        const [bg, fg] = colorFor(course.code);
        const title = course.name + " · " + (session.teacher || course.teacher || "教师待定") +
          " · " + session.weeks.text + " · " + (session.location || "地点待定");
        html += "<td class='slot' rowspan='" + span + "'><div class='cell" +
          (active ? "" : " inactive") + "' style='background:" + bg + ";color:" + fg +
          "' title=\"" + title + "\">" +
          "<div class='n'>" + course.name + "</div>" +
          "<div class='m'>" + (session.location || "") + "</div>" +
          "<div class='m'>" + session.weeks.text + "</div>" +
          "</div></td>";
      } else {
        let covered = false;
        for (const { session } of sessions) {
          if (session.dayOfWeek === day && session.startPeriod < period &&
              session.endPeriod >= period) { covered = true; break; }
        }
        if (!covered) html += "<td class='slot'></td>";
      }
    }
    html += "</tr>";
  }
  html += "</tbody></table>";
  html += '<div class="hint">灰色半透明的课表示这一周不上（单双周或周次区间之外）。' +
    "鼠标悬停可以看到教师、地点和完整周次。</div>";
  $("gridWrap").innerHTML = html;
}

function renderCourses(courses) {
  if (courses.length === 0) { $("courseWrap").innerHTML = '<span class="muted">没有课程</span>'; return; }
  let html = "<table class='list'><thead><tr>" +
    "<th>课程名</th><th>课程号</th><th>教师</th><th>学分</th><th>每周课时</th><th>来源</th>" +
    "</tr></thead><tbody>";
  for (const course of courses) {
    const source = course.source === "manual"
      ? "<span class='tag'>手动添加</span>" : "<span class='tag'>教务同步</span>";
    html += "<tr><td><b>" + course.name + "</b></td><td>" + (course.code || "-") +
      "</td><td>" + (course.teacher || "-") + "</td><td>" + course.credits +
      "</td><td>" + course.weeklyPeriods + " 节</td><td>" + source + "</td></tr>";
  }
  html += "</tbody></table>";
  $("courseWrap").innerHTML = html;
}

function renderChanges(changes) {
  if (!changes || changes.length === 0) {
    $("changesCard").style.display = "block";
    $("changesWrap").innerHTML =
      '<span class="muted">课表没有变化（再次同步是幂等的）。</span>';
    return;
  }
  let html = "<table class='list'><thead><tr><th>类型</th><th>课程</th><th>说明</th></tr></thead><tbody>";
  for (const change of changes) {
    html += "<tr><td><span class='tag'>" + change.type + "</span></td><td>" +
      change.course + "</td><td>" + change.detail + "</td></tr>";
  }
  html += "</tbody></table>";
  $("changesCard").style.display = "block";
  $("changesWrap").innerHTML = html;
}

async function loadCourses() {
  const semester = $("semester").value.trim();
  const query = semester ? ("?semester=" + encodeURIComponent(semester)) : "";
  const data = await api("/api/courses" + query);
  const courses = data.items || [];
  $("semesterBadge").textContent = "学期: " + (semester || "全部");
  renderCourses(courses);
  renderGrid(courses, $("filterWeek").checked ? Number($("week").value) : null);
  return courses;
}

async function login() {
  clearError();
  const body = JSON.stringify({
    studentId: $("studentId").value.trim(),
    password: $("password").value,
    adapter: $("adapter").value,
  });
  const data = await api("/api/auth/login", { method: "POST", body });
  token = data.token;
  $("status").textContent = "已登录，令牌 " + data.expiresIn + " 秒后过期";
}

async function sync() {
  const body = JSON.stringify({
    semester: $("semester").value.trim(),
    adapter: $("adapter").value,
  });
  const data = await api("/api/sync", { method: "POST", body });
  $("syncStatus").textContent = "同步完成：教务返回 " + data.fetchedEntries +
    " 条，转换 " + data.courses + " 门，新增 " + data.inserted +
    "，更新 " + data.updated + "，变动 " + data.changeCount + " 项";
  renderChanges(data.changes);
}

async function withBusy(button, fn) {
  button.disabled = true;
  try { await fn(); } catch (error) { showError(error.message); }
  finally { button.disabled = false; }
}

$("loginBtn").onclick = () => withBusy($("loginBtn"), async () => {
  await login();
  await sync();
  await loadCourses();
});
$("syncBtn").onclick = () => withBusy($("syncBtn"), async () => {
  if (!token) await login();
  await sync();
  await loadCourses();
});
$("reloadBtn").onclick = () => withBusy($("reloadBtn"), loadCourses);
$("filterWeek").onchange = () => { if (token) loadCourses().catch((e) => showError(e.message)); };
$("week").onchange = () => { if (token && $("filterWeek").checked) loadCourses().catch((e) => showError(e.message)); };

// 进页面先看引擎和健康状态，确认后端是活的
(async () => {
  try {
    const info = await api("/api/version");
    $("engine").textContent = "引擎: " + info.httpEngine + " · 版本 " + info.version;
  } catch (error) {
    $("engine").textContent = "引擎: 连不上后端";
    showError("读取 /api/version 失败：" + error.message);
  }
})();
</script>
</body>
</html>
)HTML";

}  // namespace campus
