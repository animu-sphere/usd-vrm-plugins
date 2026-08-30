// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONTRACKING_STATIC)
#  define MOTIONTRACKING_API
#elif defined(_WIN32)
#  if defined(MOTIONTRACKING_EXPORTS)
#    define MOTIONTRACKING_API __declspec(dllexport)
#  else
#    define MOTIONTRACKING_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define MOTIONTRACKING_API __attribute__((visibility("default")))
#else
#  define MOTIONTRACKING_API
#endif
