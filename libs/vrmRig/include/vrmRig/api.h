// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(VRMRIG_STATIC)
#define VRMRIG_API
#elif defined(_WIN32)
#if defined(VRMRIG_EXPORTS)
#define VRMRIG_API __declspec(dllexport)
#else
#define VRMRIG_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define VRMRIG_API __attribute__((visibility("default")))
#else
#define VRMRIG_API
#endif
