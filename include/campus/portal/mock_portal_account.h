#pragma once

namespace campus {

// 测试替身共用的演示账号。
//
// 内存版 MockPortalAdapter 和独立进程的 mock_portal 都以这份常量为准，
// 否则两个实现的行为会悄悄分叉——比如内存版只校验密码长度、
// 而 HTTP 版校验真实密码，基于内存版的测试就测不出真实行为。
namespace mock_portal {

inline constexpr const char* kDemoStudentId = "2024001";
inline constexpr const char* kDemoPassword = "secret123";
inline constexpr const char* kCaptchaText = "8A2F";
inline constexpr const char* kSalt = "jwgl-salt:";
inline constexpr int kMinPasswordLength = 6;

}  // namespace mock_portal

}  // namespace campus

