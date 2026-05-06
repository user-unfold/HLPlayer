#ifndef HLPLAYER_CAMERA_EXPORT_H
#define HLPLAYER_CAMERA_EXPORT_H

#ifdef _WIN32
#  ifdef HLPLAYER_CAMERA_EXPORTS
#    define HLPLAYER_CAMERA_API __declspec(dllexport)
#  else
#    define HLPLAYER_CAMERA_API __declspec(dllimport)
#  endif
#else
#  define HLPLAYER_CAMERA_API
#endif

#endif // HLPLAYER_CAMERA_EXPORT_H
