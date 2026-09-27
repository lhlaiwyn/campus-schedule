#include "campus/api/dispatcher.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "campus/domain/course_json.h"
#include "campus/infra/clock.h"
#include "campus/infra/jwt.h"
#include "campus/portal/portal_types.h"
#include "campus/repository/cached_course_repository.h"
#include "campus/repository/mysql_course_repository.h"
#include "campus/service/auth_service.h"
#include "campus/service/course_service.h"
#include "campus/service/sync_service.h"

namespace campus {
namespace {

using json = nlohmann::json;

constexpr const char* kJsonContentType = "application/json; charset=utf-8";

// 当前引擎名。进程启动时写一次，之后只读，所以不需要加锁。
std::string& EngineNameStorage() {
    static std::string name = "unknown";
    return name;
}

int HttpStatusFor(ErrorCode code) {
    switch (code) {
        case ErrorCode::kOk:
            return 200;
        case ErrorCode::kNotFound:
            return 404;
        case ErrorCode::kInvalidArgument:
            return 400;
        case ErrorCode::kUnauthorized:
            return 401;
        case ErrorCode::kConflict:
            return 409;
        case ErrorCode::kTooManyRequests:
            return 429;
        case ErrorCode::kDatabaseError:
        case ErrorCode::kInternalError:
            return 500;
    }
    return 500;
}

ApiResponse JsonResponse(const json& body, int status = 200) {
    ApiResponse response;
    response.status = status;
    response.content_type = kJsonContentType;
    response.body = body.dump();
    return response;
}

ApiResponse ErrorResponse(ErrorCode code, const std::string& message) {
    // code 字段直接放 HTTP 状态码，不要放内部枚举值。
    // 早期版本塞的是 ErrorCode 的枚举值，于是「课程号冲突」会返回
    // HTTP 409 但 body 里写 "code":4，前端和读代码的人都会看懵。
    // error 字段才是机器可读的原因（conflict / unauthorized / ...）。
    const int status = HttpStatusFor(code);
    return JsonResponse(json{{"code", status},
                             {"error", ToString(code)},
                             {"message", message}},
                        status);
}

ApiResponse NotFoundResponse(const ApiRequest& request) {
    return JsonResponse(json{{"code", 404},
                             {"error", "not_found"},
                             {"message", "接口不存在"},
                             {"path", request.path}},
                        404);
}

// 受保护接口的统一入口：先校验 JWT，必要时再按学号限流。
// 返回 false 表示已经写好响应。
//
// 限流只作用于写接口和同步接口：查询没有副作用、也是热路径，
// 限制它只会伤害正常使用；而写操作和调用外部教务系统才是需要保护的。
bool GuardRequest(const ApiRequest& request, ApiResponse& response, AppContext& ctx,
                  std::string& student_id, bool apply_rate_limit = false) {
    constexpr const char* kPrefix = "Bearer ";
    const std::string header = request.Header("Authorization");
    if (header.rfind(kPrefix, 0) != 0) {
        response = ErrorResponse(
            ErrorCode::kUnauthorized,
            "缺少 Authorization: Bearer <token> 请求头，请先调用 /api/auth/login");
        return false;
    }
    const std::string token = header.substr(std::string(kPrefix).size());

    auto claims = VerifyJwt(token, ctx.config.auth.secret, UnixSecondsNow());
    if (!claims) {
        response = ErrorResponse(claims.error().code, claims.error().message);
        return false;
    }
    student_id = claims.value().subject;

    if (apply_rate_limit && ctx.rate_limiter != nullptr) {
        double retry_after = 0.0;
        if (!ctx.rate_limiter->Allow(student_id, MonotonicMillisNow(), retry_after)) {
            const int wait_seconds = static_cast<int>(retry_after) + 1;
            response = ErrorResponse(ErrorCode::kTooManyRequests,
                                     "请求过于频繁，请 " + std::to_string(wait_seconds) +
                                         " 秒后再试");
            // 告诉客户端多久之后可以重试（HTTP 429 的标准做法）
            response.headers.emplace_back("Retry-After", std::to_string(wait_seconds));
            return false;
        }
    }
    return true;
}

// 取教务适配器实例。实例由 main 在启动时创建并复用，这里只做查找。
// 之所以不允许每请求新建：适配器可能有会话状态（模拟实现就把令牌存在实例里），
// 新建实例会丢掉登录拿到的会话，表现为「刚登录就提示会话失效」。
PortalAdapter* FindAdapter(AppContext& ctx, const std::string& name, std::string& available) {
    available.clear();
    if (ctx.adapters == nullptr) {
        return nullptr;
    }
    for (const auto& entry : *ctx.adapters) {
        if (!available.empty()) {
            available += ", ";
        }
        available += entry.first;
    }
    const auto it = ctx.adapters->find(name);
    return it == ctx.adapters->end() ? nullptr : it->second.get();
}

// 每个请求构造一次仓储与服务。成员声明顺序保证了 service 初始化时
// repository 已经就绪；两者都是栈对象，不会出现悬垂引用。
struct RequestScope {
    explicit RequestScope(AppContext& ctx)
        : repository(*ctx.pool),
          cached(repository, ctx.cache, ctx.cache_stats, ctx.config.cache.ttl_seconds),
          service(cached) {}

    MySqlCourseRepository repository;
    CachedCourseRepository cached;
    CourseService service;
};

// 把请求体解析成 JSON，失败时直接返回 400
bool ParseJsonBody(const ApiRequest& request, json& body, ApiResponse& response) {
    try {
        body = json::parse(request.body);
    } catch (const std::exception& e) {
        response = ErrorResponse(ErrorCode::kInvalidArgument,
                                 std::string("JSON 解析失败: ") + e.what());
        return false;
    }
    return true;
}

// ---------- 各个接口的处理函数（业务逻辑从原来的 httplib 回调里搬过来，逻辑未变）----------

// 探活接口，给监控和压测用
ApiResponse HandleHealth(AppContext& ctx) {
    const std::size_t pool_size = ctx.pool != nullptr ? ctx.pool->Size() : 0;
    const std::size_t idle = ctx.pool != nullptr ? ctx.pool->IdleCount() : 0;

    // 命中率是实测值：统计对象跨请求共享，反映进程启动至今的真实情况
    std::uint64_t cache_hits = 0;
    std::uint64_t cache_misses = 0;
    std::uint64_t cache_invalidations = 0;
    double cache_hit_rate = 0.0;
    if (ctx.cache_stats != nullptr) {
        cache_hits = ctx.cache_stats->Hits();
        cache_misses = ctx.cache_stats->Misses();
        cache_invalidations = ctx.cache_stats->Invalidations();
        cache_hit_rate = ctx.cache_stats->HitRate();
    }

    return JsonResponse(json{
        {"status", pool_size > 0 ? "ok" : "degraded"},
        {"service", "campus-schedule"},
        {"version", CAMPUS_VERSION},
        {"dbPoolSize", pool_size},
        {"dbPoolIdle", idle},
        {"cacheEnabled", ctx.cache != nullptr},
        {"cacheBackend", ctx.cache != nullptr ? ctx.cache->Name() : "disabled"},
        {"cacheHits", cache_hits},
        {"cacheMisses", cache_misses},
        {"cacheInvalidations", cache_invalidations},
        {"cacheHitRate", cache_hit_rate},
    });
}

ApiResponse HandleVersion() {
    return JsonResponse(json{
        {"service", "campus-schedule"},
        {"version", CAMPUS_VERSION},
        {"cxxStandard", __cplusplus},
        // 让调用方能看出这次跑的是哪个 HTTP 引擎，方便压测对比
        {"httpEngine", HttpEngineName()},
    });
}

// 登录：用教务系统凭据换本服务的 JWT。
// 密码只在这一个请求里出现，既不落库也不写进令牌。
ApiResponse HandleLogin(const ApiRequest& request, AppContext& ctx) {
    json body;
    ApiResponse response;
    if (!ParseJsonBody(request, body, response)) {
        return response;
    }

    PortalCredentials credentials;
    credentials.student_id = body.value("studentId", std::string{});
    credentials.password = body.value("password", std::string{});

    std::string adapter_name = ctx.config.portal.adapter;
    if (body.contains("adapter") && body.at("adapter").is_string()) {
        adapter_name = body.at("adapter").get<std::string>();
    }

    std::string available;
    PortalAdapter* adapter = FindAdapter(ctx, adapter_name, available);
    if (adapter == nullptr) {
        return ErrorResponse(ErrorCode::kInvalidArgument,
                             "未知的教务适配器: " + adapter_name + "（可用: " + available + "）");
    }

    AuthService auth(*adapter, *ctx.sessions, ctx.config.auth.secret,
                     ctx.config.auth.token_ttl_seconds);
    const std::int64_t now = UnixSecondsNow();
    auto token = auth.Login(credentials, now);
    if (!token) {
        return ErrorResponse(token.error().code, token.error().message);
    }

    spdlog::info("用户登录成功: 学号={} 适配器={}", credentials.student_id, adapter_name);
    return JsonResponse(json{
        {"token", token.value()},
        {"tokenType", "Bearer"},
        {"expiresIn", ctx.config.auth.token_ttl_seconds},
        {"expiresAt", now + ctx.config.auth.token_ttl_seconds},
    });
}

// 查询课表：/api/courses?semester=2026-2027-1（不传学期则查全部）
ApiResponse HandleListCourses(const ApiRequest& request, AppContext& ctx) {
    ApiResponse response;
    std::string student_id;
    if (!GuardRequest(request, response, ctx, student_id)) {  // 查询只鉴权不限流
        return response;
    }
    const std::string semester = request.Query("semester");
    RequestScope scope(ctx);
    auto result = scope.service.ListCourses(semester);
    if (!result) {
        return ErrorResponse(result.error().code, result.error().message);
    }
    json items = json::array();
    for (const auto& course : result.value()) {
        items.push_back(ToJson(course));
    }
    return JsonResponse(json{{"total", items.size()}, {"items", items}});
}

ApiResponse HandleGetCourse(std::int64_t id, const ApiRequest& request, AppContext& ctx) {
    ApiResponse response;
    std::string student_id;
    if (!GuardRequest(request, response, ctx, student_id)) {
        return response;
    }
    RequestScope scope(ctx);
    auto result = scope.service.GetCourse(id);
    if (!result) {
        return ErrorResponse(result.error().code, result.error().message);
    }
    return JsonResponse(ToJson(result.value()));
}

ApiResponse HandleCreateCourse(const ApiRequest& request, AppContext& ctx) {
    ApiResponse response;
    std::string student_id;
    if (!GuardRequest(request, response, ctx, student_id, /*apply_rate_limit=*/true)) {
        return response;
    }
    json body;
    if (!ParseJsonBody(request, body, response)) {
        return response;
    }

    Course course;
    std::string error;
    if (!FromJson(body, course, error)) {
        return ErrorResponse(ErrorCode::kInvalidArgument, error);
    }

    RequestScope scope(ctx);
    auto result = scope.service.CreateCourse(std::move(course));
    if (!result) {
        return ErrorResponse(result.error().code, result.error().message);
    }
    return JsonResponse(ToJson(result.value()), 201);
}

ApiResponse HandleUpdateCourse(std::int64_t id, const ApiRequest& request, AppContext& ctx) {
    ApiResponse response;
    std::string student_id;
    if (!GuardRequest(request, response, ctx, student_id, /*apply_rate_limit=*/true)) {
        return response;
    }
    json body;
    if (!ParseJsonBody(request, body, response)) {
        return response;
    }

    Course course;
    std::string error;
    if (!FromJson(body, course, error)) {
        return ErrorResponse(ErrorCode::kInvalidArgument, error);
    }
    course.id = id;

    RequestScope scope(ctx);
    auto result = scope.service.UpdateCourse(course);
    if (!result) {
        return ErrorResponse(result.error().code, result.error().message);
    }
    return JsonResponse(ToJson(result.value()));
}

ApiResponse HandleDeleteCourse(std::int64_t id, const ApiRequest& request, AppContext& ctx) {
    ApiResponse response;
    std::string student_id;
    if (!GuardRequest(request, response, ctx, student_id, /*apply_rate_limit=*/true)) {
        return response;
    }
    RequestScope scope(ctx);
    auto result = scope.service.DeleteCourse(id);
    if (!result) {
        return ErrorResponse(result.error().code, result.error().message);
    }
    return JsonResponse(json{{"deleted", true}, {"id", id}});
}

// 同步课表：登录教务系统 -> 拉取 -> 转换 -> 与本地比对 -> 写库
ApiResponse HandleSync(const ApiRequest& request, AppContext& ctx) {
    ApiResponse response;
    std::string student_id;
    if (!GuardRequest(request, response, ctx, student_id, /*apply_rate_limit=*/true)) {
        return response;
    }
    json body;
    if (!ParseJsonBody(request, body, response)) {
        return response;
    }

    const std::string semester = body.value("semester", std::string{});

    std::string adapter_name = ctx.config.portal.adapter;
    if (body.contains("adapter") && body.at("adapter").is_string()) {
        adapter_name = body.at("adapter").get<std::string>();
    }

    std::string available;
    PortalAdapter* adapter = FindAdapter(ctx, adapter_name, available);
    if (adapter == nullptr) {
        return ErrorResponse(ErrorCode::kInvalidArgument,
                             "未知的教务适配器: " + adapter_name + "（可用: " + available + "）");
    }

    RequestScope scope(ctx);
    SyncService sync_service(*adapter, scope.cached);

    // 用登录时缓存的教务系统会话同步，不需要用户再提交一次密码
    AuthService auth(*adapter, *ctx.sessions, ctx.config.auth.secret,
                     ctx.config.auth.token_ttl_seconds);
    auto portal_token = auth.PortalTokenFor(student_id, UnixSecondsNow());
    if (!portal_token) {
        return ErrorResponse(portal_token.error().code, portal_token.error().message);
    }

    auto result = sync_service.SyncWithToken(portal_token.value(), semester);
    if (!result) {
        return ErrorResponse(result.error().code, result.error().message);
    }

    const auto& summary = result.value();
    spdlog::info("课表同步完成: 学号={} adapter={} 学期={} 拉取 {} 条 -> {} 门课，"
                 "新增 {}，更新 {}，变动 {} 项",
                 student_id, adapter_name, summary.semester, summary.fetched,
                 summary.converted, summary.inserted, summary.updated,
                 summary.changes.size());
    json changes = json::array();
    for (const auto& change : summary.changes) {
        changes.push_back(json{
            {"type", ToString(change.type)},
            {"course", change.course_name},
            {"detail", change.detail},
        });
    }
    return JsonResponse(json{
        {"semester", summary.semester},
        {"adapter", adapter_name},
        {"fetchedEntries", summary.fetched},
        {"courses", summary.converted},
        {"inserted", summary.inserted},
        {"updated", summary.updated},
        {"changeCount", summary.changes.size()},
        {"changes", changes},
    });
}

ApiResponse DispatchRoute(const ApiRequest& request, AppContext& ctx) {
    // 路由判断在 api/route_table.cpp：两个 HTTP 引擎共用同一份规则
    const RouteMatch match = MatchRoute(request.method, request.path);
    switch (match.route) {
        case Route::kHealth:
            return HandleHealth(ctx);
        case Route::kVersion:
            return HandleVersion();
        case Route::kLogin:
            return HandleLogin(request, ctx);
        case Route::kSync:
            return HandleSync(request, ctx);
        case Route::kListCourses:
            return HandleListCourses(request, ctx);
        case Route::kCreateCourse:
            return HandleCreateCourse(request, ctx);
        case Route::kGetCourse:
            return HandleGetCourse(match.course_id, request, ctx);
        case Route::kUpdateCourse:
            return HandleUpdateCourse(match.course_id, request, ctx);
        case Route::kDeleteCourse:
            return HandleDeleteCourse(match.course_id, request, ctx);
        case Route::kNotFound:
            break;
    }
    return NotFoundResponse(request);
}

}  // namespace

void SetHttpEngineName(std::string name) { EngineNameStorage() = std::move(name); }

const std::string& HttpEngineName() { return EngineNameStorage(); }

ApiResponse DispatchRequest(const ApiRequest& request, AppContext& ctx) {
    try {
        return DispatchRoute(request, ctx);
    } catch (const std::exception& e) {
        spdlog::error("请求处理异常: {} {} 原因={}", request.method, request.target, e.what());
        return JsonResponse(
            json{{"code", 500}, {"error", "internal_error"}, {"message", e.what()}}, 500);
    } catch (...) {
        spdlog::error("请求处理异常: {} {} 未知异常", request.method, request.target);
        return JsonResponse(
            json{{"code", 500}, {"error", "internal_error"}, {"message", "服务器内部错误"}}, 500);
    }
}

}  // namespace campus
