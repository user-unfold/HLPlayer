#ifndef HLPLAYER_PLAYERAPI_H
#define HLPLAYER_PLAYERAPI_H

#include <cstdint>
#include <cstddef>

#include <hlplayer/Export.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* HLPlayerHandle;

typedef enum {
    PlayerState_Idle = 0,
    PlayerState_ResolvingURL,
    PlayerState_Prepared,
    PlayerState_Buffering,
    PlayerState_Playing,
    PlayerState_Paused,
    PlayerState_Error,
    PlayerState_End,
    PlayerState_DeviceLost
} PlayerState;

typedef enum {
    PlayerErrorCode_None = 0,
    PlayerErrorCode_InvalidURL,
    PlayerErrorCode_NetworkError,
    PlayerErrorCode_DecodeError,
    PlayerErrorCode_DeviceLost,
    PlayerErrorCode_Unknown = 999
} PlayerErrorCode;

HLPLAYER_CORE_API HLPlayerHandle HLPlayer_Create(void);
HLPLAYER_CORE_API void HLPlayer_Destroy(HLPlayerHandle handle);
HLPLAYER_CORE_API PlayerErrorCode HLPlayer_Open(HLPlayerHandle handle, const char* url);
HLPLAYER_CORE_API PlayerErrorCode HLPlayer_Play(HLPlayerHandle handle);
HLPLAYER_CORE_API PlayerErrorCode HLPlayer_Pause(HLPlayerHandle handle);
HLPLAYER_CORE_API PlayerErrorCode HLPlayer_Stop(HLPlayerHandle handle);
HLPLAYER_CORE_API PlayerErrorCode HLPlayer_Seek(HLPlayerHandle handle, double seconds);
HLPLAYER_CORE_API PlayerErrorCode HLPlayer_SetVolume(HLPlayerHandle handle, double volume);
HLPLAYER_CORE_API PlayerState HLPlayer_GetState(HLPlayerHandle handle);
HLPLAYER_CORE_API void HLPlayer_GetError(HLPlayerHandle handle, char* buf, size_t bufsize);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HLPLAYER_PLAYERAPI_H
