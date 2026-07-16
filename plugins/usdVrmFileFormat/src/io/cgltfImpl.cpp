// SPDX-License-Identifier: Apache-2.0
//
// The single translation unit that compiles cgltf's implementation. Keeping it
// isolated keeps cgltf's C symbols out of every other object file and makes the
// dependency a compiled-in static, never an exported ABI surface.
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
