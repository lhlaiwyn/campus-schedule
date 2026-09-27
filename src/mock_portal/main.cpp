// 模拟教务系统：一个独立进程，用来替代真实学校的教务系统。
//
// 它故意保留真实教务系统的几个麻烦点：
//   1. 必须先拿验证码（测试替身用固定验证码，便于自动化）
//   2. 密码不明文传输，客户端要提交 sha256(salt + password)
//   3. 课表接口需要有效的会话令牌
//
// 有了它，适配层和同步链路可以在没有真实教务系统的情况下被完整验证。

#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_set>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "campus/infra/crypto.h"
#include "campus/portal/mock_portal_account.h"

namespace {

using json = nlohmann::json;

// 演示账号与验证码常量集中放在 mock_portal_account.h，
// 保证与内存版适配器的行为完全一致
constexpr const char* kCaptchaText = campus::mock_portal::kCaptchaText;
constexpr const char* kSalt = campus::mock_portal::kSalt;
constexpr const char* kDemoStudentId = campus::mock_portal::kDemoStudentId;
constexpr const char* kDemoPassword = campus::mock_portal::kDemoPassword;

std::mutex g_mutex;
std::unordered_set<std::string> g_tokens;
std::int64_t g_token_seq = 0;

void SendJson(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

void SendError(httplib::Response& res, int status, const std::string& message) {
    SendJson(res, json{{"message", message}}, status);
}

json BuildScheduleEntries() {
    json entries = json::array();
    entries.push_back(json{{"courseName", "操作系统"},
                           {"courseCode", "CS2001"},
                           {"teacher", "张伟"},
                           {"location", "教学楼A301"},
                           {"dayOfWeek", 2},
                           {"startPeriod", 3},
                           {"endPeriod", 4},
                           {"weeksText", "1-16周"},
                           {"credits", 3.5}});
    entries.push_back(json{{"courseName", "数据结构"},
                           {"courseCode", "CS2002"},
                           {"teacher", "李娜"},
                           {"location", "教学楼B102"},
                           {"dayOfWeek", 4},
                           {"startPeriod", 1},
                           {"endPeriod", 2},
                           {"weeksText", "1-16周(单)"},
                           {"credits", 4.0}});
    entries.push_back(json{{"courseName", "计算机网络"},
                           {"courseCode", "CS2003"},
                           {"teacher", "王强"},
                           {"location", "实验楼C201"},
                           {"dayOfWeek", 6},
                           {"startPeriod", 5},
                           {"endPeriod", 8},
                           {"weeksText", "3-15周(双)"},
                           {"credits", 3.0}});
    return entries;
}

}  // namespace

int main() {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");

    int port = 9090;
    if (const char* value = std::getenv("CAMPUS_MOCK_PORTAL_PORT")) {
        try {
            port = std::stoi(value);
        } catch (...) {
        }
    }

    httplib::Server server;

    // 1. 下发验证码和本次密码盐
    server.Get("/jwgl/captcha", [](const httplib::Request&, httplib::Response& res) {
        SendJson(res, json{
                           {"captchaId", "captcha-1"},
                           {"captchaText", kCaptchaText},
                           {"salt", kSalt},
                           {"note", "这是测试替身，验证码固定为 8A2F"},
                       });
    });

    // 2. 登录：验证码 + sha256(salt + password)
    server.Post("/jwgl/login", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            SendError(res, 400, "请求体不是合法 JSON");
            return;
        }

        const std::string student_id = body.value("studentId", std::string{});
        const std::string password_hash = body.value("passwordHash", std::string{});
        const std::string captcha = body.value("captchaText", std::string{});

        if (captcha != kCaptchaText) {
            SendError(res, 400, "验证码错误");
            return;
        }
        if (student_id != kDemoStudentId) {
            SendError(res, 400, "学号不存在（演示账号为 2024001）");
            return;
        }
        if (password_hash != campus::HashPassword(kSalt, kDemoPassword)) {
            SendError(res, 400, "密码错误");
            return;
        }

        std::string token;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            token = "jwgl-token-" + std::to_string(++g_token_seq);
            g_tokens.insert(token);
        }
        SendJson(res, json{{"token", token},
                           {"studentId", student_id},
                           {"expiresInSeconds", 1800}});
    });

    // 3. 课表：必须带有效的会话令牌
    server.Get("/jwgl/schedule", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("token") || !req.has_param("semester")) {
            SendError(res, 400, "缺少 token 或 semester 参数");
            return;
        }
        const std::string token = req.get_param_value("token");
        const std::string semester = req.get_param_value("semester");

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_tokens.find(token) == g_tokens.end()) {
                SendError(res, 401, "会话无效或已过期，请重新登录");
                return;
            }
        }
        if (semester.empty()) {
            SendError(res, 400, "学期不能为空");
            return;
        }

        SendJson(res, json{{"semester", semester}, {"entries", BuildScheduleEntries()}});
    });

    // 4. 退出登录
    server.Post("/jwgl/logout", [](const httplib::Request& req, httplib::Response& res) {
        std::string token;
        try {
            token = json::parse(req.body).value("token", std::string{});
        } catch (...) {
        }
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_tokens.erase(token);
        }
        SendJson(res, json{{"ok", true}});
    });

    server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.body.empty()) {
            SendError(res, res.status == 0 ? 404 : res.status, "接口不存在");
        }
    });

    spdlog::info("模拟教务系统启动: http://127.0.0.1:{}", port);
    spdlog::info("演示账号 {} / {}，验证码固定 8A2F，密码以 sha256(salt+password) 提交",
                 kDemoStudentId, kDemoPassword);

    if (!server.listen("127.0.0.1", port)) {
        spdlog::error("监听失败，端口 {} 可能被占用", port);
        return 1;
    }
    return 0;
}
