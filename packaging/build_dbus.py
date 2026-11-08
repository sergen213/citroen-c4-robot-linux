#!/usr/bin/env python3
"""Build unmodified authenticated D-Bus 1.16.2, library only, in a fresh rootless prefix.

python3 packaging/build_dbus.py --prefix work/tools/dbus-prefix --tools-dir work/tools --jobs 4
Export C4_DBUS_PREFIX explicitly; no host installation, service or bus changes.
Requires Linux x86_64 with modern glibc, gcc, Meson >=1.1, Ninja, pkg-config,
X11 development metadata, binutils and gpg. X11 autolaunch uses the HOST helper.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import tempfile

import appimage.acquire as acquisition
import build_ffmpeg
from appimage.acquire import _download, _sha256
from build_ffmpeg import command, extract_source, local_directory

LOCK = Path(__file__).with_name("dbus.lock.json")
RECIPE = Path(__file__).resolve()
METADATA = Path("share/citroen-c4-robot/dbus-source.json")
NOTICE_ROOT = Path("share/licenses/D-Bus")
PROOF_ROOT = NOTICE_ROOT / "source-proof"
CONFIGURATION_FILES = (
    "config.h", "dbus/dbus-arch-deps.h", "dbus/version_script",
    "meson-info/intro-buildoptions.json", "meson-info/intro-compilers.json",
    "meson-info/intro-machines.json", "meson-info/intro-dependencies.json",
    "meson-info/intro-targets.json", "compile_commands.json", "meson-logs/meson-log.txt",
)


def load_lock() -> dict:
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    if (lock.get("format") != 1 or lock.get("component") != "D-Bus"
            or lock.get("version") != "1.16.2" or lock.get("license") != "GPL-2.0-or-later"
            or lock.get("libraries") != [{"name": "dbus-1", "soname": "libdbus-1.so.3", "version": "3.38.3"}]):
        raise ValueError("Unsupported D-Bus source/license/ABI lock")
    helpers = {"build_ffmpeg.py": Path(build_ffmpeg.__file__),
               "appimage/acquire.py": Path(acquisition.__file__)}
    if {row["path"] for row in lock["recipe_helpers"]} != set(helpers):
        raise ValueError("D-Bus corresponding recipe helper set mismatch")
    for row in lock["recipe_helpers"]:
        if _sha256(helpers[row["path"]]) != row["sha256"]:
            raise ValueError("D-Bus corresponding recipe helper digest mismatch: " + row["path"])
    return lock


def acquire_source(tools_dir: Path) -> Path:
    """Acquire and verify official complete source; never execute source code."""
    lock = load_lock()
    cache = local_directory(Path(tools_dir)) / "dbus-source"
    if cache.is_symlink():
        raise ValueError("D-Bus source cache must not be a symlink")
    cache.mkdir(parents=True, exist_ok=True)
    source = _download(cache, lock["archive"], lock["url"], lock["source_sha256"])
    if source.stat().st_size != lock["source_size"]:
        raise ValueError("D-Bus source archive size mismatch")
    proof = lock["provenance"]
    for material in proof["materials"]:
        path = _download(cache, material["path"], material["url"], material["sha256"])
        if path.stat().st_size != material["size"]:
            raise ValueError("D-Bus authentication material size mismatch")
    authority = (cache / "Arch-PKGBUILD").read_text(encoding="utf-8")
    keys = re.search(r"validpgpkeys=\((.*?)\)", authority, re.S)
    if keys is None or proof["primary_key_fingerprint"] not in keys.group(1):
        raise ValueError("D-Bus signer differs from pinned source authority")
    gpg = shutil.which("gpg")
    if not gpg:
        raise RuntimeError("gpg is required to authenticate D-Bus source")
    with tempfile.TemporaryDirectory(dir=cache, prefix=".signature-") as temporary:
        home = Path(temporary)
        home.chmod(0o700)
        base = [gpg, "--homedir", str(home), "--batch", "--no-options", "--no-auto-key-retrieve"]
        command(base + ["--import", str(cache / "dbus-release-key.asc")], cache)
        status = command(base + ["--status-fd=1", "--verify", str(cache / (lock["archive"] + ".asc")),
                                 str(source)], cache, capture=True).decode("utf-8")
        valid = [line.split() for line in status.splitlines() if line.startswith("[GNUPG:] VALIDSIG ")]
        if (len(valid) != 1 or valid[0][2] != proof["key_fingerprint"]
                or valid[0][-1] != proof["primary_key_fingerprint"]
                or valid[0][4] != str(proof["signature_unix_time"])):
            raise ValueError("D-Bus release signature fingerprint/time mismatch")
    return source


def source_materials(tools_dir: Path) -> list[tuple[str, Path, str]]:
    source = acquire_source(tools_dir)
    lock = load_lock()
    return [("archive/" + lock["archive"], source, lock["source_sha256"])] + [
        ("proof/" + row["path"], source.parent / row["path"], row["sha256"])
        for row in lock["provenance"]["materials"]]


def license_contents(source: Path, lock: dict) -> dict[str, bytes]:
    files = {name: (source / name).read_bytes() for name in ("COPYING", "AUTHORS")}
    files.update({path.relative_to(source).as_posix(): path.read_bytes()
                  for path in sorted((source / "LICENSES").iterdir()) if path.is_file()})
    notices = [b"D-Bus source notices; complete unmodified source retains all notices, including disabled components.\n"]
    for path in sorted(source.rglob("*"), key=lambda p: p.relative_to(source).as_posix()):
        if not path.is_file() or not path.name.endswith((".c", ".h", ".cpp", ".in", ".py", ".sh", ".build", ".txt")):
            continue
        text = path.read_bytes().decode("utf-8", errors="replace")
        match = re.match(r"\s*(/\*.*?\*/)", text, re.S)
        if match:
            header = match.group(1)
        else:
            lines = []
            for line in text.splitlines():
                if line.startswith("#") or (lines and not line.strip()):
                    lines.append(line)
                else:
                    break
            header = "\n".join(lines).strip()
        if header:
            notices.append(("\n=== " + path.relative_to(source).as_posix() + " ===\n" + header + "\n").encode("utf-8"))
    files["SOURCE-NOTICES.txt"] = b"".join(notices)
    aggregate = (b"D-Bus 1.16.2; GPL-2.0-or-later election of the upstream dual license. Shared library only; "
                 b"complete corresponding source and authentication accompany the binary.\n\n")
    for name, data in sorted(files.items()):
        aggregate += ("=== " + name + " ===\n").encode("utf-8") + data + b"\n"
    files["LICENSE.txt"] = aggregate
    if set(files) != {row["path"] for row in lock["licenses"]}:
        raise ValueError("Unexpected D-Bus notice file set")
    for row in lock["licenses"]:
        if hashlib.sha256(files[row["path"]]).hexdigest() != row["sha256"]:
            raise ValueError("D-Bus license notice mismatch: " + row["path"])
    return files


def meson_arguments(prefix: Path, lock: dict | None = None) -> list[str]:
    """Exact configure option vector shared with controlled-prefix validation."""
    lock = load_lock() if lock is None else lock
    def option(value: object) -> str:
        if isinstance(value, bool):
            return str(value).lower()
        if isinstance(value, list):
            return "[" + ",".join(repr(item) for item in value) + "]"
        return str(value)
    return ["--prefix=" + str(prefix), "--wrap-mode=nodownload",
            *["-D" + key + "=" + option(value) for key, value in lock["meson_options"].items()]]


def check_configuration(proof_dir: Path, metadata: dict) -> None:
    lock = load_lock()
    config = (proof_dir / "config.h").read_text(encoding="utf-8")
    for key, value in lock["config_defines"].items():
        # Meson configuration_data.set(bool) emits presence-only #define;
        # upstream's explicit integer expressions must retain their literal 1.
        expected = "" if value is True else " " + (json.dumps(value) if isinstance(value, str) else str(value))
        if not re.search(rf"^#define {re.escape(key)}{re.escape(expected)}$", config, re.M):
            raise ValueError("Unexpected D-Bus configuration: " + key)
    for key in lock["forbidden_config_defines"]:
        definitions = re.findall(rf"^#define {re.escape(key)}(?:[ \t]+([^\r\n]*))?$", config, re.M)
        if any(value.strip() != "0" for value in definitions):
            raise ValueError("Unexpected D-Bus enabled feature: " + key)
    options = json.loads((proof_dir / "meson-info/intro-buildoptions.json").read_text(encoding="utf-8"))
    actual = {row["name"]: row["value"] for row in options}
    for key, value in lock["meson_options"].items():
        if actual.get(key) != value:
            raise ValueError("Unexpected D-Bus Meson option: " + key)
    if "prefix" in metadata and actual.get("prefix") != metadata["prefix"]:
        raise ValueError("D-Bus generated configuration prefix mismatch")
    if (metadata.get("meson_options") != lock["meson_options"]
            or metadata.get("default_bus_addresses") != lock["default_bus_addresses"]):
        raise ValueError("D-Bus metadata configuration policy mismatch")


def check_install(root: Path, lock: dict) -> list[dict]:
    library = lock["libraries"][0]
    full = "libdbus-1.so." + library["version"]
    binary = root / "lib" / full
    if binary.is_symlink() or not binary.is_file():
        raise ValueError("Missing regular D-Bus ABI library")
    for name in ("libdbus-1.so", library["soname"]):
        alias = root / "lib" / name
        if not alias.is_symlink() or alias.readlink() != Path(full):
            raise ValueError("D-Bus shared library alias mismatch")
    allowed = {"lib/" + name for name in (full, "libdbus-1.so", library["soname"])}
    allowed.add("lib/pkgconfig/dbus-1.pc")
    allowed.add("lib/dbus-1.0/include/dbus/dbus-arch-deps.h")
    allowed.update("include/dbus-1.0/dbus/" + name for name in lock["public_headers"])
    actual = {p.relative_to(root).as_posix() for directory in (root / "lib", root / "include")
              for p in directory.rglob("*") if not p.is_dir()}
    if actual != allowed or {p.name for p in root.iterdir()} != {"lib", "include"}:
        raise ValueError("Unexpected D-Bus installation; only library, public headers and pkg-config permitted")
    for path in allowed - {"lib/libdbus-1.so", "lib/" + library["soname"]}:
        if (root / path).is_symlink() or not (root / path).is_file():
            raise ValueError("D-Bus installation contains nonregular file")
    pc = (root / "lib/pkgconfig/dbus-1.pc").read_text(encoding="utf-8")
    if "Version: " + lock["version"] not in pc.splitlines():
        raise ValueError("D-Bus pkg-config version mismatch")
    try:
        dynamic = command(["readelf", "-d", str(binary)], root, capture=True).decode("utf-8")
        symbols = command(["nm", "-D", "--defined-only", str(binary)], root, capture=True).decode("utf-8")
        header = command(["readelf", "-h", str(binary)], root, capture=True).decode("utf-8")
    except subprocess.CalledProcessError as error:
        raise ValueError("Invalid D-Bus ELF library") from error
    if (re.findall(r"\(SONAME\).*?\[(.*?)\]", dynamic) != [library["soname"]]
            or sorted(re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic)) != lock["needed_libraries"]
            or re.search(r"\((?:RPATH|RUNPATH)\)", dynamic)
            or "ELF64" not in header or "Advanced Micro Devices X86-64" not in header):
        raise ValueError("D-Bus ELF ABI/dependency mismatch")
    public = {line.split()[-1] for line in symbols.splitlines() if line.split()[-1].startswith("dbus_")}
    expected = {name + "@@" + lock["abi"]["symbol_version"] for name in lock["required_symbols"]}
    if public != expected:
        raise ValueError("D-Bus complete public symbol ABI mismatch")
    data = binary.read_bytes()
    for value in lock["config_defines"].values():
        if isinstance(value, str) and value in (lock["default_bus_addresses"]["system"], "autolaunch:"):
            if value.encode() not in data:
                raise ValueError("D-Bus compiled default bus address mismatch")
    return [{**library, "path": "lib/" + full, "sha256": _sha256(binary)}]


def build(prefix: Path, tools_dir: Path, jobs: int) -> Path:
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        raise RuntimeError("D-Bus owned release recipe requires Linux x86_64")
    if jobs < 1:
        raise ValueError("D-Bus jobs must be positive")
    prefix = local_directory(prefix)
    cache = local_directory(tools_dir)
    if prefix.exists() or prefix.is_symlink():
        raise FileExistsError("D-Bus requires a fresh dedicated prefix: " + str(prefix))
    source_cache = cache / "dbus-source"
    if prefix == cache or prefix in cache.parents or prefix == source_cache or source_cache in prefix.parents:
        raise ValueError("D-Bus prefix overlaps tool/source cache")
    tools = {name: shutil.which(name) for name in ("gcc", "meson", "ninja", "pkg-config", "ar", "readelf", "nm", "python3")}
    if any(value is None for value in tools.values()):
        raise RuntimeError("Missing D-Bus build tools: " + ", ".join(name for name, value in tools.items() if value is None))
    lock = load_lock()
    archive = acquire_source(cache)
    prefix.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=prefix.parent, prefix=".dbus-build-") as temporary:
        work = Path(temporary)
        source = extract_source(archive, work / "source", lock["top_directory"])
        notices = license_contents(source, lock)
        build_dir = work / "build"
        installed = work / "install"
        tool_dirs = list(dict.fromkeys([str(Path(path).parent) for path in tools.values()]
                                      + os.defpath.split(os.pathsep)))
        env = {"PATH": os.pathsep.join(tool_dirs), "HOME": str(work), "LC_ALL": "C", "TZ": "UTC",
               "TMPDIR": str(work), "SOURCE_DATE_EPOCH": str(lock["provenance"]["signature_unix_time"]),
               "CC": tools["gcc"], "AR": tools["ar"], "PKG_CONFIG": tools["pkg-config"],
               "CFLAGS": " ".join(lock["meson_options"]["c_args"]), "LDFLAGS": "", "CPPFLAGS": ""}
        meson_args = meson_arguments(prefix, lock)
        args = [tools["meson"], "setup", str(build_dir), str(source), *meson_args]
        command(args, work, env)
        policy = {"prefix": str(prefix), "meson_options": lock["meson_options"],
                  "default_bus_addresses": lock["default_bus_addresses"]}
        check_configuration(build_dir, policy)
        # Build only the real library target; never run aggregate install or post-install hooks.
        full = "libdbus-1.so." + lock["libraries"][0]["version"]
        command([tools["ninja"], "-C", str(build_dir), "-j", str(jobs), "dbus/" + full], work, env)
        destinations = {"lib/" + full: build_dir / "dbus" / full,
                        "lib/pkgconfig/dbus-1.pc": build_dir / "meson-private/dbus-1.pc",
                        "lib/dbus-1.0/include/dbus/dbus-arch-deps.h": build_dir / "dbus/dbus-arch-deps.h"}
        destinations.update({"include/dbus-1.0/dbus/" + name: source / "dbus" / name for name in lock["public_headers"]})
        for relative, path in destinations.items():
            destination = installed / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
        for alias in ("libdbus-1.so", "libdbus-1.so.3"):
            (installed / "lib" / alias).symlink_to(full)
        libraries = check_install(installed, lock)
        for name, data in notices.items():
            destination = installed / NOTICE_ROOT / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        proof = installed / PROOF_ROOT
        proof.mkdir()
        for path in (LOCK, RECIPE, *[archive.parent / row["path"] for row in lock["provenance"]["materials"]]):
            shutil.copyfile(path, proof / path.name)
        configuration = []
        for name in CONFIGURATION_FILES:
            destination = proof / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(build_dir / name, destination)
            configuration.append({"path": destination.relative_to(installed).as_posix(), "sha256": _sha256(destination)})
        tool_versions = {name: command([path, "--version"], work, env, capture=True).decode("utf-8", errors="replace").splitlines()[0]
                         for name, path in tools.items()}
        # Exact existing system/toolchain paths observed in compiler/build proof,
        # never a wildcard permission for a host tree or any private/home path.
        system_paths = sorted({match.group(0) for name in CONFIGURATION_FILES
                               for match in re.finditer(
                                   r"/(?:usr/(?:lib|lib64|include|bin|share)|lib|lib64|bin)(?:/[A-Za-z0-9_+.@=-]+)*",
                                   (build_dir / name).read_text(encoding="utf-8", errors="replace"))
                               if Path(match.group(0)).exists()})
        system_paths = sorted(set(system_paths) | {directory for directory in tool_dirs
                              if Path(directory).is_dir() and any(
                                  Path(base) == Path(directory) or Path(base) in Path(directory).parents
                                  for base in ("/usr", "/bin", "/lib", "/lib64"))})
        metadata = {**lock, "prefix": str(prefix), "source_cache_path": str(archive),
                    "built_libraries": libraries, "meson_args": meson_args,
                    "configuration": configuration, "lock_sha256": _sha256(LOCK),
                    "recipe_sha256": _sha256(RECIPE), "proof": lock["provenance"],
                    "provenance_roots": {"source": str(source), "build": str(build_dir), "work": str(work), "prefix": str(prefix)},
                    "tool_paths": tools, "tool_versions": tool_versions, "build_environment": env,
                    "system_paths": system_paths,
                    "authentication_files": [{"path": (PROOF_ROOT / row["path"]).as_posix(), "sha256": row["sha256"]}
                                             for row in lock["provenance"]["materials"]]}
        check_configuration(proof, metadata)
        output = installed / METADATA
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if prefix.exists() or prefix.is_symlink():
            raise FileExistsError(prefix)
        installed.rename(prefix)
    return prefix


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--tools-dir", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()
    def terminate(*_: object) -> None:
        raise KeyboardInterrupt
    previous = signal.signal(signal.SIGTERM, terminate)
    try:
        print("C4_DBUS_PREFIX=" + str(build(args.prefix, args.tools_dir, args.jobs)))
    finally:
        signal.signal(signal.SIGTERM, previous)


if __name__ == "__main__":
    main()
