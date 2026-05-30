#ifndef HLPLAYER_ASR_EXPORT_H
#define HLPLAYER_ASR_EXPORT_H

#ifdef _WIN32
#  ifdef HLPLAYER_ASR_EXPORTS
#    define HLPLAYER_ASR_API __declspec(dllexport)
#  else
#    define HLPLAYER_ASR_API __declspec(dllimport)
#  endif
#else
#  define HLPLAYER_ASR_API
#endif

#endif // HLPLAYER_ASR_EXPORT_H
