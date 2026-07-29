// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(VRMADAPTERVMC_STATIC)
#  define VRMADAPTERVMC_API
#elif defined(_WIN32)
#  if defined(VRMADAPTERVMC_EXPORTS)
#    define VRMADAPTERVMC_API __declspec(dllexport)
#  else
#    define VRMADAPTERVMC_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define VRMADAPTERVMC_API __attribute__((visibility("default")))
#else
#  define VRMADAPTERVMC_API
#endif
