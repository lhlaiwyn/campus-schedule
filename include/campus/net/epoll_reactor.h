#pragma once

#include <cstdint>
#include <vector>

#include <sys/epoll.h>

namespace campus::net {

// epoll 的 RAII 封装。
//
// 这里用水平触发（LT）而不是边缘触发（ET）：
// LT 语义简单、不容易漏事件，适合先把整体跑对；ET 性能更好，
// 但要求每次读写都读到 EAGAIN，代码更容易出错。先 LT，ET 是后续优化。
class EpollReactor {
public:
    EpollReactor();
    ~EpollReactor();

    EpollReactor(const EpollReactor&) = delete;
    EpollReactor& operator=(const EpollReactor&) = delete;

    bool Valid() const { return epfd_ >= 0; }
    int Fd() const { return epfd_; }

    // data 是事件回调时透传给调用方的指针，这里存连接对象的地址
    bool Add(int fd, std::uint32_t events, void* data);
    bool Modify(int fd, std::uint32_t events, void* data);
    bool Remove(int fd);

    // 阻塞等待事件。返回就绪事件个数：0 表示超时，-1 表示出错
    int Wait(std::vector<epoll_event>& events, int timeout_ms);

private:
    int epfd_ = -1;
};

}  // namespace campus::net

