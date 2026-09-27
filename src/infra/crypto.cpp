#include "campus/infra/crypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>

namespace campus {
namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

}  // namespace

std::string Sha256Hex(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_length = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) {
        return {};
    }

    const bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 &&
                    EVP_DigestUpdate(ctx, input.data(), input.size()) == 1 &&
                    EVP_DigestFinal_ex(ctx, digest, &digest_length) == 1;
    EVP_MD_CTX_free(ctx);

    if (!ok) {
        return {};
    }

    std::string hex;
    hex.reserve(digest_length * 2);
    for (unsigned int i = 0; i < digest_length; ++i) {
        hex.push_back(kHexDigits[digest[i] >> 4]);
        hex.push_back(kHexDigits[digest[i] & 0x0F]);
    }
    return hex;
}

std::string HashPassword(const std::string& salt, const std::string& password) {
    return Sha256Hex(salt + password);
}

bool ConstantTimeEquals(const std::string& lhs, const std::string& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    if (lhs.empty()) {
        return true;
    }
    return CRYPTO_memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
}

}  // namespace campus

