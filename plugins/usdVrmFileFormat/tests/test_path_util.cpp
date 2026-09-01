// SPDX-License-Identifier: Apache-2.0
//
// Pure-logic unit tests for util/PathUtil — the single place a glTF/VRM source
// name becomes part of a USD path. Every `Define` in the authorer trusts these
// names to be valid and distinct, and `Define` on a path that already exists
// returns the existing prim rather than failing, so a collision here is silent
// data loss downstream. No USD plugin/runtime needed — the TU is std only.
#include "util/PathUtil.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

int g_failures = 0;

void _Check(bool ok, const char* expr, int line)
{
    if (!ok) {
        std::printf("  FAIL (line %d): %s\n", line, expr);
        ++g_failures;
    }
}
#define CHECK(expr) _Check((expr), #expr, __LINE__)

bool _AllDistinct(const std::vector<std::string>& names)
{
    return std::set<std::string>(names.begin(), names.end()).size() == names.size();
}

void TestSanitize()
{
    // Already an identifier: untouched.
    CHECK(VrmSanitizeIdentifier("Body", "Mesh") == "Body");
    CHECK(VrmSanitizeIdentifier("Body_02", "Mesh") == "Body_02");
    // A leading digit is not a valid identifier start.
    CHECK(VrmSanitizeIdentifier("2Body", "Mesh") == "Mesh_2Body");
    // Invalid characters become '_', as long as one ASCII name char survives.
    CHECK(VrmSanitizeIdentifier("Left Arm.001", "Mesh") == "Left_Arm_001");
    // No ASCII name char at all (and the empty name): a stable hashed fallback,
    // not "____", so two different non-ASCII names cannot collapse together.
    const std::string kao = VrmSanitizeIdentifier("\xE9\xA1\x94", "Mesh");   // 顔
    const std::string egao = VrmSanitizeIdentifier("\xE7\xAC\x91\xE9\xA1\x94", "Mesh");  // 笑顔
    const std::string empty = VrmSanitizeIdentifier("", "Mesh");
    CHECK(kao.rfind("Mesh_", 0) == 0);
    CHECK(egao.rfind("Mesh_", 0) == 0);
    CHECK(empty.rfind("Mesh_", 0) == 0);
    CHECK(kao != egao && kao != empty && egao != empty);
    // Deterministic: the same source name always yields the same path.
    CHECK(VrmSanitizeIdentifier("\xE9\xA1\x94", "Mesh") == kao);
}

void TestUniqueNamesSuffixes()
{
    const std::vector<std::string> out =
        VrmMakeUniqueNames({"Body", "Body", "Body"}, "Mesh");
    CHECK(out.size() == 3 && out[0] == "Body" && out[1] == "Body_2" &&
          out[2] == "Body_3");
}

// The regression: a source name that already spells the suffix a duplicate is
// about to be given. Counting occurrences per base hands "Body_2" to both the
// second "Body" and to the entry actually named "Body_2".
void TestUniqueNamesSuffixCollision()
{
    const std::vector<std::string> out =
        VrmMakeUniqueNames({"Body", "Body", "Body_2"}, "Mesh");
    CHECK(out.size() == 3);
    CHECK(_AllDistinct(out));
    // Earlier entries keep the name they claimed; the loser moves on.
    CHECK(out[0] == "Body" && out[1] == "Body_2" && out[2] == "Body_2_2");
}

// The same trap from the other direction: the explicit "Body_2" comes first, so
// it is the *duplicate* that has to move past it.
void TestUniqueNamesSuffixTakenFirst()
{
    const std::vector<std::string> out =
        VrmMakeUniqueNames({"Body_2", "Body", "Body"}, "Mesh");
    CHECK(out.size() == 3);
    CHECK(_AllDistinct(out));
    CHECK(out[0] == "Body_2" && out[1] == "Body" && out[2] == "Body_3");
}

// Distinct source names that sanitize to the same identifier still get one path
// each — the uniquifier runs on the sanitized name, not on the source.
void TestUniqueNamesAfterSanitize()
{
    const std::vector<std::string> out =
        VrmMakeUniqueNames({"Left Arm", "Left.Arm", "Left/Arm"}, "Joint");
    CHECK(out.size() == 3);
    CHECK(_AllDistinct(out));
    CHECK(out[0] == "Left_Arm" && out[1] == "Left_Arm_2" && out[2] == "Left_Arm_3");
}

void TestUniqueNamesEmptyAndNonAscii()
{
    const std::vector<std::string> out = VrmMakeUniqueNames(
        {"", "", "\xE9\xA1\x94", "\xE9\xA1\x94"}, "Mesh");   // "", "", 顔, 顔
    CHECK(out.size() == 4);
    CHECK(_AllDistinct(out));
    // Same source name twice -> the second takes a suffix on the same base.
    CHECK(out[1] == out[0] + "_2");
    CHECK(out[3] == out[2] + "_2");
}

}  // namespace

int main()
{
    TestSanitize();
    TestUniqueNamesSuffixes();
    TestUniqueNamesSuffixCollision();
    TestUniqueNamesSuffixTakenFirst();
    TestUniqueNamesAfterSanitize();
    TestUniqueNamesEmptyAndNonAscii();
    if (g_failures) {
        std::printf("PathUtil unit tests: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("PathUtil unit tests: OK\n");
    return 0;
}
