// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace vrmExport
{
// Invoke the existing bake tool directly, with no shell. Its --quiet mode
// keeps stdout available for vrm_export's JSON report; refusals use stderr.
bool BakeMotion(const std::string& avatar, const std::string& motion, const std::string& output,
                std::string* error);
} // namespace vrmExport
