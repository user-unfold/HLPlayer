#ifndef HLPLAYER_CRYPTO_EXPORT_H
#define HLPLAYER_CRYPTO_EXPORT_H

#ifdef _WIN32
#  ifdef HLPLAYER_CRYPTO_EXPORTS
#    define HLPLAYER_CRYPTO_API __declspec(dllexport)
#  else
#    define HLPLAYER_CRYPTO_API __declspec(dllimport)
#  endif
#else
#  define HLPLAYER_CRYPTO_API
#endif

#endif // HLPLAYER_CRYPTO_EXPORT_H
