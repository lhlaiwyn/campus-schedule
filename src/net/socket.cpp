#include "campus/net/socket.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace campus::net {

Socket::Socket(int fd) : fd_(fd) {}

Socket::~Socket() { Close(); }

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        Close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void Socket::Close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void Socket::Adopt(int fd) {
    Close();
    fd_ = fd;
}

bool Socket::SetNonBlocking() {
    const int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool Socket::SetReuseAddr() {
    int one = 1;
    return ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == 0;
}

bool Socket::SetNoDelay() {
    int one = 1;
    return ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == 0;
}

bool Socket::SetReusePort() {
    int one = 1;
    return ::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) == 0;
}

}  // namespace campus::net
