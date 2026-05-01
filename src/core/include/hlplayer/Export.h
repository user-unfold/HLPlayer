#ifndef HLPLAYER_EXPORT_H
#define HLPLAYER_EXPORT_H

#ifdef _WIN32
#  ifdef HLPLAYER_CORE_EXPORTS
#    define HLPLAYER_CORE_API __declspec(dllexport)
#  else
#    define HLPLAYER_CORE_API __declspec(dllimport)
#  endif
#else
#  define HLPLAYER_CORE_API
#endif

#endif // HLPLAYER_EXPORT_H
