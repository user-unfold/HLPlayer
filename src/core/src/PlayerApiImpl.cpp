#include <hlplayer/PlayerFacade.h>
#include <hlplayer/PlayerApi.h>
#include <hlplayer/Result.h>

#include <cstring>
#include <exception>
#include <functional>
#include <string>

namespace {

PlayerErrorCode mapError(hlplayer::PlayerError err) {
    switch (err) {
        case hlplayer::PlayerError::None:         return PlayerErrorCode_None;
        case hlplayer::PlayerError::InvalidURL:   return PlayerErrorCode_InvalidURL;
        case hlplayer::PlayerError::NetworkError: return PlayerErrorCode_NetworkError;
        case hlplayer::PlayerError::DecodeError:  return PlayerErrorCode_DecodeError;
        case hlplayer::PlayerError::DeviceLost:   return PlayerErrorCode_DeviceLost;
        default:                                  return PlayerErrorCode_Unknown;
    }
}

PlayerErrorCode safeInvoke(std::function<PlayerErrorCode()> fn) {
    try {
        return fn();
    } catch (const std::exception&) {
        return PlayerErrorCode_Unknown;
    } catch (...) {
        return PlayerErrorCode_Unknown;
    }
}

} // namespace

extern "C" {

HLPLAYER_CORE_API HLPlayerHandle HLPlayer_Create(void) {
    try {
        auto* facade = new hlplayer::PlayerFacade();
        return reinterpret_cast<HLPlayerHandle>(facade);
    } catch (...) {
        return nullptr;
    }
}

HLPLAYER_CORE_API void HLPlayer_Destroy(HLPlayerHandle handle) {
    if (!handle) return;
    try {
        auto* facade = reinterpret_cast<hlplayer::PlayerFacade*>(handle);
        delete facade;
    } catch (...) {}
}

HLPLAYER_CORE_API PlayerErrorCode HLPlayer_Open(HLPlayerHandle handle, const char* url) {
    return safeInvoke([&] {
        if (!handle || !url) return PlayerErrorCode_Unknown;
        auto* facade = reinterpret_cast<hlplayer::PlayerFacade*>(handle);
        auto result = facade->open(std::string(url));
        return result.hasValue() ? PlayerErrorCode_None : mapError(result.error());
    });
}

HLPLAYER_CORE_API PlayerErrorCode HLPlayer_Play(HLPlayerHandle handle) {
    return safeInvoke([&] {
        if (!handle) return PlayerErrorCode_Unknown;
        auto* facade = reinterpret_cast<hlplayer::PlayerFacade*>(handle);
        auto result = facade->play();
        return result.hasValue() ? PlayerErrorCode_None : mapError(result.error());
    });
}

HLPLAYER_CORE_API PlayerErrorCode HLPlayer_Pause(HLPlayerHandle handle) {
    return safeInvoke([&] {
        if (!handle) return PlayerErrorCode_Unknown;
        auto* facade = reinterpret_cast<hlplayer::PlayerFacade*>(handle);
        auto result = facade->pause();
        return result.hasValue() ? PlayerErrorCode_None : mapError(result.error());
    });
}

HLPLAYER_CORE_API PlayerErrorCode HLPlayer_Stop(HLPlayerHandle handle) {
    return safeInvoke([&] {
        if (!handle) return PlayerErrorCode_Unknown;
        auto* facade = reinterpret_cast<hlplayer::PlayerFacade*>(handle);
        auto result = facade->stop();
        return result.hasValue() ? PlayerErrorCode_None : mapError(result.error());
    });
}

HLPLAYER_CORE_API PlayerErrorCode HLPlayer_Seek(HLPlayerHandle handle, double seconds) {
    return safeInvoke([&] {
        if (!handle) return PlayerErrorCode_Unknown;
        auto* facade = reinterpret_cast<hlplayer::PlayerFacade*>(handle);
        auto result = facade->seek(seconds);
        return result.hasValue() ? PlayerErrorCode_None : mapError(result.error());
    });
}

HLPLAYER_CORE_API PlayerErrorCode HLPlayer_SetVolume(HLPlayerHandle handle, double volume) {
    return safeInvoke([&] {
        if (!handle) return PlayerErrorCode_Unknown;
        auto* facade = reinterpret_cast<hlplayer::PlayerFacade*>(handle);
        auto result = facade->setVolume(volume);
        return result.hasValue() ? PlayerErrorCode_None : mapError(result.error());
    });
}

HLPLAYER_CORE_API PlayerState HLPlayer_GetState(HLPlayerHandle handle) {
    if (!handle) return PlayerState_Idle;
    try {
        auto* facade = reinterpret_cast<hlplayer::PlayerFacade*>(handle);
        return facade->getState();
    } catch (...) {
        return PlayerState_Error;
    }
}

HLPLAYER_CORE_API void HLPlayer_GetError(HLPlayerHandle handle, char* buf, size_t bufsize) {
    if (!handle || !buf || bufsize == 0) return;
    try {
        auto* facade = reinterpret_cast<hlplayer::PlayerFacade*>(handle);
        std::string msg = facade->getLastError();
        size_t copyLen = (msg.size() < bufsize - 1) ? msg.size() : bufsize - 1;
        std::memcpy(buf, msg.c_str(), copyLen);
        buf[copyLen] = '\0';
    } catch (...) {
        buf[0] = '\0';
    }
}

} // extern "C"
