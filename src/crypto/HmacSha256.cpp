#include "HmacSha256.h"

extern "C" {
#include <libavutil/hmac.h>
}

namespace hlplayer::crypto {

HmacSha256::HmacSha256()
    : m_ctx(nullptr) {}

HmacSha256::~HmacSha256() {
    if (m_ctx) {
        av_hmac_free(m_ctx);
        m_ctx = nullptr;
    }
}

void HmacSha256::init(const uint8_t* key, size_t keyLen) {
    if (m_ctx) {
        av_hmac_free(m_ctx);
    }
    m_ctx = av_hmac_alloc(AV_HMAC_SHA256);
    if (m_ctx) {
        av_hmac_init(m_ctx, key, static_cast<unsigned int>(keyLen));
    }
}

void HmacSha256::update(const uint8_t* data, size_t len) {
    if (m_ctx) {
        av_hmac_update(m_ctx, data, static_cast<unsigned int>(len));
    }
}

int HmacSha256::final(uint8_t out[32]) {
    if (!m_ctx) {
        return 0;
    }
    return av_hmac_final(m_ctx, out, 32);
}

void HmacSha256::compute(const uint8_t* key, size_t keyLen,
                         const uint8_t* data, size_t dataLen,
                         uint8_t out[32]) {
    AVHMAC* ctx = av_hmac_alloc(AV_HMAC_SHA256);
    if (!ctx) {
        return;
    }
    av_hmac_calc(ctx, data, static_cast<unsigned int>(dataLen),
                 key, static_cast<unsigned int>(keyLen),
                 out, 32);
    av_hmac_free(ctx);
}

} // namespace hlplayer::crypto
