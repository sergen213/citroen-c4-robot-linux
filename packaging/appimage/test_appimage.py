#!/usr/bin/env python3
"""Offline contract checks; real artifact reproducibility is verified separately."""
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import tarfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from appimage import acquire, build


def main() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        tools_dir = root / "tools-cache"
        tools_dir.mkdir()
        archive = tools_dir / "patchelf.tar.gz"
        authentic = b"authenticated-patchelf"
        with tarfile.open(archive, "w:gz") as source:
            binary = tarfile.TarInfo("bin/patchelf")
            binary.size = len(authentic)
            binary.mode = 0o755
            source.addfile(binary, io.BytesIO(authentic))
        archive_bytes = archive.read_bytes()
        lock = root / "tools.lock.json"
        lock.write_text(json.dumps({"tools": {"patchelf": {
            "name": archive.name,
            "url": "https://invalid.example/patchelf",
            "sha256": hashlib.sha256(archive_bytes).hexdigest(),
            "binary_path": "patchelf-0.19.2-x86_64/bin/patchelf",
        }}}), encoding="utf-8")
        first_root = root / "first-extraction"
        second_root = root / "second-extraction"
        with patch.object(acquire, "LOCK", lock), \
                patch.object(acquire, "_require_platform"), \
                patch.object(acquire.urllib.request, "urlopen") as download:
            first = acquire.acquire(tools_dir, names=("patchelf",),
                                    extraction_dir=first_root)["patchelf"]
            assert first.is_relative_to(first_root)
            assert first.read_bytes() == authentic
            assert first.stat().st_mode & 0o111
            assert archive.read_bytes() == archive_bytes
            first.write_bytes(b"mutated-active-patchelf")
            second = acquire.acquire(tools_dir, names=("patchelf",),
                                     extraction_dir=second_root)["patchelf"]
            assert second.is_relative_to(second_root)
            assert first != second
            assert first.read_bytes() == b"mutated-active-patchelf"
            assert second.read_bytes() == authentic
            assert second.stat().st_mode & 0o111
            assert archive.read_bytes() == archive_bytes
            archive.write_bytes(b"corrupted-archive")
            try:
                acquire.acquire(tools_dir, names=("patchelf",),
                                extraction_dir=root / "third-extraction")
            except ValueError:
                pass
            else:
                raise AssertionError("Tampered archive was accepted for isolated extraction")
            download.assert_not_called()
        cached = root / "cached"
        cached.write_bytes(b"tampered")
        with patch.object(acquire.urllib.request, "urlopen") as download:
            try:
                acquire._download(root, "cached", "https://invalid.example/tool",
                                  hashlib.sha256(b"trusted").hexdigest())
            except ValueError:
                pass
            else:
                raise AssertionError("Tampered tool cache was accepted")
            download.assert_not_called()
        stage = root / "stage"
        (stage / "bin").mkdir(parents=True)
        launcher = stage / "bin/c4-launcher"
        launcher.write_bytes(b"native-placeholder-for-copy-contract-only")
        launcher.chmod(0o755)
        (stage / "escape").symlink_to(cached)
        try:
            build._copy_stage(stage, root / "unsafe-copy")
        except ValueError:
            pass
        else:
            raise AssertionError("Escaping stage symlink was accepted")
        (stage / "escape").unlink()
        epoch = 1234567890
        tools = {"runtime": root / "trusted-runtime"}

        def fake_acquire(tools_dir, *, names, extraction_dir):
            assert tools_dir == root
            assert names == ("appimagetool", "runtime")
            assert isinstance(extraction_dir, Path)
            assert extraction_dir.name == "tools"
            assert extraction_dir.parent.name.startswith(".appimage-")
            assert extraction_dir.parent.parent == output.parent
            assert extraction_dir.parent.is_dir()
            extraction_dir.mkdir()
            tools["appimagetool"] = extraction_dir / "trusted-tool"
            tools["appimagetool"].write_bytes(b"extracted-tool-contract")
            return tools

        def fake_packager(command, *, cwd, env, check):
            assert check
            assert command[0] == str(tools["appimagetool"])
            assert Path(cwd) == tools["appimagetool"].parent.parent
            assert Path(cwd).is_dir()
            assert tools["appimagetool"].read_bytes() == b"extracted-tool-contract"
            assert "--runtime-file" in command
            assert str(tools["runtime"]) in command
            assert "--mksquashfs-opt=-processors" in command
            assert "--mksquashfs-opt=1" in command
            assert not any("updateinformation" in argument for argument in command)
            assert env["SOURCE_DATE_EPOCH"] == str(epoch)
            assert "VERSION" not in env and "APPDIR" not in env
            appdir = Path(command[-2])
            assert (appdir / "usr/bin/c4-launcher").read_bytes() == launcher.read_bytes()
            assert (appdir / "usr/bin/c4-launcher").stat().st_mtime == epoch
            assert f"X-AppImage-Version=1.2.3\n" in (
                appdir / "citroen-c4.desktop").read_text()
            assert (appdir / "AppRun").stat().st_mode & 0o111
            Path(command[-1]).write_bytes(b"\x7fELF\x02\x01\x01\x00AI\x02" + b"contract")

        output = root / "result.AppImage"
        with patch.object(build, "acquire", side_effect=fake_acquire) as acquisition, \
                patch.object(build, "_require_platform"), \
                patch.object(build.subprocess, "run", side_effect=fake_packager) as invocation:
            assert build.create(stage, output, "1.2.3", epoch, root) == output
            assert not tools["appimagetool"].parent.parent.exists()
            acquisition.assert_called_once_with(
                root, names=("appimagetool", "runtime"),
                extraction_dir=tools["appimagetool"].parent)
            assert output.stat().st_mtime == epoch
            assert output.stat().st_mode & 0o111
            try:
                build.create(stage, output, "1.2.3", epoch, root)
            except FileExistsError:
                pass
            else:
                raise AssertionError("Existing artifact was overwritten")
            assert invocation.call_count == 1
        print("AppImage acquisition and build contracts passed")


if __name__ == "__main__":
    main()
