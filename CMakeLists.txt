cmake_minimum_required(VERSION 3.20)
project(campus_schedule VERSION 0.1.0 LANGUAGES CXX)

# ---------- 语言标准 ----------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 生成 compile_commands.json，编辑器靠它做代码补全和跳转
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型" FORCE)
endif()

option(CAMPUS_BUILD_TESTS "构建单元测试" ON)

# ---------- 依赖 ----------
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)

pkg_check_modules(HTTPLIB     REQUIRED IMPORTED_TARGET cpp-httplib)
pkg_check_modules(SPDLOG      REQUIRED IMPORTED_TARGET spdlog)
pkg_check_modules(MYSQLCLIENT REQUIRED IMPORTED_TARGET mysqlclient)
pkg_check_modules(HIREDIS     REQUIRED IMPORTED_TARGET hiredis)
# 我们直接调用 OpenSSL 的 EVP 接口算 SHA-256。
# cpp-httplib 只是间接依赖 libcrypto，链接器不会自动带进来，
# 必须显式链接，否则报 "DSO missing from command line"。
pkg_check_modules(OPENSSL     REQUIRED IMPORTED_TARGET openssl)

# ---------- 网络基础库（纯 C++，无外部依赖，可单独测试）----------
add_library(campus_net STATIC
    src/net/byte_buffer.cpp
    src/net/epoll_reactor.cpp
    src/net/http_parser.cpp
    src/net/http_response.cpp
    src/net/socket.cpp
    src/net/tcp_server.cpp
    src/net/timer_wheel.cpp
)
target_include_directories(campus_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------- 核心静态库 ----------
add_library(campus_core STATIC
    src/domain/course.cpp
    src/domain/course_codec.cpp
    src/domain/week_parser.cpp
    src/infra/base64.cpp
    src/infra/clock.cpp
    src/infra/config.cpp
    src/infra/crypto.cpp
    src/infra/hmac.cpp
    src/infra/jwt.cpp
    src/infra/mysql_pool.cpp
    src/infra/portal_session_store.cpp
    src/infra/rate_limiter.cpp
    src/infra/cache_store.cpp
    src/infra/redis_cache_store.cpp
    src/portal/adapter_registry.cpp
    src/portal/http_portal_adapter.cpp
    src/portal/mock_portal_adapter.cpp
    src/portal/portal_json.cpp
    src/portal/schedule_converter.cpp
    src/repository/mysql_course_repository.cpp
    src/repository/cached_course_repository.cpp
    src/service/auth_service.cpp
    src/service/course_service.cpp
    src/service/change_detector.cpp
    src/service/sync_service.cpp
    src/api/dispatcher.cpp
    src/api/net_router.cpp
    src/api/router.cpp
    src/api/route_table.cpp
)

target_include_directories(campus_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(campus_core
    PUBLIC
        campus_net
        PkgConfig::HTTPLIB
        PkgConfig::SPDLOG
        PkgConfig::MYSQLCLIENT
        PkgConfig::HIREDIS
        PkgConfig::OPENSSL
        nlohmann_json::nlohmann_json
        Threads::Threads
)

target_compile_definitions(campus_core
    PUBLIC CAMPUS_VERSION="${PROJECT_VERSION}"
)

# ---------- 可执行文件 ----------
add_executable(campus_server src/main.cpp)
target_link_libraries(campus_server PRIVATE campus_core)

# 模拟教务系统单独一个进程，方便演示「跨进程 HTTP 集成」而不是内存里的假实现
add_executable(mock_portal src/mock_portal/main.cpp)
target_link_libraries(mock_portal PRIVATE campus_core)

# 自研 HTTP 服务器演示（只用 campus_net，不依赖 campus_core）
add_executable(net_server src/net_server/main.cpp)
target_link_libraries(net_server PRIVATE campus_net)

set(CAMPUS_TARGETS campus_core campus_net campus_server mock_portal net_server)

# ---------- 单元测试 ----------
if(CAMPUS_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    add_executable(campus_tests
        tests/auth_test.cpp
        tests/cache_test.cpp
        tests/course_test.cpp
        tests/crypto_test.cpp
        tests/dispatcher_test.cpp
        tests/net/byte_buffer_test.cpp
        tests/net/http_parser_test.cpp
        tests/net/timer_wheel_test.cpp
        tests/portal_json_test.cpp
        tests/portal_test.cpp
        tests/sync_test.cpp
    )
    target_link_libraries(campus_tests
        PRIVATE campus_core GTest::gtest GTest::gtest_main
    )

    add_test(NAME campus_tests COMMAND campus_tests)
    list(APPEND CAMPUS_TARGETS campus_tests)
endif()

# ---------- 编译警告 ----------
foreach(tgt ${CAMPUS_TARGETS})
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
