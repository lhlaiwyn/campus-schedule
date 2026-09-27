#include "campus/infra/hmac.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace campus {
namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

}  // namespace

std::string HmacSha256(const std::string& key, const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_length = 0;

    if (HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
             reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest,
             &digest_length) == nullptr) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(digest), digest_length);
}

std::string HmacSha256Hex(const std::string& key, const std::string& data) {
    const std::string digest = HmacSha256(key, data);
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (unsigned char byte : digest) {
        hex.push_back(kHexDigits[byte >> 4]);
        hex.push_back(kHexDigits[byte & 0x0F]);
    }
    return hex;
}

}  // namespace campus

