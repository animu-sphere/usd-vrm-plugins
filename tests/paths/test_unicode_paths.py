#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Every executable and file format this workspace ships, handed non-ASCII paths.

OpenUSD reads a path string as UTF-8 on every platform. Windows hands a
`main(int, char**)` its arguments in the process's ANSI code page instead, and
a narrow `std::ifstream` opens a string in that same code page. So until
2026-09-15 each of these failed under a non-ASCII directory on Windows, and no
test had ever handed a tool one:

  * `motion_retarget` could not find the avatar;
  * `motion_bvh_convert` read `é` as `e` and could not open the file;
  * `usdVrmFileFormat` could not open a `.vrm` from **any** host, Python and
    usdview included, and the package path it builds for an embedded texture
    was the same string round-tripped through the code page.

The directory is named so that no single ANSI code page can spell it: CP932 has
the kana and no `é`, CP1252 the reverse. A name one code page covers would pass
on the hosts that use it and prove nothing there, and the two this repository
actually meets are a Japanese workstation and an English CI runner. Every file
inside is named the same way, because a tool that composes a relative path
writes the file name into a layer.

Each leg is one executable or one file format, and each is held to the same leg
run from an ASCII directory: a non-ASCII path may change the path and nothing
else. The two plugin legs run in *this* process, a host whose code page is not
UTF-8 on Windows, because that is the only kind of host a plugin gets -- an
executable of ours sets its own code page to UTF-8, and a plugin loaded inside
one would pass whatever it did.

On Linux and macOS a path is bytes and every leg passes with or without the
fixes; this is registered there anyway, so a platform-specific regression in
the other direction has a name too.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

from pxr import Ar, Sdf, Usd, UsdSkel

UNICODE_DIRECTORY = "ユニコード-é"

# The recorded mocopi export and the profile that reads it, the pair the
# real-avatar bake already proves onto this avatar.
RECORDED = "mocopi-mobile-arm-raise-turn.bvh"
PROFILE = "mocopi-mobile-bvh-default-v1.yaml"


class Failures:
    def __init__(self) -> None:
        self.messages: list[str] = []

    def check(self, condition: bool, message: str) -> bool:
        if not condition:
            self.messages.append(message)
        return condition

    def report(self) -> int:
        for message in self.messages:
            print(f"FAIL: {message}", file=sys.stderr)
        if self.messages:
            return 1
        print("unicode paths: every leg matches its ASCII twin")
        return 0


class Workspace:
    """One directory of inputs, named either in ASCII or in no code page."""

    def __init__(self, root: pathlib.Path, unicode: bool) -> None:
        self.unicode = unicode
        self.directory = root / (UNICODE_DIRECTORY if unicode else "ascii")
        self.directory.mkdir()

    def name(self, stem: str, japanese: str, suffix: str) -> pathlib.Path:
        return self.directory / (f"{japanese}-é{suffix}" if self.unicode
                                 else f"{stem}{suffix}")

    def copy(self, source: pathlib.Path, stem: str,
             japanese: str) -> pathlib.Path:
        target = self.name(stem, japanese, source.suffix)
        shutil.copyfile(source, target)
        return target


def run(failures: Failures, leg: str, tool: pathlib.Path,
        *arguments: object) -> subprocess.CompletedProcess | None:
    """Run one executable; a failure names the leg and says what came back."""
    result = subprocess.run(
        [str(tool), *(str(argument) for argument in arguments)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if not failures.check(
            result.returncode == 0,
            f"{leg}: {tool.name} exited {result.returncode}\n"
            f"{result.stderr.decode('utf-8', 'replace')}"):
        return None
    return result


def animation_samples(path: pathlib.Path) -> list[tuple[str, list]]:
    """Every UsdSkelAnimation the layer at `path` composes, as plain values.

    Keyed by prim path and compared whole: a non-ASCII directory has no business
    changing which prim a tool authors, or any value on it.
    """
    stage = Usd.Stage.Open(str(path))
    animations = []
    for prim in stage.Traverse():
        if not prim.IsA(UsdSkel.Animation):
            continue
        values = []
        for attribute in sorted(prim.GetAttributes(),
                                key=lambda a: a.GetName()):
            times = attribute.GetTimeSamples()
            values.append((attribute.GetName(), attribute.Get(),
                           [(t, attribute.Get(t)) for t in times]))
        animations.append((str(prim.GetPath()), values))
    return animations


def check_same_animation(failures: Failures, leg: str, unicode: pathlib.Path,
                         ascii_: pathlib.Path) -> None:
    ours = animation_samples(unicode)
    theirs = animation_samples(ascii_)
    if not failures.check(ours, f"{leg}: {unicode.name} carries no animation"):
        return
    failures.check(
        ours == theirs,
        f"{leg}: the animation authored under a non-ASCII path differs from "
        f"the one authored from an ASCII directory")


def check_bake_composes(failures: Failures, leg: str,
                        bake: pathlib.Path, avatar: pathlib.Path) -> None:
    """The bake reaches the avatar through the relative path it wrote.

    That path carries the avatar's non-ASCII file name, and a bake whose
    reference does not resolve still opens: it just has no skeleton, and the
    animation it binds drives nothing.
    """
    layer = Sdf.Layer.FindOrOpen(str(bake))
    failures.check(
        layer is not None and avatar.name in layer.ExportToString(),
        f"{leg}: {bake.name} does not name '{avatar.name}' anywhere, so the "
        f"reference to the avatar was written under some other spelling")
    stage = Usd.Stage.Open(str(bake))
    skeletons = [prim for prim in stage.Traverse()
                 if prim.IsA(UsdSkel.Skeleton)]
    if not failures.check(
            skeletons,
            f"{leg}: {bake.name} composes no skeleton, so the avatar it "
            f"references did not resolve"):
        return
    cache = UsdSkel.Cache()
    bound = [skeleton for skeleton in skeletons
             if cache.GetSkelQuery(UsdSkel.Skeleton(skeleton)).GetAnimQuery()]
    failures.check(
        bound,
        f"{leg}: no skeleton in {bake.name} is driven by an animation")


def check_plugin_host(failures: Failures, vrm: pathlib.Path,
                      ascii_vrm: pathlib.Path, vrma: pathlib.Path,
                      ascii_vrma: pathlib.Path) -> None:
    """Both file formats, loaded in a process whose code page is not ours."""
    # Asked directly, because opening a layer never asks: Sdf picks a format
    # by extension and consults CanRead only where formats share one, so the
    # sniff is unreachable from Usd.Stage.Open and a regression in it would
    # pass every other leg.
    for path, extension in ((vrm, "vrm"), (vrma, "vrma")):
        file_format = Sdf.FileFormat.FindByExtension(extension)
        failures.check(
            file_format is not None and file_format.CanRead(str(path)),
            f"{extension} file format: CanRead refuses {path.name}")

    try:
        avatar = Usd.Stage.Open(str(vrm))
    except Exception as error:  # a Tf error is raised as an exception
        failures.check(False, f"usdVrmFileFormat: {vrm.name} did not open "
                              f"from Python: {error}")
        avatar = None
    reference = Usd.Stage.Open(str(ascii_vrm))
    if avatar is not None:
        failures.check(
            [str(p.GetPath()) for p in avatar.Traverse()]
            == [str(p.GetPath()) for p in reference.Traverse()],
            f"usdVrmFileFormat: {vrm.name} imports different prims from the "
            f"same bytes under an ASCII name")
        check_textures(failures, avatar, vrm)

    try:
        clip = Usd.Stage.Open(str(vrma))
    except Exception as error:
        failures.check(False, f"usdVrmaFileFormat: {vrma.name} did not open "
                              f"from Python: {error}")
        return
    failures.check(
        any(prim.IsA(UsdSkel.Animation) for prim in clip.Traverse()),
        f"usdVrmaFileFormat: {vrma.name} opened and carries no animation")
    check_same_animation(failures, "usdVrmaFileFormat", vrma, ascii_vrma)


def check_textures(failures: Failures, stage: Usd.Stage,
                   vrm: pathlib.Path) -> None:
    """An embedded texture's package path is the avatar's own, and resolves.

    The importer writes `<avatar>.vrm[<entry>]`, and `<avatar>` is the resolved
    path USD handed it. Built through a narrow `std::filesystem::path` it came
    back as whatever the code page made of it, so the texture named a package
    that does not exist -- and a missing texture is a warning, not an error.
    """
    package = vrm.as_posix()
    resolver = Ar.GetResolver()
    context = stage.GetPathResolverContext()
    resolved_count = 0
    for prim in stage.Traverse():
        for attribute in prim.GetAttributes():
            if attribute.GetTypeName() != Sdf.ValueTypeNames.Asset:
                continue
            value = attribute.Get()
            path = str(value.path) if value else ""
            if ".vrm[" not in path:
                continue
            if not failures.check(
                    path.startswith(package + "["),
                    f"usdVrmFileFormat: texture '{path}' does not name the "
                    f"package it came from, '{package}'"):
                return
            with Ar.ResolverContextBinder(context):
                resolved = resolver.Resolve(path)
            asset = resolver.OpenAsset(resolved) if resolved else None
            if not failures.check(
                    asset is not None and asset.GetSize() > 0,
                    f"usdVrmPackageResolver: texture '{path}' did not "
                    f"resolve to any bytes"):
                return
            resolved_count += 1
    failures.check(
        resolved_count > 0,
        f"usdVrmFileFormat: {vrm.name} authored no embedded texture to resolve")


def legs(failures: Failures, arguments: argparse.Namespace,
         workspace: Workspace) -> dict[str, pathlib.Path]:
    """Run every leg in one workspace and return what it produced."""
    corpus = arguments.bvh_corpus / "recorded" / "redistributable"
    bvh = workspace.copy(corpus / RECORDED, "recorded", "収録")
    profile = workspace.copy(arguments.profiles / PROFILE, "profile",
                             "プロファイル")
    avatar = workspace.copy(arguments.avatar, "avatar", "アバター")
    vrma = workspace.copy(arguments.vrma, "walk", "歩き")
    out = {"bvh": bvh, "avatar": avatar, "vrma": vrma}

    # The recorded path: syntax, conversion through a profile named by path,
    # and a bake onto a real avatar.
    run(failures, "motion_bvh_inspect", arguments.bvh_inspect, bvh, "--all")
    out["clip"] = workspace.name("clip", "クリップ", ".usda")
    run(failures, "motion_bvh_convert", arguments.bvh_convert, bvh,
        "--profile", profile, "--output", out["clip"], "--quiet")
    out["bake"] = workspace.name("bake", "焼き込み", ".usda")
    if out["clip"].exists():
        run(failures, "motion_retarget", arguments.retarget,
            "--avatar", avatar, "--animation", out["clip"],
            "--output", out["bake"], "--quiet")

    # A `.vrma` clip, which reaches usdVrmaFileFormat through the tool.
    out["vrma_bake"] = workspace.name("vrma-bake", "歩きの焼き込み", ".usda")
    run(failures, "motion_retarget (.vrma)", arguments.retarget,
        "--avatar", avatar, "--animation", vrma,
        "--output", out["vrma_bake"], "--quiet")

    # The live path left with MIG-4: the three recorders to `motion-connectors`
    # and `motion_capture` to `usd-motion-plugins`, as `motion_record`, whose
    # own suite replays a trace from a directory no ANSI code page can spell.
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for tool in ("bvh-inspect", "bvh-convert", "retarget"):
        parser.add_argument(f"--{tool}", type=pathlib.Path, required=True)
    for data in ("avatar", "vrma", "bvh-corpus", "profiles"):
        parser.add_argument(f"--{data}", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    # A failure names a non-ASCII file, and a pipe on Windows is otherwise
    # written in the code page this test exists to stay out of.
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

    failures = Failures()
    with tempfile.TemporaryDirectory(prefix="unicode-paths-") as directory:
        root = pathlib.Path(directory)
        ascii_ = legs(failures, arguments, Workspace(root, unicode=False))
        if failures.messages:
            # The baseline is what every other leg is held to; without it a
            # comparison would report the wrong thing.
            print("the ASCII baseline itself failed; nothing below compares",
                  file=sys.stderr)
            return failures.report()
        unicode = legs(failures, arguments, Workspace(root, unicode=True))

        for key in ("clip", "bake", "vrma_bake"):
            failures.check(unicode[key].exists(),
                           f"{unicode[key].name} was not written")

        # The converter records the file's own name as provenance, so the name
        # has to come back as the one it was given rather than a code page's
        # rendering of it.
        if unicode["clip"].exists():
            clip = Usd.Stage.Open(str(unicode["clip"]))
            source_id = clip.GetDefaultPrim().GetCustomDataByKey(
                "source:sourceId")
            failures.check(
                source_id == unicode["bvh"].name,
                f"motion_bvh_convert: the clip records its source as "
                f"'{source_id}', not '{unicode['bvh'].name}'")
            check_same_animation(failures, "motion_bvh_convert",
                                 unicode["clip"], ascii_["clip"])
        for key, leg in (("bake", "motion_retarget"),
                         ("vrma_bake", "motion_retarget (.vrma)")):
            if unicode[key].exists():
                check_bake_composes(failures, leg, unicode[key],
                                    unicode["avatar"])
                check_same_animation(failures, leg, unicode[key], ascii_[key])

        check_plugin_host(failures, unicode["avatar"], ascii_["avatar"],
                          unicode["vrma"], ascii_["vrma"])

    return failures.report()


if __name__ == "__main__":
    raise SystemExit(main())
