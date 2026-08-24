// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(VRMADAPTERVRCHATOSC_STATIC)
#  define VRMADAPTERVRCHATOSC_API
#elif defined(_WIN32)
#  if defined(VRMADAPTERVRCHATOSC_EXPORTS)
#    define VRMADAPTERVRCHATOSC_API __declspec(dllexport)
#  else
#    define VRMADAPTERVRCHATOSC_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define VRMADAPTERVRCHATOSC_API __attribute__((visibility("default")))
#else
#  define VRMADAPTERVRCHATOSC_API
#endif
