// SPDX-License-Identifier: Apache-2.0
//
// The source rig model, built by hand throughout — which is the case
// `ValidateSourceSkeleton` exists for. A reader is not the only thing that can
// produce one of these, and the profile and converter layers above are entitled
// to the invariants rather than to a re-derivation of them.
#include "motionSource/SourceSkeleton.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace
{

using motionSource::SourceJoint;
using motionSource::SourceQuat;
using motionSource::SourceSkeleton;
using motionSource::SourceVec3;
using motionSource::ValidateSourceSkeleton;

SourceJoint
MakeJoint(std::string name, int parent, SourceVec3 restTranslation)
{
    SourceJoint joint;
    joint.name = std::move(name);
    joint.parent = parent;
    joint.restTranslation = restTranslation;
    return joint;
}

// root -> spine -> head, with a tip marker on the leaf. Names are the source's
// own; nothing here maps them to anything.
SourceSkeleton
MakeSkeleton()
{
    SourceSkeleton skeleton;
    skeleton.joints.push_back(MakeJoint("root", -1, {0.0f, 95.98f, 0.0f}));
    skeleton.joints.push_back(MakeJoint("spine", 0, {0.0f, 12.0f, 0.0f}));
    skeleton.joints.push_back(MakeJoint("head", 1, {0.0f, 20.0f, 0.0f}));
    skeleton.joints[2].tipOffset = SourceVec3{0.0f, 0.1f, 0.0f};
    return skeleton;
}

void
TestQueries()
{
    const SourceSkeleton skeleton = MakeSkeleton();

    assert(skeleton.FindJoint("spine").has_value());
    assert(*skeleton.FindJoint("spine") == 1);
    assert(!skeleton.FindJoint("Spine").has_value()); // verbatim, never folded
    assert(!skeleton.FindJoint("hips").has_value());

    assert(skeleton.HasUniqueJointNames());
    assert(skeleton.Depth(0) == 0);
    assert(skeleton.Depth(2) == 2);
    assert(skeleton.Depth(99) == 0);
    assert(skeleton.MaxDepth() == 2);

    assert(skeleton.ChildJoints(0) == std::vector<std::size_t>{1});
    assert(skeleton.ChildJoints(2).empty());
    assert(skeleton.ChildJoints(99).empty());
}

// A source that repeats a name has to be visible as an ambiguity: a profile maps
// joints by name, and silently taking the first one is how a rig gets half
// mapped without anyone being told.
void
TestDuplicateNames()
{
    // Under joint 1 rather than under the leaf: the leaf carries a tip offset,
    // and a joint with a tip may not have children.
    SourceSkeleton skeleton = MakeSkeleton();
    skeleton.joints.push_back(MakeJoint("spine", 1, {0.0f, 3.0f, 0.0f}));

    assert(!skeleton.HasUniqueJointNames());
    assert(*skeleton.FindJoint("spine") == 1);

    const std::vector<std::size_t> found = skeleton.FindJoints("spine");
    assert(found.size() == 2);
    assert(found[0] == 1 && found[1] == 3);

    // Still a valid skeleton: duplicate names are a source's business, and the
    // layer that refuses them is the profile match, with a rule to refuse by.
    assert(ValidateSourceSkeleton(skeleton));
}

void
TestCyclicParentTerminates()
{
    SourceSkeleton skeleton = MakeSkeleton();
    skeleton.joints[1].parent = 2; // spine <-> head

    assert(skeleton.Depth(1) == 0);
    assert(skeleton.Depth(2) == 0);
    assert(skeleton.MaxDepth() == 0);
    assert(!ValidateSourceSkeleton(skeleton));
}

void
TestEquality()
{
    const SourceSkeleton skeleton = MakeSkeleton();
    SourceSkeleton other = MakeSkeleton();
    assert(skeleton == other);

    other.joints[1].restTranslation.y = 12.5f;
    assert(skeleton != other);

    // An unset rest rotation is not an identity one. A source that stated no
    // rest orientation said something different from one that stated the
    // identity, and the converter needs those apart.
    other = MakeSkeleton();
    other.joints[1].restRotation = SourceQuat{};
    assert(skeleton != other);

    other = MakeSkeleton();
    other.joints[2].tipOffset.reset();
    assert(skeleton != other);
}

void
Refuses(const SourceSkeleton& skeleton, const char* expectedFragment)
{
    std::string reason;
    assert(!ValidateSourceSkeleton(skeleton, &reason));
    assert(reason.find(expectedFragment) != std::string::npos);
}

void
TestValidationRefusals()
{
    std::string reason = "not cleared";
    assert(ValidateSourceSkeleton(MakeSkeleton(), &reason));
    assert(reason.empty());

    Refuses(SourceSkeleton{}, "no joints");

    SourceSkeleton skeleton = MakeSkeleton();
    skeleton.joints[0].parent = 1;
    Refuses(skeleton, "joint 0 is not the root");

    // Exactly one root. A file describing two disconnected rigs is refused by
    // its reader rather than represented here.
    skeleton = MakeSkeleton();
    skeleton.joints[1].parent = -1;
    Refuses(skeleton, "second root");

    // Parents precede their children, so a single forward pass can build a
    // transform chain.
    skeleton = MakeSkeleton();
    skeleton.joints[1].parent = 2;
    Refuses(skeleton, "does not follow its parent");

    skeleton = MakeSkeleton();
    skeleton.joints[2].name.clear();
    Refuses(skeleton, "no name");

    const float infinity = std::numeric_limits<float>::infinity();
    const float notANumber = std::numeric_limits<float>::quiet_NaN();

    skeleton = MakeSkeleton();
    skeleton.joints[1].restTranslation.z = infinity;
    Refuses(skeleton, "non-finite rest translation");

    skeleton = MakeSkeleton();
    skeleton.joints[1].restRotation = SourceQuat{notANumber, 0.0f, 0.0f, 0.0f};
    Refuses(skeleton, "non-finite rest rotation");

    // Zero has no interpretation as a rotation in any convention, which is why
    // it is refused here while an un-normalised quaternion is not: one is a
    // writer's imprecision to report, the other is not a rotation at all.
    skeleton = MakeSkeleton();
    skeleton.joints[1].restRotation = SourceQuat{0.0f, 0.0f, 0.0f, 0.0f};
    Refuses(skeleton, "zero-magnitude rest rotation");

    skeleton = MakeSkeleton();
    skeleton.joints[1].restRotation = SourceQuat{0.5f, 0.5f, 0.0f, 0.0f};
    assert(ValidateSourceSkeleton(skeleton));

    skeleton = MakeSkeleton();
    skeleton.joints[2].tipOffset = SourceVec3{0.0f, notANumber, 0.0f};
    Refuses(skeleton, "non-finite tip offset");
}

// A tip says where a chain ends, so a joint with children has not ended. The
// combination is refused rather than left for each consumer to interpret: a
// converter building a source rest pose would otherwise have to decide whether
// the tip or the child is the continuation, and two consumers would decide
// differently.
void
TestTipOffsetRequiresALeaf()
{
    SourceSkeleton skeleton = MakeSkeleton();
    skeleton.joints[1].tipOffset = SourceVec3{0.0f, 0.1f, 0.0f};
    Refuses(skeleton, "tip offset and children");

    // The leaf keeps its own, so this is not a rule against tips.
    assert(skeleton.joints[2].tipOffset.has_value());
    skeleton.joints[1].tipOffset.reset();
    assert(ValidateSourceSkeleton(skeleton));

    // Growing a child under the leaf moves the refusal with it, which is what
    // makes this a statement about the hierarchy rather than about one index.
    skeleton.joints.push_back(MakeJoint("headTop", 2, {0.0f, 4.0f, 0.0f}));
    Refuses(skeleton, "tip offset and children");
}

// The refusal names the joint by index *and* by name, because a source that
// repeats a name would otherwise produce two identical messages for two
// different joints.
void
TestRefusalIdentifiesTheJoint()
{
    SourceSkeleton skeleton = MakeSkeleton();
    skeleton.joints.push_back(MakeJoint("spine", 1, {0.0f, 3.0f, 0.0f}));
    skeleton.joints[3].restTranslation.x = std::numeric_limits<float>::infinity();

    std::string reason;
    assert(!ValidateSourceSkeleton(skeleton, &reason));
    assert(reason.find("joint 3") != std::string::npos);
    assert(reason.find("'spine'") != std::string::npos);
}

} // namespace

int
main()
{
    TestQueries();
    TestDuplicateNames();
    TestCyclicParentTerminates();
    TestEquality();
    TestValidationRefusals();
    TestTipOffsetRequiresALeaf();
    TestRefusalIdentifiesTheJoint();
    std::printf("motionSource skeleton model: verified\n");
    return 0;
}
