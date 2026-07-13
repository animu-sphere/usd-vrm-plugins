// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(_WIN32)
#  if defined(VRMCONTAINER_EXPORTS)
#    define VRMCONTAINER_API __declspec(dllexport)
#  else
#    define VRMCONTAINER_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define VRMCONTAINER_API __attribute__((visibility("default")))
#else
#  define VRMCONTAINER_API
#endif
