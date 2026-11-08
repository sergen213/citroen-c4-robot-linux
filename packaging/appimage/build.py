#!/usr/bin/env python3
"""Create a native Linux x86_64 AppImage from an already-validated release stage."""
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile

from .acquire import acquire, _require_platform

ASSETS = Path(__file__).resolve().parent


def _copy_stage(stage: Path, destination: Path) -> None:
    for path in stage.rglob("*"):
        mode = path.lstat().st_mode
        if stat.S_ISLNK(mode):
            target = os.readlink(path)
            if os.path.isabs(target) or not path.resolve(strict=True).is_relative_to(stage):
                raise ValueError(f"Stage symlink escapes its release tree: {path}")
        elif not (stat.S_ISREG(mode) or stat.S_ISDIR(mode)):
            raise ValueError(f"Unsupported release file type: {path}")
    shutil.copytree(stage, destination, symlinks=True)


def _normalize(root: Path, epoch: int) -> None:
    for path in [root, *root.rglob("*")]:
        if not path.is_symlink():
            executable = path.is_dir() or bool(path.stat().st_mode & 0o111)
            path.chmod(0o755 if executable else 0o644)
        os.utime(path, (epoch, epoch), follow_symlinks=False)


def create(stage: Path, output: Path, version: str, epoch: int, tools_dir: Path) -> Path:
    """Build a fresh artifact; signing is deliberately performed by the caller.

    stage is the complete native tree with bin/c4-launcher, lib and plugins.
    No installer or recovered proprietary resources belong in that tree.
    Cached packaging downloads are verified and freshly extracted before use.
    """
    _require_platform()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+_-]{0,127}", version):
        raise ValueError("Version must be a safe nonempty release identifier")
    if type(epoch) is not int or not 0 <= epoch <= 0xFFFFFFFF:
        raise ValueError("SOURCE_DATE_EPOCH must fit an unsigned squashfs timestamp")
    stage = stage.resolve(strict=True)
    if not stage.is_dir() or not (stage / "bin/c4-launcher").is_file():
        raise ValueError("Native release stage has no bin/c4-launcher")
    output = output.absolute()
    if os.path.lexists(output):
        raise FileExistsError(f"Refusing to replace an existing artifact: {output}")
    if output.parent.resolve().is_relative_to(stage):
        raise ValueError("AppImage output must be outside the native release stage")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent, prefix=".appimage-") as work:
        work = Path(work)
        tools = acquire(tools_dir, names=("appimagetool", "runtime"),
                        extraction_dir=work / "tools")
        appdir = work / "CitroenC4.AppDir"
        appdir.mkdir()
        _copy_stage(stage, appdir / "usr")
        shutil.copyfile(ASSETS / "AppRun", appdir / "AppRun")
        (appdir / "AppRun").chmod(0o755)
        desktop = (ASSETS / "citroen-c4.desktop").read_text(encoding="utf-8")
        (appdir / "citroen-c4.desktop").write_text(
            desktop.rstrip() + f"\nX-AppImage-Version={version}\n", encoding="utf-8")
        shutil.copyfile(ASSETS / "citroen-c4.svg", appdir / "citroen-c4.svg")
        (appdir / ".DirIcon").symlink_to("citroen-c4.svg")
        _normalize(appdir, epoch)
        artifact = work / "CitroenC4-x86_64.AppImage"
        command = [str(tools["appimagetool"]), "--no-appstream", "--runtime-file",
                   str(tools["runtime"]), "--comp", "zstd"]
        for argument in ("-processors", "1", "-no-xattrs"):
            command.append(f"--mksquashfs-opt={argument}")
        command.extend((str(appdir), str(artifact)))
        # SOURCE_DATE_EPOCH clamps appimagetool's rewritten desktop/icon mtimes too.
        # Do not combine it with -mkfs-time/-all-time: upstream rejects that pairing.
        environment = {"PATH": os.defpath, "LC_ALL": "C", "TZ": "UTC", "ARCH": "x86_64",
                       "SOURCE_DATE_EPOCH": str(epoch)}
        subprocess.run(command, cwd=work, env=environment, check=True)
        if artifact.is_symlink() or not artifact.is_file():
            raise RuntimeError("appimagetool did not produce a regular artifact")
        with artifact.open("rb") as stream:
            header = stream.read(11)
        if header[:4] != b"\x7fELF" or header[8:11] != b"AI\x02":
            raise RuntimeError("appimagetool output is not a type2 AppImage")
        artifact.chmod(0o755)
        os.utime(artifact, (epoch, epoch))
        # Same-filesystem hard link publishes atomically without overwriting even a
        # file/symlink created concurrently after the initial freshness check.
        os.link(artifact, output)
    return output
