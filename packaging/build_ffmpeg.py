#!/usr/bin/env python3
"""Build the authenticated, LGPL-only WMV3/ASF decoder in a fresh rootless prefix.

Run: python3 packaging/build_ffmpeg.py --prefix work/tools/ffmpeg-prefix --tools-dir work/tools
Export C4_FFMPEG_PREFIX to that prefix and prepend its lib/pkgconfig to PKG_CONFIG_PATH.
acquire_source(tools_dir) preserves byte-exact corresponding source and official proof for
release publishers; it does not execute source. Redistribute that archive, this recipe and
ffmpeg.lock.json as the corresponding-source supplement, not original game media.
Requires Linux x86_64, Python 3.11+, gcc, make, nasm, binutils and gpg. No sudo or installer.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import signal
import subprocess
import tarfile
import tempfile

from appimage.acquire import _download, _sha256

LOCK = Path(__file__).with_name("ffmpeg.lock.json")
METADATA = Path("share/citroen-c4-robot/ffmpeg-source.json")
NOTICE_ROOT = Path("share/licenses/FFmpeg")
NOTICE_DIRS = {"libavcodec", "libavformat", "libavutil", "libswscale", "compat", "ffbuild"}
NOTICE_SUFFIXES = (".c", ".h", ".asm", ".inc", ".S", ".sh", ".mak")


def load_lock() -> dict:
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    if lock.get("format") != 1 or lock["license"] != "LGPL-2.1-or-later":
        raise ValueError("Unsupported FFmpeg source/license lock")
    return lock


def local_directory(path: Path) -> Path:
    """Reject system installation/cache targets, including symlinked parent escapes."""
    path = path.absolute()
    if path.is_symlink():
        raise ValueError(f"Directory must not be a symlink: {path}")
    path = path.resolve()
    forbidden = ("/usr", "/bin", "/sbin", "/lib", "/lib64", "/etc", "/boot", "/dev", "/proc", "/sys", "/var", "/run", "/opt")
    if path == Path("/") or any(path == Path(p) or Path(p) in path.parents for p in forbidden):
        raise ValueError(f"Refusing a system directory: {path}")
    return path


def command(args: list[str], cwd: Path, env: dict | None = None, capture: bool = False) -> bytes:
    """Own/reap the whole build group before temporary staging can be removed."""
    child = subprocess.Popen(args, cwd=cwd, env=env, start_new_session=True,
                             stdout=subprocess.PIPE if capture else None)
    try:
        stdout, _ = child.communicate()
        if child.returncode:
            raise subprocess.CalledProcessError(child.returncode, args)
        return stdout or b""
    finally:
        try:
            if child.poll() is None:
                try:
                    os.killpg(child.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    child.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(child.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
            # Descendants may outlive a terminated shell; never leave their build running.
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        finally:
            child.wait()


def acquire_source(tools_dir: Path) -> Path:
    """Authenticate the preserved official source; no configure or compilation."""
    lock = load_lock()
    cache = local_directory(Path(tools_dir)) / "ffmpeg-source"
    if cache.is_symlink():
        raise ValueError("FFmpeg cache must not be a symlink")
    cache.mkdir(parents=True, exist_ok=True)
    source = _download(cache, lock["archive"], lock["url"], lock["source_sha256"])
    if source.stat().st_size != lock["source_size"]:
        raise ValueError("FFmpeg source archive size mismatch")
    proof = lock["provenance"]
    signature = _download(cache, lock["archive"] + ".asc", proof["signature_url"], proof["signature_sha256"])
    key = _download(cache, "ffmpeg-devel.asc", proof["key_url"], proof["key_sha256"])
    gpg = shutil.which("gpg")
    if not gpg:
        raise RuntimeError("gpg is required to authenticate the official FFmpeg release")
    with tempfile.TemporaryDirectory(dir=cache, prefix=".signature-") as temporary:
        home = Path(temporary)
        home.chmod(0o700)
        base = [gpg, "--homedir", str(home), "--batch", "--no-options", "--no-auto-key-retrieve"]
        command(base + ["--import", str(key)], cache)
        status = command(base + ["--status-fd=1", "--verify", str(signature), str(source)], cache, capture=True)
        valid = [line.split()[2] for line in status.decode("utf-8").splitlines()
                 if line.startswith("[GNUPG:] VALIDSIG ")]
        if valid != [proof["key_fingerprint"]]:
            raise ValueError("FFmpeg release signature fingerprint mismatch")
    return source


def extract_source(archive: Path, destination: Path, top_directory: str) -> Path:
    """Extract only bounded regular files/directories; never links, devices or escapes."""
    with tarfile.open(archive, "r:xz") as source:
        members = source.getmembers()
        if len(members) > 20000 or sum(m.size for m in members) > 256 * 1024 * 1024:
            raise ValueError("FFmpeg source exceeds extraction bounds")
        seen = set()
        for member in members:
            relative = PurePosixPath(member.name)
            if (relative.is_absolute() or ".." in relative.parts or not relative.parts
                    or relative.parts[0] != top_directory
                    or not (member.isdir() or member.isfile()) or relative in seen):
                raise ValueError(f"Unsafe FFmpeg source member: {member.name}")
            seen.add(relative)
        for member in members:
            target = destination.joinpath(*PurePosixPath(member.name).parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as input_stream, target.open("xb") as output:
                    shutil.copyfileobj(input_stream, output)
                target.chmod(0o755 if member.mode & 0o111 else 0o644)
    return destination / top_directory


def source_notices(source: Path) -> bytes:
    parts = [b"FFmpeg source copyright notices (including disabled source components).\nThe accompanying byte-exact source archive preserves all original notices.\n"]
    for path in sorted(source.rglob("*"), key=lambda p: p.relative_to(source).as_posix()):
        name = path.relative_to(source).as_posix()
        if name.split("/")[0] not in NOTICE_DIRS or not name.endswith(NOTICE_SUFFIXES) or not path.is_file():
            continue
        text = path.read_bytes().decode("utf-8", errors="replace")
        match = re.match(r"\s*(/\*.*?\*/)", text, re.S)
        if match:
            header = match.group(1)
        else:
            lines = []
            for line in text.splitlines():
                if line.startswith((";", "#")) or (lines and not line.strip()):
                    lines.append(line)
                else:
                    break
            header = "\n".join(lines).strip()
        if header:
            parts.append(("\n=== " + name + " ===\n" + header + "\n").encode("utf-8"))
    return b"".join(parts)


def license_contents(source: Path, lock: dict) -> dict[str, bytes]:
    files = {name: (source / name).read_bytes() for name in ("COPYING.LGPLv2.1", "LICENSE.md", "CREDITS")}
    files["SOURCE-NOTICES.txt"] = source_notices(source)
    aggregate = (f"FFmpeg {lock['version']} - LGPL-2.1-or-later minimal WMV3/ASF decoder build.\n"
                 "No GPL, nonfree, external codecs or network support are enabled.\n"
                 "Unmodified corresponding source and release authentication are supplied separately.\n\n").encode("utf-8")
    for name, data in files.items():
        aggregate += ("=== " + name + " ===\n").encode("utf-8") + data + b"\n"
    files["LICENSE.txt"] = aggregate
    for record in lock["licenses"]:
        data = files[Path(record["path"]).name]
        if hashlib.sha256(data).hexdigest() != record["sha256"]:
            raise ValueError(f"Authenticated FFmpeg notice digest mismatch: {record['path']}")
    return files


def check_configuration(source: Path) -> None:
    config = (source / "config.h").read_text(encoding="utf-8")
    for name, enabled in {"GPL": 0, "NONFREE": 0, "VERSION3": 0, "NETWORK": 0,
                          "AVCODEC": 1, "AVFORMAT": 1, "AVUTIL": 1, "SWSCALE": 1,
                          "AVDEVICE": 0, "AVFILTER": 0, "SWRESAMPLE": 0}.items():
        if not re.search(rf"^#define CONFIG_{name} {enabled}$", config, re.M):
            raise ValueError(f"Unexpected FFmpeg configuration: {name}")


def check_install(root: Path, lock: dict) -> list[dict]:
    allowed = set()
    records = []
    for library in lock["libraries"]:
        base = "lib" + library["name"]
        full = base + ".so." + library["version"]
        allowed.update((base + ".so", library["soname"], full, "pkgconfig/" + base + ".pc"))
        binary = root / "lib" / full
        if binary.is_symlink() or not binary.is_file():
            raise ValueError(f"Missing regular FFmpeg library: {binary}")
        for alias in (base + ".so", library["soname"]):
            link = root / "lib" / alias
            if not link.is_symlink() or link.resolve() != binary.resolve():
                raise ValueError(f"Invalid FFmpeg shared-library alias: {link}")
        pc = (root / "lib/pkgconfig" / (base + ".pc")).read_text(encoding="utf-8")
        if f"Version: {library['version']}" not in pc.splitlines():
            raise ValueError(f"FFmpeg pkg-config version mismatch: {base}")
        records.append({**library, "path": "lib/" + full, "sha256": _sha256(binary)})
    actual = {p.relative_to(root / "lib").as_posix() for p in (root / "lib").rglob("*") if not p.is_dir()}
    if actual != allowed:
        raise ValueError(f"Unexpected FFmpeg library/metadata installation: {sorted(actual ^ allowed)}")
    if {p.name for p in root.iterdir()} != {"lib", "include"}:
        raise ValueError("FFmpeg installed unexpected programs, data or documentation")
    return records


def build(prefix: Path, tools_dir: Path) -> Path:
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        raise RuntimeError("FFmpeg release recipe requires Linux x86_64")
    prefix = local_directory(prefix)
    cache = local_directory(tools_dir)
    if prefix.exists() or prefix.is_symlink():
        raise FileExistsError(f"FFmpeg requires a fresh dedicated prefix: {prefix}")
    source_cache = cache / "ffmpeg-source"
    if prefix == cache or prefix in cache.parents or prefix == source_cache or source_cache in prefix.parents:
        raise ValueError("FFmpeg prefix must not overlap the source cache or contain the tool cache")
    tools = {}
    for name in ("gcc", "make", "nasm", "ar", "ranlib", "strip", "nm"):
        tools[name] = shutil.which(name)
        if not tools[name]:
            raise RuntimeError(f"Required FFmpeg build tool is missing: {name}")
    lock = load_lock()
    archive = acquire_source(cache)
    prefix.parent.mkdir(parents=True, exist_ok=True)
    # Same filesystem as the prefix makes the completed installation an atomic rename.
    with tempfile.TemporaryDirectory(dir=prefix.parent, prefix=".ffmpeg-build-") as temporary:
        work = Path(temporary)
        source = extract_source(archive, work, lock["top_directory"])
        notices = license_contents(source, lock)  # Verify before executing configure.
        args = [str(source / "configure"), "--prefix=" + str(prefix),
                "--libdir=" + str(prefix / "lib"), "--shlibdir=" + str(prefix / "lib"),
                "--pkgconfigdir=" + str(prefix / "lib/pkgconfig"), *lock["configure_args"],
                "--cc=" + tools["gcc"], "--ld=" + tools["gcc"], "--ar=" + tools["ar"],
                "--ranlib=" + tools["ranlib"], "--strip=" + tools["strip"], "--nm=" + tools["nm"],
                "--x86asmexe=" + tools["nasm"], "--pkg-config=false"]
        # Do not inherit host CFLAGS/CPPFLAGS/LDFLAGS, native CPU flags or package discovery.
        env = {"PATH": os.environ.get("PATH", os.defpath), "HOME": str(work),
               "LC_ALL": "C", "TZ": "UTC", "TMPDIR": str(work),
               "SOURCE_DATE_EPOCH": str(lock["provenance"]["signature_unix_time"])}
        command(args, source, env)
        check_configuration(source)
        command([tools["make"], "-j" + str(os.cpu_count() or 1)], source, env)
        stage = work / "install"
        # Upstream's aggregate install always copies example sources, even with --disable-doc.
        # Library header targets also install the four pkg-config files.
        command([tools["make"], "install-libs", "install-headers", "DESTDIR=" + str(stage)], source, env)
        installed = stage / prefix.relative_to("/")
        libraries = check_install(installed, lock)
        for name, data in notices.items():
            output = installed / NOTICE_ROOT / name
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(data)
        proof = installed / NOTICE_ROOT / "source-proof"
        proof.mkdir()
        for name in (lock["archive"] + ".asc", "ffmpeg-devel.asc"):
            shutil.copyfile(archive.parent / name, proof / name)
        shutil.copyfile(LOCK, proof / LOCK.name)
        configuration = []
        for name in ("config.h", "config_components.h", "ffbuild/config.mak", "ffbuild/config.sh"):
            destination = proof / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / name, destination)
            configuration.append({"path": destination.relative_to(installed).as_posix(),
                                  "sha256": _sha256(destination)})
        metadata = {"format": 1, "component": "FFmpeg", "version": lock["version"],
                    "url": lock["url"], "source_sha256": lock["source_sha256"],
                    "license": lock["license"], "licenses": lock["licenses"],
                    "libraries": lock["libraries"], "built_libraries": libraries,
                    "source_cache_path": str(archive), "prefix": str(prefix),
                    "configure_args": args[1:], "lock_sha256": _sha256(LOCK),
                    "proof": lock["provenance"], "source_terms": lock["source_terms"],
                    "configuration": configuration,
                    "provenance_roots": {"source": str(source), "build": str(source),
                                         "work": str(work), "prefix": str(prefix)},
                    "tool_paths": {"cc": tools["gcc"], "ld": tools["gcc"],
                                   "ar": tools["ar"], "ranlib": tools["ranlib"],
                                   "strip": tools["strip"], "nm": tools["nm"],
                                   "x86asmexe": tools["nasm"]}}
        output = installed / METADATA
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if prefix.exists() or prefix.is_symlink():
            raise FileExistsError(prefix)
        installed.rename(prefix)
    return prefix


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--tools-dir", required=True, type=Path)
    arguments = parser.parse_args()
    def terminate(*_: object) -> None:
        raise KeyboardInterrupt
    previous = signal.signal(signal.SIGTERM, terminate)
    try:
        print("C4_FFMPEG_PREFIX=" + str(build(arguments.prefix, arguments.tools_dir)))
    finally:
        signal.signal(signal.SIGTERM, previous)


if __name__ == "__main__":
    main()
