// SPDX-License-Identifier: Apache-2.0
#include "vrmRetarget/TargetSkeleton.h"

#include <unordered_map>

namespace vrmRetarget
{

int
TargetSkeleton::FindJoint(const std::string& token) const
{
    for (std::size_t i = 0; i < _joints.size(); ++i) {
        if (_joints[i].token == token) {
            return static_cast<int>(i);
        }
    }
    return kNoParent;
}

void
TargetSkeleton::ResolveParentsFromTokens()
{
    std::unordered_map<std::string, int> byToken;
    byToken.reserve(_joints.size());
    for (std::size_t i = 0; i < _joints.size(); ++i) {
        byToken.emplace(_joints[i].token, static_cast<int>(i));
    }

    for (std::size_t i = 0; i < _joints.size(); ++i) {
        TargetJoint& joint = _joints[i];
        const std::size_t separator = joint.token.rfind('/');
        if (separator == std::string::npos) {
            joint.parent = kNoParent;
            continue;
        }
        const auto found = byToken.find(joint.token.substr(0, separator));
        // A joint whose parent path is not itself a joint of this skeleton is
        // treated as a root rather than dangling.
        joint.parent = (found == byToken.end()) ? kNoParent : found->second;
    }
}

bool
TargetSkeleton::IsTopologicallyOrdered() const
{
    for (std::size_t i = 0; i < _joints.size(); ++i) {
        const int parent = _joints[i].parent;
        if (parent == kNoParent) {
            continue;
        }
        if (parent < 0 || parent >= static_cast<int>(i)) {
            return false;
        }
    }
    return true;
}

} // namespace vrmRetarget
