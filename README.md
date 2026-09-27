# campus-schedule 校园课表服务端

用 C++20 写的课表后端服务。解决的实际问题是：学校教务系统没有对外开放接口，
每次查课表都要重新登录，移动端体验很差。

服务端提供 RESTful 接口，可以从教务系统同步课表、检测调课变动、把课表持久化到 MySQL，
并配套一套单元测试和性能测试报告。

## 技术栈

| 层 | 选型 |
|---|---|
| 语言 | C++20 |
| HTTP | 自研 epoll 网络库（默认）+ cpp-httplib（对照组，同一套业务代码） |
| 网络模型 | 非阻塞 I/O + epoll + 多 Reactor（SO_REUSEPORT），水平/边缘触发可切换 |
| 数据库 | MySQL 8.0 |
| JSON | nlohmann/json |
| 日志 | spdlog |
| 构建 | CMake + Ninja |
| 测试 | GoogleTest（154 个用例 / 30 个套件）+ 端到端接口断言 |
| 加密与鉴权 | OpenSSL（SHA-256 密码哈希、HMAC-SHA256、HS256 JWT） |
| 部署 | Docker + docker-compose + GitHub Actions |
| 开发环境 | WSL2 / Ubuntu 24.04 |

## 架构

```
                      HTTP 请求
                         │
                ┌────────▼────────┐
                │   api 层        │  router.cpp：路由、JSON 转换、HTTP 状态码映射
                │  (接口适配)      │  不写业务规则
                └────────┬────────┘
                         │
                ┌────────▼────────┐
                │  service 层     │  CourseService  课程增删改查 + 参数校验
                │  (业务规则)      │  SyncService    同步流程编排
                │                 │  ChangeDetector 课表变动检测
                └───┬─────────┬───┘
                    │         │
        ┌───────────▼──┐  ┌───▼──────────────┐
        │ repository 层 │  │ portal 层         │
        │ (数据访问)    │  │ (外部系统适配)     │
        │ CourseRepo   │  │ PortalAdapter 接口 │
        └───────┬──────┘  │  └ MockPortal     │
                │         │  └ AdapterRegistry│
        ┌───────▼──────┐  └───┬───────────────┘
        │  MySqlPool   │      │
        │  (连接池)     │      │
        └───────┬──────┘      │
                │             │
          ┌─────▼─────┐  ┌────▼──────────────┐
          │  MySQL    │  │  学校教务系统      │
          └───────────┘  │  (当前为模拟实现)   │
                         └───────────────────┘
```

### 分层规则

- **api 层**只负责把 HTTP 请求翻译成对 service 的调用，再把结果翻译成 JSON，不写业务逻辑。
- **service 层**是唯一放业务规则的地方：参数校验、时间冲突判定、变动检测、同步编排。
- **repository 层**只和数据库打交道，SQL 全部集中在这里。
- **portal 层**封装外部教务系统，上层只认 `PortalAdapter` 接口。
- 所有可能失败的函数返回 `Result<T>`，而不是抛异常。调用方必须显式处理失败分支，
  错误码在 api 层统一映射成 HTTP 状态码。

## 关键设计

### 1. 可插拔的教务适配层

不同学校的教务系统在登录方式、验证码、课表字段、周次写法上都不一样，但
「登录 → 拉取课表」的流程是一样的。把这些差异收敛到接口实现里：

```cpp
class PortalAdapter {
public:
    virtual std::string Name() const = 0;
    virtual Result<std::string> Login(const PortalCredentials& credentials) = 0;
    virtual Result<std::vector<RawScheduleEntry>> FetchSchedule(
        const std::string& token, const std::string& semester) = 0;
    virtual void Logout(const std::string& token) = 0;
};
```

新增一所学校只需要写一个实现类并注册到 `AdapterRegistry`，**不需要修改任何已有代码**。
目前有两个实现：

| 名字 | 实现方式 | 用途 |
|---|---|---|
| `mock` | 进程内、数据确定 | 单元测试与默认演示，不依赖外部服务 |
| `http` | 真实 HTTP 调用 | 对接 `mock_portal`（独立进程的模拟教务系统） |

`http` 适配器还原了真实教务系统的两个麻烦点：先请求 `/jwgl/captcha` 拿到验证码与本次 salt，
再在客户端本地计算 `sha256(salt + password)` 提交，**明文密码不落到网络上**。
哈希用 OpenSSL 实现，`tests/crypto_test.cpp` 里放了标准测试向量。

### 模拟教务系统

`src/mock_portal/` 是一个独立进程，模拟学校教务系统的接口：

```
GET  /jwgl/captcha    下发 salt 与验证码
POST /jwgl/login      校验验证码 + sha256(salt+password)
GET  /jwgl/schedule   需要有效会话令牌
POST /jwgl/logout     注销会话
```

有了它，「登录 → 拉取 → 解析 → 入库」这条链路可以在没有真实教务系统的情况下被完整验证，
而且是真实的跨进程 HTTP 调用，不是内存里假装一下。

### 2. 课表变动检测

同步时先把本地课表和刚拉到的课表做结构化 diff：用课程号（没有课程号时用名称 + 教师）
判断是不是同一门课，再用「星期 + 起止节次」作为时间槽匹配具体课次，输出六类变动：
新增课程、课程移除、停课、时间变动、换教室、换老师、周次变动。

输出经过排序，保证同样的输入永远得到同样的结果，方便测试和幂等重跑。

### 3. 接口鉴权与限流

`POST /api/auth/login` 用教务系统凭据换本服务签发的 JWT：

1. 凭据交给教务系统判定（本服务**不保存第二份密码**）；
2. 登录成功后把教务系统的会话令牌按「适配器 + 学号」缓存起来；
3. 签发自己的 HS256 JWT，之后请求只带 token。

几个关键点：

- **密码只在登录那一个请求里出现**，既不落库也不写进令牌；后续同步直接用缓存好的教务系统会话。
- 验签时**先校验签名再解析内容**，并且要求 `alg` 必须是 HS256，防止改 header 绕过签名。
- 签名比对用定长时间比较（`CRYPTO_memcmp`），避免通过耗时差异逐字节猜出签名。
- 密钥短于 16 字节直接拒绝签发；还在用内置开发密钥时，启动日志会告警。

限流用令牌桶，**只作用于写接口和同步接口**：查询没有副作用、又是热路径，限制它只会伤害正常使用；
写操作和调用外部教务系统才是需要保护的。触发限流时返回 429 并带上 `Retry-After` 头。

### 4. 自研 MySQL 连接池

`mutex + condition_variable` 实现连接的阻塞获取，借出时用 `shared_ptr` 的自定义删除器
归还连接（RAII）。借出前用 `mysql_ping` 检测失效连接并自动重连。

### 5. 课表缓存（Cache-Aside）

缓存用**装饰器**包在仓储外面，业务层完全不知道缓存的存在：

```
MySqlCourseRepository         真实数据源
  └── CachedCourseRepository  读缓存 / 写失效
        └── CourseService     业务代码一行都没改
```

- 列表查询走 Cache-Aside：先读缓存，未命中就查库并回填；
- 写操作成功后让对应学期的缓存失效，改学期时会同时清掉旧学期；
- 缓存内容读不出来（版本升级、人为改动）时当作未命中并丢弃，避免一直读脏数据；
- 缓存为空指针表示不启用，所有调用直接透传，不影响服务可用性。

命中率是**实测值**：统计对象跨请求共享，`/api/health` 会返回 `cacheHits`、`cacheMisses`、
`cacheHitRate` 和 `cacheInvalidations`。

缓存默认**关闭**：没装 Redis 也能直接跑起来，不会因为连不上而拖慢每个请求。
要启用在有 Redis 的环境里设置 `CAMPUS_CACHE_ENABLED=true`。

### 6. 性能优化：一次完整的瓶颈定位

初始版本压测只有 179 QPS，P50 恒定在 44ms。通过分层压测定位到根因：
HTTP 框架默认未开启 `TCP_NODELAY`，响应分包触发 Nagle 算法与对端延迟 ACK 的 40ms 互等。
开启后同条件下 **QPS 提升到 11616（约 65 倍），P50 从 44ms 降到 1ms**。

完整的排查过程和原始日志见 [docs/性能测试报告.md](docs/性能测试报告.md)。

### 7. 自研 epoll 网络库（net/）

`src/net/` 是一个从零实现的事件驱动 HTTP 服务器，用来回答「为什么不用框架」：
框架的「每连接一线程」模型正是压测时 500 并发塌陷的根因。

| 组件 | 作用 |
|---|---|
| `ByteBuffer` | 读头 + 写尾的连续缓冲区，非阻塞 I/O 的核心 |
| `HttpRequestParser` | 增量解析 HTTP 请求（一次 recv 可能只收到半个请求） |
| `TimerWheel` | O(1) 添加/取消的定时器，用于连接超时 |
| `Socket` / `EpollReactor` | fd 与 epoll 的 RAII 封装 |
| `TcpServer` | 事件循环：非阻塞 accept → 读 → 解析 → 处理 → 写 |

关键设计：

- 用**水平触发（LT）**先把整体跑对，边缘触发（ET）作为后续优化；
- 连接的关闭延迟到一轮事件处理结束统一执行，避免 use-after-free；
- 响应先写进输出缓冲区，写不完注册 `EPOLLOUT` 继续写，大响应不会阻塞事件循环。

### 多 Reactor（SO_REUSEPORT）

单线程版本跑通后，用 `SO_REUSEPORT` 扩展成多 Reactor：每个 worker 线程持有自己的
epoll、监听 socket 和连接表，**共享 nothing，连接操作一次锁都不用加**。

实测（12 vCPU，wrk 4 线程 / 200 连接 / 每轮 10 秒）：

| 负载类型 | 1 worker | 2 workers | 4 workers | 扩展比 |
|---|---|---|---|---|
| 纯 I/O（只回显几个字节） | 49,621 | — | 314,197 | 6.33× |
| CPU 密集（每请求 2000 轮哈希） | 4,400 | 8,364 | 16,148 | 3.67× |

所有轮次非 2xx 响应与 socket 错误均为 0。两次独立运行的数字几乎一致。

> 一个方法论上的坑：最初我用「纯 I/O」负载验证多线程，4 个 worker 反而比单线程慢——
> 因为请求太轻，瓶颈在回环网络和压测工具上，加线程只是增加开销。
> 换成 CPU 密集负载后才测出接近线性的 3.67× 扩展。
> **想证明多线程有用，先得让瓶颈真的落在 CPU 上。**

### 8. 把自研网络库接进主服务（业务层与 HTTP 库解耦）

网络库写完只是「能跑」，真正的价值在于**主服务用它**。为此把业务逻辑从 HTTP 框架里
彻底拿出来：

```
         httplib 适配层 (api/router.cpp)        自研网络库适配层 (api/net_router.cpp)
                    │                                          │
                    └──────────────►  ApiRequest  ◄────────────┘
                                            │
                              ApiResponse   ▼
                          api/dispatcher.cpp   ← 唯一的路由表与业务编排
                                            │
                            service / repository / portal
```

| 文件 | 职责 |
|---|---|
| `api/route_table.cpp` | 「方法 + 路径 → 路由」的纯函数，零依赖 |
| `api/dispatcher.cpp` | 路由分发、JWT 鉴权、限流、JSON 转换、异常兜底 |
| `api/http_types.h` | 与任何 HTTP 库无关的 `ApiRequest` / `ApiResponse` |
| `api/router.cpp` | 只做 httplib 请求/响应与 `ApiRequest`/`ApiResponse` 的互转 |
| `api/net_router.cpp` | 只做自研网络库请求/响应与 `ApiRequest`/`ApiResponse` 的互转 |

于是：

- 默认走自研实现：`./build/campus_server`（等价于 `CAMPUS_HTTP_ENGINE=net`）；
- 跑对照组：`CAMPUS_HTTP_ENGINE=httplib ./build/campus_server`；
- 两种引擎用**同一套接口断言**验证，`/api/version` 会返回 `httpEngine` 字段，
  压测脚本先自检这个字段再开始跑，避免「打到上一个没退干净的进程」这类假数据。

```json
// GET /api/version —— 一眼看出当前进程用的是哪个引擎
{"cxxStandard":202002,"httpEngine":"net","service":"campus-schedule","version":"0.1.0"}
```

> 接入过程中修掉的两个真实问题：
> 1. `Expect: 100-continue`——curl 对超过 1KB 的请求体会先只发头再等中间响应，
>    不回就会干等到自己的 1 秒超时才发 body；服务器现在会回 `100 Continue`。
> 2. `Content-Length` 是客户端随便写的，不设上限就能被一个「声明 1GB body」的请求
>    吃光内存；现在超过 8MB 直接回 413 并关连接。

### 演示程序 `net_server`

除了主服务，`net_server` 保留为一个「只有网络库、没有任何业务」的纯 I/O 压测靶子，
用来单独量网络库本身的吞吐（多 Reactor 扩展比的实验就是用它做的）：

```bash
./build/net_server 4 0 &        # 4 个 worker，水平触发
curl http://127.0.0.1:8081/anything
# {"method":"GET","path":"/anything","query":"","hash":0}

./build/net_server 4 2000 et &  # CPU 密集 + 边缘触发
```

## 接口

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/health` | 探活，返回连接池状态 |
| GET | `/api/version` | 版本与 C++ 标准 |
| GET | `/api/courses?semester=2026-2027-1` | 查询课表（不传学期则查全部） |
| GET | `/api/courses/{id}` | 查询单门课程 |
| POST | `/api/courses` | 新增课程（含课次） |
| PUT | `/api/courses/{id}` | 全量更新课程 |
| DELETE | `/api/courses/{id}` | 删除课程（课次级联删除） |
| POST | `/api/sync` | 从教务系统同步课表并返回变动列表（`adapter` 字段可选 `mock` / `http`） |

统一约定：

- 所有响应都是 JSON，`Content-Type: application/json; charset=utf-8`。
- 失败时返回 `{"code": <HTTP 状态码>, "error": "<错误类型>", "message": "<可读原因>"}`。
  例如课程号冲突是 `HTTP 409` + `{"code":409,"error":"conflict","message":"..."}`——
  `code` 与 HTTP 状态码一致，`error` 才是给程序判断的稳定标识。
- 参数非法返回 400，课程不存在返回 404，数据库异常返回 500。

### 新增课程示例

```bash
curl -X POST http://127.0.0.1:8080/api/courses \
  -H 'Content-Type: application/json' \
  -d '{
        "name": "操作系统", "code": "CS2001", "teacher": "张伟",
        "credits": 3.5, "semester": "2026-2027-1",
        "sessions": [
          {"dayOfWeek": 2, "startPeriod": 3, "endPeriod": 4,
           "weeks": {"from": 1, "to": 16, "parity": 0},
           "location": "教学楼A301"}
        ]
      }'
```

### 同步课表示例

```bash
curl -X POST http://127.0.0.1:8080/api/sync \
  -H 'Content-Type: application/json' \
  -d '{"studentId": "2024001", "password": "secret123",
       "semester": "2026-2027-1", "adapter": "mock"}'
```

返回同步条目数、课程数、新增/更新数量，以及本次检测到的变动列表。

## 在 WSL 里运行

```bash
# 1. 初始化数据库并执行迁移（需要 root）
#    改了表结构之后也要重新跑一次，它会自动补上缺失的列
sudo bash scripts/init_db.sh

# 2. 编译并跑单元测试
bash scripts/build.sh

# 3. 启动服务（默认用 mock 适配器）
./build/campus_server

# 4.（可选）另开一个终端启动模拟教务系统，用来验证 http 适配器
./build/mock_portal
```

## 用 Docker 运行

```bash
docker compose up --build
curl http://127.0.0.1:8080/api/health
```

`docker-compose.yml` 会同时起 MySQL 和本服务，建表语句通过
`docker-entrypoint-initdb.d` 自动执行。

## 配置项

全部通过环境变量覆盖，代码里给的是开发默认值：

| 变量 | 默认值 | 说明 |
|---|---|---|
| `CAMPUS_HOST` / `CAMPUS_PORT` | `127.0.0.1` / `8080` | 服务监听地址 |
| `CAMPUS_HTTP_ENGINE` | `net` | HTTP 引擎：`net`(自研 epoll) / `httplib`(对照组) |
| `CAMPUS_HTTP_WORKERS` | 自动（按 CPU 核数，上限 16） | 自研引擎的 worker 线程数 |
| `CAMPUS_HTTP_ET` | `false` | 自研引擎是否用边缘触发（EPOLLET） |
| `CAMPUS_DB_HOST` / `CAMPUS_DB_PORT` | `127.0.0.1` / `3306` | 数据库地址 |
| `CAMPUS_DB_USER` / `CAMPUS_DB_PASSWORD` | `campus` / `campus_dev_2026` | 数据库账号 |
| `CAMPUS_DB_NAME` | `campus_schedule` | 数据库名 |
| `CAMPUS_DB_POOL_SIZE` | `8` | 连接池大小 |
| `CAMPUS_PORTAL_ADAPTER` | `mock` | 教务适配器名字 |
| `CAMPUS_AUTH_SECRET` | 内置开发密钥 | JWT 签名密钥，**上线必须覆盖** |
| `CAMPUS_AUTH_TTL_SECONDS` | `3600` | 令牌有效期 |
| `CAMPUS_RATE_LIMIT_RPS` | `20` | 每个学号每秒允许的写请求数 |
| `CAMPUS_RATE_LIMIT_BURST` | `40` | 允许的突发量 |
| `CAMPUS_CACHE_ENABLED` | `false` | 是否启用 Redis 课表缓存 |
| `CAMPUS_CACHE_HOST` / `CAMPUS_CACHE_PORT` | `127.0.0.1` / `6379` | Redis 地址 |
| `CAMPUS_CACHE_TTL_SECONDS` | `300` | 课表缓存有效期 |

## 目录结构

```
campus-schedule/
├── CMakeLists.txt
├── include/campus/
│   ├── domain/       领域模型：课程、课次、周次、Result<T>
│   ├── infra/        基础设施：配置、连接池
│   ├── portal/       教务适配层：适配器接口、注册表、转换器
│   ├── repository/   数据访问
│   ├── service/      业务：课程服务、同步服务、变动检测
│   ├── api/          路由表、分发层、两个 HTTP 引擎的适配层
│   └── net/          自研网络库：缓冲区、HTTP 解析、epoll Reactor、TCP 服务器
├── src/              对应实现
├── tests/            GoogleTest 单元测试
├── sql/schema.sql    建表语句
├── scripts/          数据库初始化、构建、压测脚本
└── docs/
    ├── ROADMAP.md         学习与开发路线
    └── 性能测试报告.md     压测过程与结论
```

## 测试与压测

```bash
bash scripts/build.sh             # 编译 + 单元测试
bash scripts/smoke_test.sh        # 端到端：单元测试 -> 鉴权 -> 业务接口 -> 双引擎一致性 -> 限流
bash scripts/bench_wrk.sh         # keep-alive 长尾复核（wrk）
bash scripts/bench_engine.sh      # 自研引擎 vs httplib 同机对比（all / http / db）
bash scripts/bench_net_server.sh  # 只测网络库本身（多 worker 扩展比）
```

最终压测结果（WSL2，12 vCPU / 6 GB，MySQL 同机，wrk，4 线程 / 100 连接 / 每轮 10 秒）：

| 模式 | QPS（连续 4 轮） | 失败响应 |
|---|---|---|
| keep-alive | 16298 / 16897 / 15866 / 15751 | 0 |
| 短连接 | 11020 / 11933 / 10370 / 11400 | 0 |

长连接复用带来约 45% 的吞吐提升。

> 这组数据是在 **cpp-httplib 引擎**上测的（`scripts/bench_wrk.sh` 里已固定引擎，便于复现）。
> 自研引擎与 httplib 的同机对比见 `scripts/bench_engine.sh`。

`scripts/smoke_test.sh` 是端到端那一层：单元测试 → 鉴权 → 业务接口 → **双引擎一致性**
→ 限流，共 **60 项接口断言**（19 项基础 + 41 项双引擎），全部自动计数。
跑完直接给出「通过 N 项，失败 N 项」，不需要人翻日志找问题。

> 中间过程值得一提：用 ab 压测时曾观察到 5 秒级长尾，看起来像服务端性能退化。
> 通过四组对照实验排除了业务代码、数据库、连接池和线程泄漏，最后换 wrk 复核，
> 确认是 ab「每连接一线程」的模型在 WSL2 下的测量假象。
> 完整排查过程见 [docs/性能测试报告.md](docs/性能测试报告.md)。

## 后续优化

- Docker Compose 已写好，但还没在真机上验证过
- 会话目前存在单进程内存里，多实例部署要换成 Redis
- 只支持 `Content-Length`，还不支持 `Transfer-Encoding: chunked` 和 `HEAD`
- Docker 改成多阶段构建缩小镜像体积
