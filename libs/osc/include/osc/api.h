// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(OSC_STATIC)
#  define OSC_API
#elif defined(_WIN32)
#  if defined(OSC_EXPORTS)
#    define OSC_API __declspec(dllexport)
#  else
#    define OSC_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define OSC_API __attribute__((visibility("default")))
#else
#  define OSC_API
#endif
