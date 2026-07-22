// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONCORE_STATIC)
#  define MOTIONCORE_API
#elif defined(_WIN32)
#  if defined(MOTIONCORE_EXPORTS)
#    define MOTIONCORE_API __declspec(dllexport)
#  else
#    define MOTIONCORE_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define MOTIONCORE_API __attribute__((visibility("default")))
#else
#  define MOTIONCORE_API
#endif
