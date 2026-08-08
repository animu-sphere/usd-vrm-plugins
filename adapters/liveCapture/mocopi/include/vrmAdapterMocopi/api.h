// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(VRMADAPTERMOCOPI_STATIC)
#  define VRMADAPTERMOCOPI_API
#elif defined(_WIN32)
#  if defined(VRMADAPTERMOCOPI_EXPORTS)
#    define VRMADAPTERMOCOPI_API __declspec(dllexport)
#  else
#    define VRMADAPTERMOCOPI_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define VRMADAPTERMOCOPI_API __attribute__((visibility("default")))
#else
#  define VRMADAPTERMOCOPI_API
#endif
