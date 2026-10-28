// SPDX-License-Identifier: Apache-2.0
#include "vrmRig/SpringBoneSolver.h"
#include "pxr/base/js/json.h"
#include "pxr/base/js/value.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>

namespace
{
using namespace vrmRig;
using pxr::GfQuatf;
using pxr::GfVec3f;
using pxr::JsValue;
const JsValue&
At(const JsValue& v, const char* key)
{
    return v.GetJsObject().at(key);
}
float
Number(const JsValue& v)
{
    return static_cast<float>(v.IsInt() ? v.GetInt() : v.GetReal());
}
GfVec3f
Vec(const JsValue& v)
{
    const auto& a = v.GetJsArray();
    return GfVec3f(Number(a[0]), Number(a[1]), Number(a[2]));
}
struct Expected
{
    GfVec3f tail;
    GfQuatf rotation;
};
using Rows = std::map<std::string, std::map<int, std::map<int, Expected>>>;
Rows
Read(const std::string& path)
{
    std::ifstream file(path);
    assert(file.good());
    Rows rows;
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        std::istringstream stream(line);
        std::vector<std::string> fields;
        std::string field;
        while (std::getline(stream, field, ','))
            fields.push_back(field);
        assert(fields.size() == 10);
        Expected expected;
        for (int k = 0; k < 3; ++k)
            expected.tail[k] = std::stof(fields[k + 3]);
        expected.rotation =
            GfQuatf(std::stof(fields[6]),
                    GfVec3f(std::stof(fields[7]), std::stof(fields[8]), std::stof(fields[9])));
        assert(
            rows[fields[0]][std::stoi(fields[1])].emplace(std::stoi(fields[2]), expected).second);
    }
    assert(!rows.empty());
    return rows;
}
} // namespace

int
main(int argc, char** argv)
{
    assert(argc == 4);
    const auto expected = Read(argv[2]);
    std::ifstream file(argv[1]);
    assert(file.good());
    std::string json((std::istreambuf_iterator<char>(file)), {});
    const JsValue input = pxr::JsParseString(json);
    assert(input.IsObject());
    const auto& parameters = At(input, "parameters");
    SpringBoneParameters p;
    p.stiffness = Number(At(parameters, "stiffness"));
    p.dragForce = Number(At(parameters, "dragForce"));
    p.gravityPower = Number(At(parameters, "gravityPower"));
    p.gravityDirection = Vec(At(parameters, "gravityDirection"));
    p.hitRadius = Number(At(parameters, "hitRadius"));
    size_t cases = 0, samples = 0;
    float worstPosition = 0, worstRotation = 0;
    for (const auto& spec : At(input, "cases").GetJsArray())
    {
        const std::string name = At(spec, "name").GetString();
        if (std::string(argv[3]) == "vrm0" && At(spec, "version").GetInt() != 0)
            continue;
        assert(expected.count(name) == 1);
        ++cases;
        SpringBoneRig rig;
        for (const auto& parent : At(spec, "parents").GetJsArray())
            rig.parents.push_back(parent.GetInt());
        for (const auto& translation : At(spec, "translations").GetJsArray())
            rig.restTransforms.push_back({Vec(translation), GfQuatf(1)});
        SpringBoneSpring spring;
        auto& declaration = At(spec, "version").GetInt() == 0 ? spring.rootJoints : spring.joints;
        for (const auto& joint : At(spec, "declaration").GetJsArray())
            declaration.push_back(joint.GetInt());
        spring.parameters.resize(declaration.size(), p);
        if (spec.GetJsObject().count("center"))
            spring.center = At(spec, "center").GetInt();
        if (spec.GetJsObject().count("collider"))
        {
            const auto& c = At(spec, "collider");
            SpringBoneCollider collider;
            collider.joint = 0;
            collider.offset = Vec(At(c, "offset"));
            collider.radius = Number(At(c, "radius"));
            if (At(c, "shape").GetString() == "capsule")
            {
                collider.shape = SpringBoneColliderShape::Capsule;
                collider.tail = Vec(At(c, "tail"));
            }
            rig.colliders = {collider};
            rig.colliderGroups = {{0}};
            spring.colliderGroups = {0};
        }
        rig.springs = {spring};
        SpringBonePose pose{rig.restTransforms, {}};
        SpringBoneSolver solver;
        SpringBoneResult result;
        assert(solver.Configure(rig) && solver.Reset(pose, &result));
        const int frames = At(input, "frames").GetInt();
        assert(expected.at(name).size() == static_cast<size_t>(frames));
        for (int frame = 1; frame <= frames; ++frame)
        {
            pose.placement.translation =
                Vec(At(input, "translationPerFrame")) * static_cast<float>(frame);
            const float halfAngle = Number(At(input, "radiansPerFrame")) * frame * 0.5f;
            pose.localTransforms[0].rotation =
                GfQuatf(std::cos(halfAngle), GfVec3f(0, 0, std::sin(halfAngle)));
            assert(solver.Step(Number(At(input, "seconds")), pose, &result));
            assert(result.tails.size() == expected.at(name).at(frame).size());
            for (const auto& tail : result.tails)
            {
                const auto& row = expected.at(name).at(frame).at(tail.joint);
                const float positionError = (tail.position - row.tail).GetLength();
                const auto actual = result.localRotations[tail.joint].GetNormalized();
                auto ref = row.rotation.GetNormalized();
                if (pxr::GfDot(actual, ref) < 0)
                    ref = GfQuatf(-ref.GetReal(), -ref.GetImaginary());
                const float rotationError = (actual - ref).GetLength();
                worstPosition = std::max(worstPosition, positionError);
                worstRotation = std::max(worstRotation, rotationError);
                if (positionError > 1e-4f || rotationError > 1e-4f)
                {
                    std::fprintf(
                        stderr,
                        "%s frame %d joint %d: position error %.9g m, quaternion error %.9g\n",
                        name.c_str(), frame, tail.joint, positionError, rotationError);
                    return 1;
                }
                ++samples;
            }
        }
    }
    assert(cases == expected.size());
    std::printf(
        "%zu cases, %zu joint samples: maximum position error %.9g m, quaternion error %.9g\n",
        cases, samples, worstPosition, worstRotation);
}
