#!/usr/bin/env python3
"""Acquire digest-pinned native tools; never execute a mutable extracted cache."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

LOCK = Path(__file__).with_name("tools.lock.json")


def _require_platform() -> None:
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        raise RuntimeError("Release tooling supports only Linux x86_64")


def _sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def _download(directory: Path, name: str, url: str, digest: str) -> Path:
    destination = directory / name
    if destination.is_symlink():
        raise ValueError(f"Tool cache must not contain symlinks: {destination}")
    if destination.exists():
        if not destination.is_file() or _sha256(destination) != digest:
            raise ValueError(f"Tool cache digest mismatch: {destination}")
        return destination
    request = urllib.request.Request(url, headers={
        "Accept": "application/octet-stream",
        "User-Agent": "CitroenC4Robot-release-packaging",
    })
    with tempfile.NamedTemporaryFile(dir=directory, prefix=".download-", delete=False) as stream:
        temporary = Path(stream.name)
        try:
            with urllib.request.urlopen(request, timeout=90) as response:
                if not response.url.startswith("https://"):
                    raise ValueError("Tool downloads must remain on HTTPS")
                shutil.copyfileobj(response, stream)
            stream.flush()
            if _sha256(temporary) != digest:
                raise ValueError(f"Upstream tool digest mismatch: {name}")
            # A concurrent acquisition must not replace an already-present cache file.
            os.link(temporary, destination)
        finally:
            temporary.unlink(missing_ok=True)
    return destination


def _replace_tree(source: Path, destination: Path) -> None:
    if destination.is_symlink():
        raise ValueError(f"Extracted cache must not be a symlink: {destination}")
    if destination.exists():
        shutil.rmtree(destination)
    source.rename(destination)


def acquire(tools_dir: Path, names: tuple[str, ...] = (
    "appimagetool", "runtime", "innoextract", "patchelf",
), extraction_dir: Path | None = None) -> dict[str, Path]:
    """Return tool executable paths after digest checks and fresh extraction.

    Official immutable GitHub asset URLs use the octet-stream API media type.
    Existing extracted directories are rebuilt from verified originals every time.
    Callers may isolate extracted tools in their own workspace while sharing downloads.
    """
    _require_platform()
    specifications = json.loads(LOCK.read_text(encoding="utf-8"))["tools"]
    if len(set(names)) != len(names) or any(name not in specifications for name in names):
        raise ValueError("Unknown or duplicate requested tool")
    tools_dir = tools_dir.resolve()
    tools_dir.mkdir(parents=True, exist_ok=True)
    extraction_dir = tools_dir if extraction_dir is None else extraction_dir.resolve()
    extraction_dir.mkdir(parents=True, exist_ok=True)
    paths = {}
    for name in names:
        specification = specifications[name]
        archive = _download(tools_dir, specification["name"], specification["url"],
                            specification["sha256"])
        if name == "runtime":
            archive.chmod(0o755)
            paths[name] = archive
        elif name == "appimagetool":
            archive.chmod(0o755)
            extracted = extraction_dir / f"appimagetool-{specification['sha256']}"
            if extracted.is_symlink():
                raise ValueError(f"Extracted cache must not be a symlink: {extracted}")
            with tempfile.TemporaryDirectory(dir=extraction_dir, prefix=".extract-appimage-") as work:
                root = Path(work)
                subprocess.run([str(archive), "--appimage-extract"], cwd=root,
                               stdout=subprocess.DEVNULL, check=True)
                if not (root / "squashfs-root/AppRun").is_file():
                    raise RuntimeError("Official appimagetool extraction has no AppRun")
                # The containing directory is stable; replace only this tool's own tree.
                extracted.mkdir(exist_ok=True)
                _replace_tree(root / "squashfs-root", extracted / "squashfs-root")
            paths[name] = extracted / "squashfs-root/AppRun"
        elif name == "patchelf":
            with tempfile.TemporaryDirectory(dir=extraction_dir, prefix=".extract-patchelf-") as work:
                root = Path(work) / "patchelf-0.19.2-x86_64"
                root.mkdir()
                with tarfile.open(archive, "r:gz") as source:
                    source.extractall(root, filter="data")
                binary = root / "bin/patchelf"
                if binary.is_symlink() or not binary.is_file():
                    raise RuntimeError("Pinned patchelf archive has no regular native binary")
                _replace_tree(root, extraction_dir / root.name)
            paths[name] = extraction_dir / specification["binary_path"]
        else:
            proof = specification["provenance"]
            _download(tools_dir, specification["name"] + ".sig", proof["signature_url"],
                      proof["signature_sha256"])
            _download(tools_dir, "dscharrer.asc", proof["key_url"], proof["key_sha256"])
            with tempfile.TemporaryDirectory(dir=extraction_dir, prefix=".extract-inno-") as work:
                with tarfile.open(archive, "r:xz") as source:
                    source.extractall(work, filter="data")
                root = Path(work) / "innoextract-1.9-linux"
                binary = Path(work) / specification["binary_path"]
                if binary.is_symlink() or not binary.is_file():
                    raise RuntimeError("Pinned innoextract archive has no regular amd64 binary")
                for relative in specification["licenses"]:
                    license_path = Path(work) / relative
                    if license_path.is_symlink() or not license_path.is_file():
                        raise RuntimeError(f"Pinned extractor has no regular license: {relative}")
                _replace_tree(root, extraction_dir / root.name)
            paths[name] = extraction_dir / specification["binary_path"]
    return paths


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools-dir", type=Path, default=Path("work/tools"))
    arguments = parser.parse_args()
    print(json.dumps({name: str(path) for name, path in acquire(arguments.tools_dir).items()},
                     indent=2, sort_keys=True))
