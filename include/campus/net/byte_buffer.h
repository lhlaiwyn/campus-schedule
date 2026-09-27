#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace campus::net {

// 非阻塞网络编程的核心数据结构：一块「读头 + 写尾」的连续缓冲区。
//
// 收数据时往写尾追加，解析时从读头消费；读过的空间在必要时自动向前压缩回收。
// 这样既避免每收一次数据就搬一次整块内存，又能让内存长期有界。
//
// 这是自研网络库的第一块积木：epoll 事件循环会用它来收数据和喂给 HTTP 解析器。
class ByteBuffer {
public:
    ByteBuffer() : ByteBuffer(4096) {}
    explicit ByteBuffer(std::size_t initial_capacity);

    // ---------- 写侧（socket recv 往这里写）----------
    char* WritePtr() { return storage_.data() + write_index_; }
    std::size_t WritableBytes() const { return storage_.size() - write_index_; }
    // 把刚写入的 count 个字节标记为可读
    void CommitWrite(std::size_t count);
    // 保证可写区至少有 need 字节，必要时搬移或扩容
    void EnsureWritable(std::size_t need);

    // ---------- 读侧（解析器从这里读）----------
    const char* ReadPtr() const { return storage_.data() + read_index_; }
    std::size_t ReadableBytes() const { return write_index_ - read_index_; }
    // 读掉 count 个字节
    void Consume(std::size_t count);
    std::string ReadAsString() const { return std::string(ReadPtr(), ReadableBytes()); }

    // ---------- 通用 ----------
    void Append(const char* data, std::size_t length);
    void Append(const std::string& text) { Append(text.data(), text.size()); }

    // 在可读区里找 needle，返回相对可读区开头的偏移；找不到返回 npos。
    // 解析请求行 / 头部终止符都靠它。
    std::size_t Find(const char* needle, std::size_t needle_length) const;

    std::size_t Capacity() const { return storage_.size(); }
    bool Empty() const { return ReadableBytes() == 0; }
    void Clear() { read_index_ = write_index_ = 0; }
    // 把读过的空间回收，让写侧能继续用
    void Compact();

private:
    std::vector<char> storage_;
    std::size_t read_index_ = 0;
    std::size_t write_index_ = 0;
};

}  // namespace campus::net

