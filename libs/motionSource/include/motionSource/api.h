// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(MOTIONSOURCE_STATIC)
#  define MOTIONSOURCE_API
#elif defined(_WIN32)
#  if defined(MOTIONSOURCE_EXPORTS)
#    define MOTIONSOURCE_API __declspec(dllexport)
#  else
#    define MOTIONSOURCE_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define MOTIONSOURCE_API __attribute__((visibility("default")))
#else
#  define MOTIONSOURCE_API
#endif
