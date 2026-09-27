#include "campus/net/byte_buffer.h"

#include <cstring>

namespace campus::net {

ByteBuffer::ByteBuffer(std::size_t initial_capacity)
    : storage_(initial_capacity == 0 ? 1 : initial_capacity) {}

void ByteBuffer::CommitWrite(std::size_t count) {
    // 调用方保证 count <= WritableBytes()
    write_index_ += count;
}

void ByteBuffer::EnsureWritable(std::size_t need) {
    if (WritableBytes() >= need) {
        return;
    }

    // 先尽量回收读过的空间，实在不够再扩容
    if (read_index_ > 0) {
        Compact();
    }

    if (WritableBytes() < need) {
        storage_.resize(write_index_ + need);
    }
}

void ByteBuffer::Consume(std::size_t count) {
    read_index_ += count;
    // 全部读完后指针归零，避免读写指针无限增长
    if (read_index_ == write_index_) {
        read_index_ = write_index_ = 0;
    }
}

void ByteBuffer::Append(const char* data, std::size_t length) {
    EnsureWritable(length);
    std::memcpy(WritePtr(), data, length);
    CommitWrite(length);
}

void ByteBuffer::Compact() {
    if (read_index_ == 0) {
        return;
    }
    const std::size_t remaining = ReadableBytes();
    if (remaining > 0) {
        std::memmove(storage_.data(), storage_.data() + read_index_, remaining);
    }
    read_index_ = 0;
    write_index_ = remaining;
}

std::size_t ByteBuffer::Find(const char* needle, std::size_t needle_length) const {
    if (needle_length == 0 || ReadableBytes() < needle_length) {
        return std::string::npos;
    }
    const char* begin = ReadPtr();
    const char* end = begin + ReadableBytes() - needle_length + 1;
    for (const char* p = begin; p < end; ++p) {
        if (std::memcmp(p, needle, needle_length) == 0) {
            return static_cast<std::size_t>(p - begin);
        }
    }
    return std::string::npos;
}

}  // namespace campus::net

