// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONRUNTIME_STATIC)
#  define MOTIONRUNTIME_API
#elif defined(_WIN32)
#  if defined(MOTIONRUNTIME_EXPORTS)
#    define MOTIONRUNTIME_API __declspec(dllexport)
#  else
#    define MOTIONRUNTIME_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define MOTIONRUNTIME_API __attribute__((visibility("default")))
#else
#  define MOTIONRUNTIME_API
#endif
