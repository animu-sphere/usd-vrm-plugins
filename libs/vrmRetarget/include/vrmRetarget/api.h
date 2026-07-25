// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(VRMRETARGET_STATIC)
#  define VRMRETARGET_API
#elif defined(_WIN32)
#  if defined(VRMRETARGET_EXPORTS)
#    define VRMRETARGET_API __declspec(dllexport)
#  else
#    define VRMRETARGET_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define VRMRETARGET_API __attribute__((visibility("default")))
#else
#  define VRMRETARGET_API
#endif
