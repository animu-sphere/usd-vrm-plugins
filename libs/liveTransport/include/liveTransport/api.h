// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(LIVETRANSPORT_STATIC)
#  define LIVETRANSPORT_API
#elif defined(_WIN32)
#  if defined(LIVETRANSPORT_EXPORTS)
#    define LIVETRANSPORT_API __declspec(dllexport)
#  else
#    define LIVETRANSPORT_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define LIVETRANSPORT_API __attribute__((visibility("default")))
#else
#  define LIVETRANSPORT_API
#endif
