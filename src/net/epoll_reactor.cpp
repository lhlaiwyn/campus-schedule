#include "campus/net/epoll_reactor.h"

#include <unistd.h>

namespace campus::net {

EpollReactor::EpollReactor() { epfd_ = ::epoll_create1(EPOLL_CLOEXEC); }

EpollReactor::~EpollReactor() {
    if (epfd_ >= 0) {
        ::close(epfd_);
    }
}

bool EpollReactor::Add(int fd, std::uint32_t events, void* data) {
    epoll_event event{};
    event.events = events;
    event.data.ptr = data;
    return ::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &event) == 0;
}

bool EpollReactor::Modify(int fd, std::uint32_t events, void* data) {
    epoll_event event{};
    event.events = events;
    event.data.ptr = data;
    return ::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &event) == 0;
}

bool EpollReactor::Remove(int fd) {
    return ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

int EpollReactor::Wait(std::vector<epoll_event>& events, int timeout_ms) {
    return ::epoll_wait(epfd_, events.data(), static_cast<int>(events.size()), timeout_ms);
}

}  // namespace campus::net

