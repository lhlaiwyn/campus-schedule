#pragma once

// 这一个头文件负责集中配置 httplib 的编译期开关。
// 必须在任何 <httplib.h> 之前包含，保证所有编译单元用同一份配置。

// 打开 TCP_NODELAY。
// httplib 默认关闭，导致响应分包时受 Nagle 算法与对端延迟 ACK 影响：
// 头信息和 body 分两次 write，第二个小包要等 ACK，而对端最多等 40ms 才回 ACK，
// 于是每个 keep-alive 请求平白多出约 40ms 延迟（压测实测 P50 稳定在 44ms）。
#define CPPHTTPLIB_TCP_NODELAY 1

// httplib 每接受一个连接就从线程池取一条线程，且 keep-alive 连接会一直占着它。
// 默认线程数是「CPU 核数 - 1」，高并发下连接会被这个数量卡住，所以显式调大。
// 这是线程-每连接模型的固有代价：想同时服务 N 条 keep-alive 连接，
// 就需要 N 条线程。真正做到几十万连接要靠 epoll 事件驱动，见 docs/ROADMAP.md。
#define CPPHTTPLIB_THREAD_POOL_COUNT 256

// httplib 默认的 listen backlog 只有 5：内核等待 accept 的连接队列一满，
// 新来的连接会被直接丢弃，客户端只能按 TCP 指数退避重传（1s→3s→7s→…），
// 表现为偶发几十秒的「假死」和 ab 报出的 Failed requests。这里调大。
#define CPPHTTPLIB_LISTEN_BACKLOG 1024

// 服务端对空闲 keep-alive 连接的超时，默认只有 5 秒。
// 超时后服务端会主动断开空闲连接，而客户端（浏览器、压测工具）会继续复用，
// 撞上「服务端正在关闭」的那一瞬间就会出现失败请求，并出现 5 秒级的停顿。
// 压测日志里出现的 5005ms 极值正好等于这个默认值。这里放宽到 30 秒。
#define CPPHTTPLIB_KEEPALIVE_TIMEOUT_SECOND 30

#include <httplib.h>
