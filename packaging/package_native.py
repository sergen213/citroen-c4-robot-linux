#!/usr/bin/env python3
"""Stage the native CMake install and create a reproducible signed Linux package."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile

from runtime_bundle import allowed_runtime_path, deploy

EXECUTABLE_PATHS = frozenset({
    "bin/citroen-c4-robot", "bin/c4-launcher", "bin/c4-updater",
    "tools/innoextract", "runtime/bin/python3",
})

BINARY_PATHS = frozenset({
    "bin/citroen-c4-robot", "bin/c4-launcher", "bin/c4-updater",
})
# Exact project-owned install paths: never package an arbitrary share subtree.
SHARE_PATHS = frozenset({
    "share/citroen-c4-robot/tools/import_assets.py",
    "share/citroen-c4-robot/tools/original-assets.json",
    "share/doc/citroen-c4-robot/README.md",
    "share/doc/citroen-c4-robot/docs/PROGRESS.md",
    "share/doc/citroen-c4-robot/docs/REVERSE_ENGINEERING.md",
    "share/doc/citroen-c4-robot/docs/ENGINE_ARCHITECTURE.md",
    "share/doc/citroen-c4-robot/docs/GRAPHICS_ROADMAP.md",
    "share/doc/citroen-c4-robot/docs/KNOWN_ISSUES.md",
    "share/doc/citroen-c4-robot/docs/FUNCTION_MAP.md",
})
ALLOWED_PATHS = BINARY_PATHS | SHARE_PATHS
AUTHORED_SHADER_PATHS = frozenset(
    f"share/citroen-c4-robot/shaders/vulkan/{name}"
    for name in ("raster.vert.spv", "raster.frag.spv", "compose.frag.spv",
                 "rt.comp.spv", "temporal.comp.spv", "fullscreen.vert.spv"))
MAX_FILE = 128 * 1024 * 1024
MAX_TOTAL = 1024 * 1024 * 1024
MAX_PACKAGE = 512 * 1024 * 1024
PACKAGE_NAME = "native-linux-x86_64.tar.gz"
ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")


def validate_version(version):
    number = r"(?:0|[1-9][0-9]{0,8})"
    if len(version) > 128 or not re.fullmatch(rf"{number}\.{number}\.{number}(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?", version):
        raise ValueError("version must be SemVer without build metadata")
    if "-" in version:
        for identifier in version.split("-", 1)[1].split("."):
            if identifier.isascii() and identifier.isdigit():
                if len(identifier) > 1 and identifier[0] == "0":
                    raise ValueError("numeric prerelease identifiers cannot have leading zeros")
                if int(identifier) > 18446744073709551615:
                    raise ValueError("numeric prerelease identifiers must fit uint64")
    return version


def run(command, **kwargs):
    return subprocess.run(command, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, **kwargs).stdout


def public_key(signing_key):
    # Parsing private PEM and checking exact Ed25519 SPKI avoids algorithm-name text parsing.
    encoded = run(["openssl", "pkey", "-in", str(signing_key), "-passin", "pass:",
                   "-pubout", "-outform", "DER"])
    if len(encoded) != 44 or not encoded.startswith(ED25519_SPKI_PREFIX):
        raise ValueError("signing key must be an unencrypted Ed25519 private PEM key")
    return encoded


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(chunk)
    return checksum.hexdigest()


def collect_files(stage, runtime_paths=()):
    runtime_paths = frozenset(runtime_paths)
    if any(not allowed_runtime_path(name) for name in runtime_paths):
        raise ValueError("runtime deployment contains an unauthorized path")
    allowed_paths = ALLOWED_PATHS | AUTHORED_SHADER_PATHS | runtime_paths
    allowed_dirs = {str(parent) for name in allowed_paths
                    for parent in Path(name).parents if str(parent) != "."}
    files = []
    total = 0

    def visit(directory):
        nonlocal total
        for entry in sorted(os.scandir(directory), key=lambda item: item.name):
            path = Path(entry.path)
            relative = path.relative_to(stage).as_posix()
            info = entry.stat(follow_symlinks=False)
            if stat.S_ISDIR(info.st_mode):
                if relative not in allowed_dirs:
                    raise ValueError(f"unlisted install directory: {relative}")
                visit(path)
                continue
            if not stat.S_ISREG(info.st_mode):
                raise ValueError(f"links and special install entries are forbidden: {relative}")
            if relative not in allowed_paths:
                raise ValueError(f"unlisted install file: {relative}")
            if info.st_size > MAX_FILE:
                raise ValueError(f"install file exceeds 128 MiB: {relative}")
            total += info.st_size
            if total > MAX_TOTAL:
                raise ValueError("installed files exceed 1 GiB")
            if relative in EXECUTABLE_PATHS:
                with path.open("rb") as stream:
                    header = stream.read(20)
                if (len(header) != 20 or header[:6] != b"\x7fELF\x02\x01"
                        or header[18:20] != b"\x3e\x00"):
                    raise ValueError(f"native binary is not Linux x86_64 ELF: {relative}")
            files.append({"path": relative, "size": info.st_size, "sha256": digest(path)})

    visit(stage)
    missing = BINARY_PATHS - {item["path"] for item in files}
    if missing:
        raise ValueError("required native binaries missing: " + ", ".join(sorted(missing)))
    if not SHARE_PATHS <= {item["path"] for item in files}:
        raise ValueError("required project install files missing")
    if runtime_paths - {item["path"] for item in files}:
        raise ValueError("required runtime files missing")
    shader_files = AUTHORED_SHADER_PATHS & {item["path"] for item in files}
    if shader_files and shader_files != AUTHORED_SHADER_PATHS:
        raise ValueError("installed authored Vulkan shader set is incomplete")
    if shader_files and not any(item["path"] == "lib/libvulkan.so.1" for item in files):
        raise ValueError("full Vulkan shader installation requires its bundled native loader")
    return sorted(files, key=lambda item: item["path"])


def create_archive(stage, files, destination, epoch):
    with destination.open("xb") as output:
        with gzip.GzipFile(filename="", fileobj=output, mode="wb", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                for item in files:
                    header = tarfile.TarInfo(item["path"])
                    header.size = item["size"]
                    header.mode = 0o755 if item["path"] in EXECUTABLE_PATHS else 0o644
                    header.uid = header.gid = 0
                    header.uname = header.gname = ""
                    header.mtime = epoch
                    with (stage / item["path"]).open("rb") as source:
                        archive.addfile(header, source)
    if destination.stat().st_size > MAX_PACKAGE:
        raise ValueError("compressed package exceeds 512 MiB")



def stage_runtime(stage, tools_dir):
    from appimage.acquire import acquire
    with tempfile.TemporaryDirectory(prefix="c4-native-tools-") as extraction:
        tools = acquire(tools_dir, names=("innoextract", "patchelf"),
                        extraction_dir=Path(extraction))
        environment_path = os.environ.get("PATH", "")
        os.environ["PATH"] = str(tools["patchelf"].parent) + os.pathsep + environment_path
        try:
            for name in sorted(BINARY_PATHS):
                run([str(tools["patchelf"]), "--set-rpath", "$ORIGIN/../lib", str(stage / name)])
            paths = deploy(stage, tools["innoextract"].parents[2],
                           dependency_sources_dir=Path(tools_dir) / "dependency-sources")
        finally:
            os.environ["PATH"] = environment_path
    required = {
        "tools/innoextract", "runtime/bin/python3", "bin/qt.conf",
        "share/citroen-c4-robot/runtime-manifest.json", "plugins/platforms/libqxcb.so",
        "plugins/tls/libqopensslbackend.so",
    }
    if not required <= set(paths):
        raise ValueError("runtime bundle is missing required launcher/importer components")
    if sum(bool(re.fullmatch(r"runtime/lib/python3(?:11|12|13|14)\.zip", name))
           for name in paths) != 1:
        raise ValueError("runtime bundle must have exactly one supported Python stdlib")
    if not any(name in paths for name in (
            "plugins/platforms/libqwayland.so", "plugins/platforms/libqwayland-egl.so",
            "plugins/platforms/libqwayland-generic.so")):
        raise ValueError("runtime bundle is missing its Wayland platform plugin")
    return paths

def package(build_dir, output, version, signing_key, epoch, tools_dir=None, appimage=False):
    validate_version(version)
    if sys.platform != "linux" or platform.machine() not in {"x86_64", "AMD64"}:
        raise ValueError("only Linux x86_64 native packaging is supported")
    if not 0 <= epoch <= 0o77777777777:
        raise ValueError("SOURCE_DATE_EPOCH is outside the USTAR timestamp range")
    if appimage and epoch > 0xFFFFFFFF:
        raise ValueError("SOURCE_DATE_EPOCH is outside the AppImage squashfs timestamp range")
    # Reject even a dangling symlink or empty output directory; never overwrite artifacts.
    if os.path.lexists(output):
        raise FileExistsError(f"output must be a fresh nonexistent directory: {output}")
    key = public_key(signing_key)
    output.mkdir(parents=False, exist_ok=False)
    try:
        with tempfile.TemporaryDirectory(prefix="c4-native-package-") as temporary:
            temporary = Path(temporary)
            stage = temporary / "stage"
            stage.mkdir()
            environment = os.environ.copy()
            environment.pop("DESTDIR", None)
            run(["cmake", "--install", str(build_dir), "--prefix", str(stage)], env=environment)
            runtime_paths = stage_runtime(stage, tools_dir or Path("work/tools").absolute())
            from appimage.source_bundle import create as create_sources
            sources = output / "runtime-corresponding-source.tar.gz"
            runtime_manifest = json.loads(
                (stage / "share/citroen-c4-robot/runtime-manifest.json").read_bytes())
            create_sources(sources, epoch, tools_dir or Path("work/tools").absolute(),
                           runtime_manifest=runtime_manifest, include_appimage_runtime=appimage)
            files = collect_files(stage, runtime_paths)
            for item in files:
                (stage / item["path"]).chmod(0o755 if item["path"] in EXECUTABLE_PATHS else 0o644)
            archive = output / PACKAGE_NAME
            create_archive(stage, files, archive, epoch)
            manifest = {
                "schema_version": 1, "version": version, "os": "linux", "arch": "x86_64",
                "package": {"name": PACKAGE_NAME, "size": archive.stat().st_size,
                            "sha256": digest(archive)},
                "files": files,
            }
            manifest_path = output / "manifest.json"
            manifest_path.write_bytes((json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8"))
            signature = output / "manifest.sig"
            run(["openssl", "pkeyutl", "-sign", "-rawin", "-inkey", str(signing_key),
                 "-passin", "pass:", "-in", str(manifest_path), "-out", str(signature)])
            if signature.stat().st_size != 64:
                raise ValueError("Ed25519 detached signature must be exactly 64 bytes")
            public_path = temporary / "public.der"
            public_path.write_bytes(key)
            run(["openssl", "pkeyutl", "-verify", "-rawin", "-pubin", "-keyform", "DER",
                 "-inkey", str(public_path), "-in", str(manifest_path), "-sigfile", str(signature)])
            artifacts = [sources]
            if appimage:
                from appimage.build import create
                image = output / f"Citroen-C4-Robot-{version}-x86_64.AppImage"
                create(stage, image, version, epoch, tools_dir or Path("work/tools").absolute())
                artifacts.append(image)
            for artifact in artifacts:
                artifact_signature = output / (artifact.name + ".sig")
                run(["openssl", "pkeyutl", "-sign", "-rawin", "-inkey", str(signing_key),
                     "-passin", "pass:", "-in", str(artifact), "-out", str(artifact_signature)])
                if artifact_signature.stat().st_size != 64:
                    raise ValueError("artifact Ed25519 signature must be exactly 64 bytes")
                run(["openssl", "pkeyutl", "-verify", "-rawin", "-pubin", "-keyform", "DER",
                     "-inkey", str(public_path), "-in", str(artifact),
                     "-sigfile", str(artifact_signature)])
            shutil.copyfile(stage / "share/citroen-c4-robot/runtime-manifest.json",
                            output / "runtime-manifest.json")
    except BaseException:
        shutil.rmtree(output)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="fresh nonexistent output directory")
    parser.add_argument("--version", required=True)
    parser.add_argument("--signing-key", type=Path, required=True, help="unencrypted Ed25519 private PEM")
    parser.add_argument("--tools-dir", type=Path, default=Path("work/tools"),
                        help="SHA-256-verified upstream tool download cache")
    args = parser.parse_args()
    try:
        epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
        package(args.build_dir.resolve(), args.output.absolute(), args.version,
                args.signing_key.resolve(strict=True), epoch, args.tools_dir.absolute(), appimage=True)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"packaging failed: {error}\n")
    print(f"Signed native package written to {args.output}")


if __name__ == "__main__":
    main()
