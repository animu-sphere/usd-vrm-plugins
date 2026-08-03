// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONBVH_STATIC)
#  define MOTIONBVH_API
#elif defined(_WIN32)
#  if defined(MOTIONBVH_EXPORTS)
#    define MOTIONBVH_API __declspec(dllexport)
#  else
#    define MOTIONBVH_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define MOTIONBVH_API __attribute__((visibility("default")))
#else
#  define MOTIONBVH_API
#endif
