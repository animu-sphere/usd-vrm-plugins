// SPDX-License-Identifier: Apache-2.0
//
// USD prim/property name hygiene. Source names from glTF/VRM are never trusted:
// they may be empty, non-ASCII, collide, or start with a digit. Every name that
// becomes part of a USD path goes through here so that the same source file
// always yields the same, valid, collision-free paths.
#pragma once

#include "pxr/pxr.h"

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

// Turn an arbitrary source string into a single valid USD identifier. Empty /
// all-invalid input falls back to "<fallbackPrefix>_<hash>"; a leading digit is
// prefixed. Deterministic for a given input.
std::string VrmSanitizeIdentifier(const std::string& value,
                                  const std::string& fallbackPrefix);

// Sanitize a batch of source names and disambiguate collisions deterministically
// by appending _2, _3, ... in input order. `fallbackPrefix` is used both for the
// empty-name fallback and the collision suffix base.
std::vector<std::string> VrmMakeUniqueNames(const std::vector<std::string>& sourceNames,
                                            const std::string& fallbackPrefix);

PXR_NAMESPACE_CLOSE_SCOPE
