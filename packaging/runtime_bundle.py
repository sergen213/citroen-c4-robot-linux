#!/usr/bin/env python3
"""Deploy the bounded Linux/x86_64 runtime; never copy assets or host drivers."""
import ast
import ctypes.util
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import stat
import subprocess
import sys
import sysconfig
import tarfile
import tempfile
import zipfile

MAX_FILE = 128 * 1024 * 1024
MAX_TOTAL = 1024 * 1024 * 1024
MAX_PATH = 240
_POLICY = json.loads((Path(__file__).parent / "runtime_policy.json").read_text(encoding="utf-8"))
PYTHON_MINORS = frozenset(_POLICY["python_minors"])
LIBRARY_BASES = frozenset(_POLICY["library_bases"])
EXTENSION_MODULES = frozenset(_POLICY["python_modules"])
PLUGIN_PATHS = frozenset(_POLICY["plugins"])
LICENSE_NAMES = frozenset(_POLICY["license_names"])
_PYTHON_VERSION = "(?:" + "|".join(map(str, sorted(PYTHON_MINORS))) + ")"
INNO_LICENSES = (
    "Boost.LICENSE_1_0", "innoextract.LICENSE", "libbz2.LICENSE",
    "liblzma.COPYING", "libstdc++.COPYING.RUNTIME", "libstdc++.COPYING3",
    "zlib.copyright",
)
FIXED_PATHS = frozenset({
    "tools/innoextract", "runtime/bin/python3", "bin/qt.conf",
    "share/citroen-c4-robot/runtime-manifest.json",
}) | PLUGIN_PATHS
VULKAN_SHADER_PATHS = frozenset(
    f"share/citroen-c4-robot/shaders/vulkan/{name}"
    for name in ("raster.vert.spv", "raster.frag.spv", "compose.frag.spv",
                 "rt.comp.spv", "temporal.comp.spv", "fullscreen.vert.spv")
)
SDL_BACKENDS = (
    "SDL3", "wayland-client", "wayland-cursor", "wayland-egl", "xkbcommon",
    "X11", "Xrandr", "Xcursor", "Xi", "Xfixes", "Xext", "Xss",
    "asound", "pulse-simple", "pulse",
)
HOST_BASES = frozenset({
    "c", "m", "mvec", "pthread", "rt", "dl", "resolv", "udev",
    "GL", "EGL", "GLESv1_CM", "GLESv2", "GLX", "GLdispatch", "OpenGL",
    "glapi", "drm", "drm_amdgpu", "drm_intel", "drm_nouveau", "drm_radeon",
    "gbm", "vulkan",
})


def allowed_runtime_path(name):
    """Fixed updater-compatible grammar, not a recursive directory permission."""
    if (not isinstance(name, str) or not name or len(name) > MAX_PATH
            or not name.isascii() or "\\" in name or ":" in name
            or any(part in ("", ".", "..") for part in name.split("/"))
            or any(ord(char) < 32 or ord(char) == 127 for char in name)):
        return False
    if name in FIXED_PATHS:
        return True
    if name.startswith("lib/"):
        match = re.fullmatch(r"lib/lib([A-Za-z0-9_+.-]{1,64})\.so((?:\.[0-9]{1,4}){0,4})", name)
        if not match:
            return False
        base = match[1]
        return base in LIBRARY_BASES
    match = re.fullmatch(r"runtime/lib/python3(" + _PYTHON_VERSION + r")\.zip", name)
    if match:
        return True
    match = re.fullmatch(
        r"runtime/lib/python3\.(" + _PYTHON_VERSION + r")/lib-dynload/([A-Za-z0-9_]+)"
        r"(?:\.cpython-3(" + _PYTHON_VERSION + r")-x86_64-linux-gnu)?\.so", name)
    if match:
        return match[2] in EXTENSION_MODULES and (match[3] is None or match[1] == match[3])
    match = re.fullmatch(
        r"share/licenses/citroen-c4-robot/([a-z0-9][a-z0-9+.-]{0,95})/([^/]{1,96})", name)
    return bool(match and ".." not in match[1] and match[2] in LICENSE_NAMES)


def _run(command, library_dirs=(), *, binary=False):
    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    for key in ("LD_PRELOAD", "LD_LIBRARY_PATH", "PYTHONPATH", "PYTHONHOME"):
        environment.pop(key, None)
    if library_dirs:
        environment["LD_LIBRARY_PATH"] = ":".join(map(str, library_dirs))
    return subprocess.run(command, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=not binary, env=environment).stdout


def _digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def _regular(path, limit=MAX_FILE, *, allow_empty=False):
    info = path.lstat()
    minimum_size = 0 if allow_empty else 1
    if not stat.S_ISREG(info.st_mode) or not minimum_size <= info.st_size <= limit:
        raise ValueError(f"not a bounded regular file: {path}")


def _write_stdlib_archive(target, stdlib, stdlib_files):
    excluded_dirs = {"site-packages", "dist-packages", "__pycache__", "test", "tests",
                     "lib-dynload", "idlelib", "tkinter", "ensurepip", "venv"}
    with zipfile.ZipFile(target, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for directory, dirs, files in os.walk(stdlib, followlinks=False):
            dirs[:] = sorted(name for name in dirs if name not in excluded_dirs
                             and not name.startswith(".") and not (Path(directory) / name).is_symlink())
            for filename in sorted(files):
                if (not filename.endswith(".py") or filename.startswith(("test_", "."))
                        or filename in {"sitecustomize.py", "usercustomize.py"}):
                    continue
                source = Path(directory) / filename
                _regular(source, 4 * 1024 * 1024, allow_empty=True)
                if source not in stdlib_files:
                    raise ValueError(f"private/unowned file in distro stdlib: {source}")
                entry = zipfile.ZipInfo(source.relative_to(stdlib).as_posix(), (1980, 1, 1, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.create_system = 3
                entry.external_attr = 0o100644 << 16
                archive.writestr(entry, source.read_bytes())


def _elf(path):
    _regular(path)
    with path.open("rb") as source:
        header = source.read(20)
    if len(header) != 20 or header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\x3e\x00":
        raise ValueError(f"not Linux x86_64 ELF: {path}")
    dynamic = _run(["readelf", "--wide", "--dynamic", str(path)])
    needed = re.findall(r"\(NEEDED\).*?\[([^]]+)\]", dynamic)
    sonames = re.findall(r"\(SONAME\).*?\[([^]]+)\]", dynamic)
    versions = _run(["readelf", "--wide", "--version-info", str(path)])
    needs = versions.split("Version needs section", 1)[-1] if "Version needs section" in versions else ""
    glibc = [tuple(map(int, value.split(".")))
             for value in re.findall(r"Name: GLIBC_([0-9]+\.[0-9]+(?:\.[0-9]+)?)", needs)]
    return needed, sonames[0] if len(sonames) == 1 else None, max(glibc, default=())


GNU_X86_ISA_LEVELS = {"x86-64-baseline": 1, "x86-64-v2": 2,
                      "x86-64-v3": 3, "x86-64-v4": 4}


def _gnu_x86_isa_needed(path):
    """Read REQUIRED GNU properties, not ISA USED by optional dispatch paths."""
    notes = _run(["readelf", "--wide", "--notes", str(path)])
    values = set()
    for field in re.findall(r"x86 ISA needed:\s*([^\n]+)", notes):
        field = re.split(r",\s+(?=(?:x86 |[0-9]+_))", field, maxsplit=1)[0]
        for value in field.split(","):
            value = value.strip()
            if value not in GNU_X86_ISA_LEVELS:
                raise ValueError(f"unrecognized GNU x86 ISA needed property in {path}: {value}")
            values.add(value)
    return sorted(values, key=GNU_X86_ISA_LEVELS.__getitem__) or None


def _vulkan_runtime_required(stage: Path) -> bool:
    present = set()
    for name in VULKAN_SHADER_PATHS:
        source = stage / name
        if source.exists() or source.is_symlink():
            _regular(source)
            present.add(name)
    if present and present != VULKAN_SHADER_PATHS:
        raise ValueError("incomplete authored Vulkan shader payload")
    return bool(present)


def _host_dependency(name, *, bundled_vulkan=False):
    if bundled_vulkan and name == "libvulkan.so.1":
        return False
    match = re.fullmatch(r"lib(.+)\.so(?:\.[0-9.]+)?", name)
    return (name.startswith("ld-linux-") or bool(match and (
        match[1] in HOST_BASES or match[1].startswith(("GLX_", "EGL_", "nvidia-")))))


FFMPEG_LIBRARIES = frozenset({"avcodec", "avformat", "avutil", "swscale"})


def validate_ffmpeg_prefix(prefix: Path) -> dict:
    """Validate the pinned minimal LGPL build; retain original metadata for source publication."""
    from build_ffmpeg import check_configuration, load_lock

    prefix = Path(prefix)
    if not prefix.is_absolute() or not stat.S_ISDIR(prefix.lstat().st_mode):
        raise ValueError("FFmpeg prefix must be a real absolute directory")
    prefix = prefix.resolve(strict=True)
    lock_path = Path(__file__).with_name("ffmpeg.lock.json")
    lock = load_lock()
    if {item["name"] for item in lock["libraries"]} != FFMPEG_LIBRARIES:
        raise ValueError("FFmpeg lock must contain only the four minimal libraries")

    def file(relative, limit=MAX_FILE):
        target = prefix / relative
        for parent in target.parents:
            if parent == prefix or prefix in parent.parents:
                if not stat.S_ISDIR(parent.lstat().st_mode):
                    raise ValueError(f"FFmpeg input parent is not a real directory: {parent}")
        _regular(target, limit)
        if target.resolve(strict=True) != target:
            raise ValueError(f"FFmpeg input path escape: {relative}")
        return target

    metadata = json.loads(file("share/citroen-c4-robot/ffmpeg-source.json", 1024 * 1024)
                          .read_text(encoding="utf-8"))
    if not isinstance(metadata, dict):
        raise ValueError("FFmpeg source metadata must be a JSON object")
    for key in ("format", "component", "version", "url", "source_sha256",
                "license", "licenses", "libraries", "source_terms"):
        if metadata.get(key) != lock[key]:
            raise ValueError(f"FFmpeg source metadata disagrees with lock: {key}")
    if (metadata.get("prefix") != str(prefix) or metadata.get("lock_sha256") != _digest(lock_path)
            or metadata.get("proof") != lock["provenance"]):
        raise ValueError("FFmpeg prefix/lock/signature provenance mismatch")
    source_path = metadata.get("source_cache_path")
    if not isinstance(source_path, str) or not Path(source_path).is_absolute():
        raise ValueError("FFmpeg source cache must identify the explicit absolute archive path")
    source = Path(source_path)
    _regular(source)
    if (source.name != lock["archive"] or source.stat().st_size != lock["source_size"]
            or _digest(source) != lock["source_sha256"]):
        raise ValueError("FFmpeg corresponding source archive is missing or changed")
    proof_root = "share/licenses/FFmpeg/source-proof/"
    for relative, checksum in {
        proof_root + lock["archive"] + ".asc": lock["provenance"]["signature_sha256"],
        proof_root + "ffmpeg-devel.asc": lock["provenance"]["key_sha256"],
        proof_root + lock_path.name: _digest(lock_path),
    }.items():
        if _digest(file(relative, 1024 * 1024)) != checksum:
            raise ValueError(f"FFmpeg source signature/key/lock proof changed: {relative}")
    for notice in lock["licenses"]:
        if _digest(file(notice["path"], 4 * 1024 * 1024)) != notice["sha256"]:
            raise ValueError(f"FFmpeg original notice changed: {notice['path']}")

    configuration_paths = {proof_root + name for name in (
        "config.h", "config_components.h", "ffbuild/config.mak", "ffbuild/config.sh")}
    configuration = metadata.get("configuration", [])
    if (len(configuration) != len(configuration_paths)
            or {item["path"] for item in configuration} != configuration_paths):
        raise ValueError("FFmpeg configuration proof must contain the four exact generated files")
    for item in configuration:
        if _digest(file(item["path"], 4 * 1024 * 1024)) != item["sha256"]:
            raise ValueError(f"FFmpeg configuration proof changed: {item['path']}")
    proof_directory = prefix / proof_root
    check_configuration(proof_directory)
    arguments = metadata.get("configure_args", [])
    if not isinstance(arguments, list) or any(not isinstance(value, str) for value in arguments):
        raise ValueError("FFmpeg configure argument proof must be a list of strings")
    fixed_args = ["--prefix=" + str(prefix), "--libdir=" + str(prefix / "lib"),
                  "--shlibdir=" + str(prefix / "lib"),
                  "--pkgconfigdir=" + str(prefix / "lib/pkgconfig"), *lock["configure_args"]]
    tools = ("cc", "ld", "ar", "ranlib", "strip", "nm", "x86asmexe")
    if (arguments[:len(fixed_args)] != fixed_args
            or len(arguments) != len(fixed_args) + len(tools) + 1
            or arguments[-1] != "--pkg-config=false"):
        raise ValueError("FFmpeg configure arguments differ from the pinned minimal recipe")
    for name, argument in zip(tools, arguments[len(fixed_args):-1]):
        switch, separator, tool_path = argument.partition("=")
        if switch != "--" + name or not separator or not Path(tool_path).is_absolute():
            raise ValueError("FFmpeg compiler/tool argument is not an explicit absolute tool path")
    config = file(proof_root + "config.h", 4 * 1024 * 1024).read_text(encoding="utf-8")
    declared = re.search(r"^#define FFMPEG_CONFIGURATION (.+)$", config, re.MULTILINE)
    try:
        actual_arguments = shlex.split(ast.literal_eval(declared[1])) if declared else None
    except (ValueError, SyntaxError, TypeError) as error:
        raise ValueError("FFmpeg generated configure argument proof is malformed") from error
    if actual_arguments != arguments:
        raise ValueError("FFmpeg actual generated configuration disagrees with its build metadata")

    built = metadata.get("built_libraries", [])
    if len(built) != len(lock["libraries"]) or {item["name"] for item in built} != FFMPEG_LIBRARIES:
        raise ValueError("FFmpeg built-library inventory differs from the four pinned families")
    allowed_dependencies = {item["soname"] for item in lock["libraries"]} | {
        "libc.so.6", "libm.so.6", "libpthread.so.0", "librt.so.1", "libdl.so.2", "libgcc_s.so.1"}
    for library in lock["libraries"]:
        record = next(item for item in built if item["name"] == library["name"])
        relative = f"lib/lib{library['name']}.so.{library['version']}"
        if any(record.get(key) != value for key, value in dict(library, path=relative).items()):
            raise ValueError(f"FFmpeg library version/path/SONAME mismatch: {library['name']}")
        binary = file(relative)
        needed, soname, _ = _elf(binary)
        if (_digest(binary) != record["sha256"] or soname != library["soname"]
                or not set(needed) <= allowed_dependencies):
            raise ValueError(f"FFmpeg binary hash/ABI/minimal dependency proof mismatch: {relative}")
        for alias in (f"lib{library['name']}.so", library["soname"]):
            link = prefix / "lib" / alias
            if not link.is_symlink() or link.resolve(strict=True) != binary:
                raise ValueError(f"FFmpeg shared-library alias is invalid: {alias}")
    return dict(metadata, prefix=str(prefix), source_cache_path=str(source.resolve(strict=True)))


def validate_dbus_prefix(prefix: Path) -> dict:
    """Authenticate the owned ABI-compatible libdbus; never substitute a distro recipe."""
    from build_dbus import CONFIGURATION_FILES, check_configuration, load_lock, meson_arguments

    prefix = Path(prefix)
    if not prefix.is_absolute() or not stat.S_ISDIR(prefix.lstat().st_mode):
        raise ValueError("D-Bus prefix must be a real absolute directory")
    prefix = prefix.resolve(strict=True)
    lock_path = Path(__file__).with_name("dbus.lock.json")
    recipe_path = Path(__file__).with_name("build_dbus.py")
    lock = load_lock()
    if lock["libraries"] != [{"name": "dbus-1", "soname": "libdbus-1.so.3", "version": "3.38.3"}]:
        raise ValueError("D-Bus lock must identify the unmodified 1.16.2 shared ABI")

    def file(relative, limit=MAX_FILE):
        if (not isinstance(relative, str) or Path(relative).is_absolute()
                or any(part in ("", ".", "..") for part in relative.split("/"))):
            raise ValueError("invalid D-Bus proof relative path")
        target = prefix / relative
        for parent in target.parents:
            if parent == prefix or prefix in parent.parents:
                if not stat.S_ISDIR(parent.lstat().st_mode):
                    raise ValueError(f"D-Bus input parent is not a real directory: {parent}")
        _regular(target, limit)
        if target.resolve(strict=True) != target:
            raise ValueError(f"D-Bus input path escape: {relative}")
        return target

    metadata = json.loads(file("share/citroen-c4-robot/dbus-source.json", 1024 * 1024)
                          .read_text(encoding="utf-8"))
    if not isinstance(metadata, dict):
        raise ValueError("D-Bus source metadata must be a JSON object")
    for key, value in lock.items():
        if metadata.get(key) != value:
            raise ValueError(f"D-Bus source metadata disagrees with lock: {key}")
    if (metadata.get("prefix") != str(prefix) or metadata.get("lock_sha256") != _digest(lock_path)
            or metadata.get("recipe_sha256") != _digest(recipe_path)
            or metadata.get("proof") != lock["provenance"]):
        raise ValueError("D-Bus prefix/lock/recipe/signature provenance mismatch")
    source_path = metadata.get("source_cache_path")
    if not isinstance(source_path, str) or not Path(source_path).is_absolute():
        raise ValueError("D-Bus source cache must identify the explicit absolute archive path")
    source = Path(source_path)
    _regular(source)
    if (source.name != lock["archive"] or source.stat().st_size != lock["source_size"]
            or _digest(source) != lock["source_sha256"]):
        raise ValueError("D-Bus corresponding source archive is missing or changed")
    proof_root = "share/licenses/D-Bus/source-proof/"
    for material in lock["provenance"]["materials"]:
        path = file(proof_root + material["path"], 1024 * 1024)
        if path.stat().st_size != material["size"] or _digest(path) != material["sha256"]:
            raise ValueError(f"D-Bus original source authentication changed: {material['path']}")
    for path in (lock_path, recipe_path):
        if _digest(file(proof_root + path.name, 1024 * 1024)) != _digest(path):
            raise ValueError(f"D-Bus authored lock/recipe proof changed: {path.name}")
    for notice in lock["licenses"]:
        if _digest(file("share/licenses/D-Bus/" + notice["path"], 4 * 1024 * 1024)) != notice["sha256"]:
            raise ValueError(f"D-Bus original notice changed: {notice['path']}")

    paths = {proof_root + name for name in CONFIGURATION_FILES}
    configuration = metadata.get("configuration")
    if (not isinstance(configuration, list) or len(configuration) != len(paths)
            or any(not isinstance(item, dict) for item in configuration)
            or {item.get("path") for item in configuration} != paths):
        raise ValueError("D-Bus configuration proof must contain the exact generated files")
    for item in configuration:
        if _digest(file(item["path"], 4 * 1024 * 1024)) != item.get("sha256"):
            raise ValueError(f"D-Bus generated configuration proof changed: {item['path']}")
    if metadata.get("meson_args") != meson_arguments(prefix, lock):
        raise ValueError("D-Bus Meson arguments differ from the exact authored recipe")
    check_configuration(prefix / proof_root, metadata)

    built = metadata.get("built_libraries")
    if not isinstance(built, list) or len(built) != 1 or not isinstance(built[0], dict):
        raise ValueError("D-Bus built-library inventory must contain only libdbus-1")
    library = lock["libraries"][0]
    record = built[0]
    relative = f"lib/libdbus-1.so.{library['version']}"
    if any(record.get(key) != value for key, value in dict(library, path=relative).items()):
        raise ValueError("D-Bus library version/path/SONAME mismatch")
    binary = file(relative)
    needed, soname, _ = _elf(binary)
    if (_digest(binary) != record.get("sha256") or soname != library["soname"]
            or sorted(needed) != sorted(lock["needed_libraries"])):
        raise ValueError("D-Bus binary hash/ABI/exact linked dependency proof mismatch")
    symbols = _run(["readelf", "--wide", "--dyn-syms", str(binary)])
    exports = set(re.findall(
        r"^\s*\d+:\s+\S+\s+\d+\s+FUNC\s+(?:GLOBAL|WEAK)\s+DEFAULT\s+(?!UND\b)\S+\s+"
        r"(dbus_[A-Za-z0-9_]+)@@" + re.escape(lock["abi"]["symbol_version"]) + r"(?:\s|$)",
        symbols, re.MULTILINE))
    if not set(lock["required_symbols"]) <= exports:
        raise ValueError("D-Bus shared library lacks the required versioned public ABI")
    data = binary.read_bytes()
    for key in ("system", "session"):
        if lock["default_bus_addresses"][key].encode() + b"\0" not in data:
            raise ValueError(f"D-Bus binary default {key} bus address disagrees with policy")
    for alias in ("libdbus-1.so", library["soname"]):
        link = prefix / "lib" / alias
        if not link.is_symlink() or link.resolve(strict=True) != binary:
            raise ValueError(f"D-Bus shared-library alias is invalid: {alias}")
    return dict(metadata, prefix=str(prefix), source_cache_path=str(source.resolve(strict=True)))


def _source_required(expression):
    """Select a distributable license route; unknown terms never mean permissive."""
    if not isinstance(expression, str) or not expression.strip():
        raise ValueError("missing component license expression")
    permissive = {
        "MIT", "MIT-open-group", "ISC", "Zlib", "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause",
        "BSD-4-Clause", "BSD-4-Clause-UC", "BSD-Source-Code", "0BSD", "BSL-1.0", "FTL", "ICU",
        "Unicode-3.0", "Unicode-DFS-2016", "Python-2.0", "PSF-2.0", "bzip2-1.0.6",
        "libpng-2.0", "libpng", "curl", "HPND", "HPND-sell-variant", "X11", "IJG",
        "X11-distribute-modifications-variant", "CC0-1.0", "CC-BY-4.0", "NAIST-2003", "SMLNJ",
        "LicenseRef-MIT-SIPB", "LicenseRef-Public-Domain", "LicenseRef-PublicDomain",
        "LicenseRef-libXext-1.3.7",
        "LicenseRef-Kerberos-1.22.2-LibraryNotices",
        "LicenseRef-libarchive-3.8.10-LibraryNotices-CC0",
    }
    tokens = re.findall(r"[A-Za-z0-9.+_-]+|[()]", expression)
    if "".join(tokens) != re.sub(r"\s+", "", expression):
        raise ValueError(f"invalid component license expression: {expression}")
    position = 0

    def atom():
        nonlocal position
        if position >= len(tokens):
            raise ValueError("incomplete component license expression")
        token = tokens[position]
        position += 1
        if token == "(":
            value = alternative()
            if position >= len(tokens) or tokens[position] != ")":
                raise ValueError("unbalanced component license expression")
            position += 1
            return value
        if token in permissive:
            value = False
        elif re.fullmatch(r"(?:A?GPL|LGPL)-[0-9.]+-(?:only|or-later)", token) or token == "MPL-2.0":
            value = True
        else:
            raise ValueError(f"unknown component source terms: {token}")
        if position < len(tokens) and tokens[position] == "WITH":
            position += 1
            if position >= len(tokens):
                raise ValueError("missing component license exception")
            exception = tokens[position]
            position += 1
            if exception == "GCC-exception-3.1" and token.startswith("GPL-3.0-"):
                value = False
            elif exception == "PCRE2-exception" and token == "BSD-3-Clause":
                value = False
            elif exception not in {"Qt-GPL-exception-1.0", "Linux-syscall-note"}:
                raise ValueError(f"unknown component license exception: {exception}")
        return value

    def conjunction():
        nonlocal position
        value = atom()
        while position < len(tokens) and tokens[position] == "AND":
            position += 1
            right = atom()
            value = value or right
        return value

    def alternative():
        nonlocal position
        value = conjunction()
        while position < len(tokens) and tokens[position] == "OR":
            position += 1
            right = conjunction()
            value = value and right
        return value

    result = alternative()
    if position != len(tokens):
        raise ValueError(f"invalid component license expression: {expression}")
    return result


def _license_expression(label):
    """Translate conventional distro license short names, retaining AND/OR."""
    label = re.sub(r"\bor\b", "OR", label, flags=re.I)
    label = re.sub(r"\band\b", "AND", label, flags=re.I)
    aliases = {"BSD": "BSD-3-Clause", "BSD-2": "BSD-2-Clause", "BSD-3": "BSD-3-Clause",
               "Expat": "MIT", "MIT/X11": "MIT", "public-domain": "LicenseRef-Public-Domain",
               "public_domain": "LicenseRef-Public-Domain", "PSF": "Python-2.0",
               "Python": "Python-2.0", "PSF-2": "PSF-2.0", "BZIP": "bzip2-1.0.6",
               "BSD-3-clause": "BSD-3-Clause", "BSD-2-clause": "BSD-2-Clause",
               "GPL2": "GPL-2.0-only", "LGPL3": "LGPL-3.0-only"}
    for old, new in aliases.items():
        label = re.sub(r"(?<![A-Za-z0-9.+_-])" + re.escape(old) + r"(?![A-Za-z0-9.+_-])", new, label)
    label = re.sub(r"\b(GPL|LGPL|AGPL)-([0-9.]+)(\+)?(?![A-Za-z0-9.+_-])",
                   lambda match: match[1] + "-" + (
                       match[2] + ".0" if "." not in match[2] else match[2]) + (
                       "-or-later" if match[3] else "-only"), label)
    return label.strip()


def _standard_license_text(identifier):
    """Resolve legacy standard names without changing package/component grants."""
    candidates = [Path("/usr/share/licenses/spdx") / (identifier + ".txt"),
                  Path("/usr/share/licenses/spdx/exceptions") / (identifier + ".txt"),
                  Path("/usr/share/licenses/common") / identifier / "license.txt"]
    canonical = _license_expression(identifier)
    if canonical != identifier:
        candidates.extend([
            Path("/usr/share/licenses/spdx") / (canonical + ".txt"),
            Path("/usr/share/licenses/spdx/exceptions") / (canonical + ".txt")])
    found = next((path for path in candidates if path.is_file()), None)
    return found.resolve(strict=True) if found is not None else None


def _debian_expression(text):
    for paragraph in re.split(r"\n\s*\n", text):
        files = re.search(r"(?m)^Files:\s*(.+)", paragraph)
        label = re.search(r"(?m)^License:\s*(.+)", paragraph)
        if files and "*" in files[1].split() and label:
            return _license_expression(label[1])
    # Older Debian notices predate DEP-5. Accept only explicit license names.
    if "Python Software Foundation" in text and "GNU General Public" not in text:
        return "Python-2.0"
    if "GNU LESSER GENERAL PUBLIC LICENSE" in text.upper() or "GNU LIBRARY GENERAL PUBLIC LICENSE" in text.upper():
        match = re.search(r"(?:Lesser|Library) General Public License[^\n]*(?:version|Version)\s+([0-9.]+)", text)
        if match:
            return _license_expression("LGPL-" + match[1] + "+")
    if "Permission is hereby granted, free of charge" in text and not re.search(r"GNU.*General Public", text):
        return "MIT"
    return None

_LIBARCHIVE_NOTICE_SOURCE = {
    "version": "3.8.10",
    "commit": "0c48412be2d94afe1859a807ea7896a18d8900bd",
    "archive_url": "https://codeload.github.com/libarchive/libarchive/tar.gz/0c48412be2d94afe1859a807ea7896a18d8900bd",
    "archive_sha256": "391daa6bbb2d0d8315a8dea5dc741f071e759cfeca2fe0bb90999b42979743f1",
    "archive_size": 6829745,
    "manifest_sha256": "d97e9d1e314e76430f4d44a054c7e6fe0a5fef88505d5e8fef46e5d7b6a22362",
    "manifest_size": 79897,
    "source_count": 176,
    "declared_source": "git+https://github.com/libarchive/libarchive.git?signed#tag=v3.8.10",
    "recipe_scope": ("exact signed-package BUILDINFO recipe hash; no downstream patches or backports; "
                     "one revert of upstream commit fafe82e3502e8e75a6c4146ec977f77244787c6d "
                     "(xar XML library detection), whose only legal-comment change is adding "
                     "libarchive/archive_xml_private.h, so the tag-tree corpus is a notice superset"),
    "reverts": ["fafe82e3502e8e75a6c4146ec977f77244787c6d"],
    "cc0_license": "CC0-1.0",
    "pkgbuild_sha256": "f97910d09d390608ef7177d48f39fdb1e0f340b265c00f557d1d9cb076c8019d",
    "library_sha256": "08960c60b44bb280e84eb35c54350829cd69bd27190b844da6a54ffa227583f0",
    "collector": "libarchive-3.8.10-library-comment-corpus-v1",
    "cc0_files": ["libarchive/archive_blake2.h", "libarchive/archive_blake2_impl.h",
                  "libarchive/archive_blake2s_ref.c", "libarchive/archive_blake2sp_ref.c"],
    "qualification": ("Conservative all-platform notice retention from the signed v3.8.10 tag tree, "
                      "not compiled-object or legal certification; tag signature locally verified "
                      "(validpgpkeys 659C84C0E23EA1FA97E0B58CC040B508D63D2B36)"),
}



# Full release notices, not SPDX templates or packagewide opaque-label aliases.
_COMPONENT_NOTICES = {
    "libogg": {
        "library": r"libogg\.so(?:\.[0-9]+)+",
        "declared_expression": "BSD",
        "license_expression": "BSD-3-Clause",
        "origin": "https://raw.githubusercontent.com/xiph/ogg/v1.3.6/COPYING",
        "sha256": "d2ab5758336489da61c12cc5bb757da5339c4ae9001f9bb0562b4370249af814",
    },
    "bzip2": {
        "library": r"libbz2\.so(?:\.[0-9]+)+",
        "declared_expression": "BSD",
        "license_expression": "bzip2-1.0.6",
        "origin": "https://sourceware.org/git/?p=bzip2.git;a=blob_plain;f=LICENSE;hb=bzip2-1.0.8",
        "sha256": "c6dbbf828498be844a89eaa3b84adbab3199e342eb5cb2ed2f0d4ba7ec0f38a3",
    },
    "icu": {
        "library": r"libicu(?:data|uc|i18n)\.so\.78\.3",
        "source_version": "78.3-1.1",
        "declared_expression": "(LicenseRef-Unicode-3.0) AND (BSD-2-Clause) AND (BSD-3-Clause) AND (NAIST-2003)",
        "license_expression": "Unicode-3.0 AND BSD-2-Clause AND BSD-3-Clause AND NAIST-2003",
        "origin": "https://raw.githubusercontent.com/unicode-org/icu/release-78.3/LICENSE",
        "sha256": "e55522d81edc687a341a4411e0776e54ca654e90147f354a90458aaced4116af",
    },
    "libxext": {
        "library": r"libXext\.so\.6\.4\.0",
        "source_version": "1.3.7-1.1",
        "declared_expression": "LicenseRef-libxext",
        # Release-scoped extracted notice identifier, not a standard SPDX mapping.
        "license_expression": "LicenseRef-libXext-1.3.7",
        "origin": "https://gitlab.freedesktop.org/xorg/lib/libxext/-/raw/libXext-1.3.7/COPYING",
        "sha256": "fd62910be4b13829d94e76c1447cf840953f0e225c4dc6c79349c84dd0557f22",
    },
    "krb5": {
        "library": r"(?:libgssapi_krb5\.so\.2\.2|libkrb5\.so\.3\.3|libk5crypto\.so\.3\.1|libkrb5support\.so\.0\.1)",
        "source_version": "1.22.2-1.1",
        "declared_expression": "custom",
        # Full retained permissions, not a standard SPDX mapping or legal certification.
        "license_expression": "LicenseRef-Kerberos-1.22.2-LibraryNotices",
        "origin": "https://raw.githubusercontent.com/krb5/krb5/8570e77819563e036027e1da789d08ec9333ed4d/NOTICE",
        "sha256": "d37fcb886de2ad1c602f61c99c6893bcbd9a1949f54d9a6bb40d191204eb605e",
        "scope": "complete unchanged release notice; conditional grants retained conservatively",
        "notice_cache": "krb5-1.22.2",
        "supplemental_notices": {
            "src/include/k5-queue.h": {
                "sha256": "6074ee76bdde96dfcf2a57134676baf7ae37fac7644ff349d3996b748a82d0bb",
                "origin": "https://raw.githubusercontent.com/krb5/krb5/8570e77819563e036027e1da789d08ec9333ed4d/src/include/k5-queue.h",
                "scope": "actual UCB queue-header attribution incorporated by library support hashtab",
            },
            "src/include/copyright.h": {
                "sha256": "435ef19645770fbb3e9cdd2bead820307768ce1981e5d164622f6faca5333f2d",
                "origin": "https://raw.githubusercontent.com/krb5/krb5/8570e77819563e036027e1da789d08ec9333ed4d/src/include/copyright.h",
                "scope": "complete legacy MIT permission and retained conditions",
            },
            "src/lib/gssapi/LICENSE": {
                "sha256": "c4ab82b506f53b97e7bf7ee2b71152da792e0d5578da4089ed17b4dd8798aedf",
                "origin": "https://raw.githubusercontent.com/krb5/krb5/8570e77819563e036027e1da789d08ec9333ed4d/src/lib/gssapi/LICENSE",
                "scope": "complete Sun GSSAPI permission and provenance",
            },
        },
    },
    "libarchive": {
        "library": r"libarchive\.so\.13\.8\.10",
        "source_version": "3.8.10-1.1",
        "declared_expression": "(BSD-2-Clause) AND (BSD-4-Clause-UC) AND (Apache-2.0 OR CC0-1.0 OR OpenSSL)",
        "license_expression": "LicenseRef-libarchive-3.8.10-LibraryNotices-CC0",
        "origin": "https://raw.githubusercontent.com/libarchive/libarchive/0c48412be2d94afe1859a807ea7896a18d8900bd/COPYING",
        "sha256": "30e556b3959e3985d66efefec5eaac51d4995053caa1d3cffe6eb916f146f229",
        "size": 3089,
        "scope": "unchanged upstream COPYING default; individual source notices control",
        "notice_cache": "libarchive-3.8.10",
        "notice_source": _LIBARCHIVE_NOTICE_SOURCE,
        "supplemental_notices": {
            "cc0-1.0.txt": {
                "origin": "https://raw.githubusercontent.com/creativecommons/cc-legal-tools-data/a0dac18d1a773b4cdb7d8dd5d9bf4efab7901030/docs/publicdomain/zero/1.0/legalcode.txt",
                "sha256": "a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499",
                "size": 7048,
                "scope": "CC0-1.0 elected only for the four BLAKE2 reference files",
            },
            "library-notices.txt": {
                "origin": _LIBARCHIVE_NOTICE_SOURCE["archive_url"],
                "sha256": "34c7c75c7410b42a9e556b77ae6a23729d23031301b0b1354c4aee65d6562ee2",
                "size": 292117,
                "scope": "verbatim legal comments from all 176 top-level library source/header files; conditional platforms retained",
            },
        },
    },
    "libasyncns": {
        "package": "libasyncns",
        "library": r"libasyncns\.so\.0\.3\.1",
        "source_version": "1:0.8+r3+g68cd5af-3.2",
        "declared_expression": "LGPL",
        "license_expression": "LGPL-2.1-or-later",
        "source_member": "LICENSE",
        "origin": "https://git.0pointer.net/libasyncns.git/tree/LICENSE?id=68cd5aff1467638c086f1bedcc750e34917168e4",
        "sha256": "d80c9d084ebfb50ea1ed91bfbc2410d6ce542097a32c43b00781b83adcb8c77f",
        "size": 26528,
        "mode": 0o664,
        "scope": "complete unchanged LGPL 2.1 body; actual library headers supply the or-later grant",
        "notice_cache": "libasyncns-68cd5aff1467638c086f1bedcc750e34917168e4",
        "notice_source": {
            "collector": "cached-source-members-v1",
            "commit": "68cd5aff1467638c086f1bedcc750e34917168e4",
            "archive_path": "libasyncns/1:0.8+r3+g68cd5af-3.2/libasyncns-68cd5aff1467638c086f1bedcc750e34917168e4.tar.gz",
            "archive_url": "https://github.com/Cor0n4V1rus/libasyncns",
            "archive_sha256": "aef00f63f60ce69816405b7372ca78b7c525b17520bf7a747a700ffb9d82b296",
            "archive_size": 45214,
            "canonical_archive_sha256": "c727e140aab4c94c5c235768e496bd2466c299691d5ddd13dd5ffd9e7364b61a",
            "declared_source": "git+https://git.0pointer.net/clone/libasyncns.git#commit=68cd5aff1467638c086f1bedcc750e34917168e4",
            "pkgbuild_sha256": "c3406297b1bf539da442e3e35cb43f47652d6adaac487028012fda4f9e48d122",
            "library_sha256": "532222b24689f1d81d70748db19cc11f84ba468cb204f4f1db2584357371ac92",
            "binary_archive_sha256": "9a5d3d165207882ffee4bfc885194999b3b3c8e6ba73ceafb4d2a7e01670002f",
            "mtree_sha256": "07ce825c50ead527ff2eb74983a9aa83243171281b562dbccf22894d6199dfd9",
            "recipe_scope": "exact signed BUILDINFO recipe; unchanged commit snapshot, no downstream patches",
            "qualification": "complete selected library source/header notices, not a grant inferred from a license template",
        },
        "supplemental_notices": {
            "libasyncns/asyncns.c": {
                "source_member": "libasyncns/asyncns.c",
                "origin": "https://git.0pointer.net/libasyncns.git/tree/libasyncns/asyncns.c?id=68cd5aff1467638c086f1bedcc750e34917168e4",
                "sha256": "4db61110d36de49df9162499df3b875c2fa6744418bc2f40f79bb7e78a037abb",
                "size": 37112,
                "mode": 0o664,
                "scope": "complete actual library source, including Lennart Poettering 2005-2008 LGPL 2.1-or-later grant",
            },
            "libasyncns/asyncns.h": {
                "source_member": "libasyncns/asyncns.h",
                "origin": "https://git.0pointer.net/libasyncns.git/tree/libasyncns/asyncns.h?id=68cd5aff1467638c086f1bedcc750e34917168e4",
                "sha256": "74a2409ab37cddd0fbb8c49230189913b025368f598ec5172ee468d185fe444e",
                "size": 6985,
                "mode": 0o664,
                "scope": "complete actual public library header, including Lennart Poettering 2005-2008 LGPL 2.1-or-later grant",
            },
        },
    },
}


def _component_terms(source, source_package, expression, copyright_text,
                     notice_records=(), evidence=None, *, source_version=None, source_binding=None):
    """Resolve exact declared component routes against pinned complete release notices."""
    name = source.name
    if evidence is not None:
        evidence["notices"] = notice_records
    contract = _COMPONENT_NOTICES.get(source_package)
    if (contract and expression == contract["declared_expression"] and
            re.fullmatch(contract["library"], name)):
        if "source_version" in contract and source_version != contract["source_version"]:
            raise ValueError(f"{name} requires exact source version {contract['source_version']}")
        if "notice_source" in contract:
            expected = contract["notice_source"]
            member = source_binding.get("binary_member", {}) if isinstance(source_binding, dict) else {}
            if (not isinstance(source_binding, dict) or
                    not isinstance(member, dict) or
                    source_binding.get("pkgbuild_sha256") != expected["pkgbuild_sha256"] or
                    member.get("path") != "usr/lib/" + name or
                    member.get("sha256") != expected["library_sha256"] or
                    not isinstance(member.get("mtree_sha256"), str) or
                    not re.fullmatch(r"[a-f0-9]{64}", member["mtree_sha256"]) or
                    ("mtree_sha256" in expected and member["mtree_sha256"] != expected["mtree_sha256"])):
                raise ValueError(f"{name} requires its authenticated exact recipe and binary-member notice-source binding")
        primary_materials = []
        for pin in (contract, *contract.get("supplemental_notices", {}).values()):
            notice = next((record for record in notice_records
                           if record["sha256"] == pin["sha256"]), None)
            if notice is None:
                raise ValueError(f"{name} requires its pinned complete upstream component notice")
            material = dict(notice, origin=pin["origin"], license_expression=contract["license_expression"])
            if "source_version" in contract:
                material.update(source_package=source_package, source_version=source_version, library=name)
            if "scope" in pin:
                material["scope"] = pin["scope"]
            primary_materials.append(material)
        if evidence is not None:
            evidence["primary_notice"] = primary_materials[0]
            if "supplemental_notices" in contract:
                evidence["primary_notices"] = primary_materials
            if "notice_source" in contract:
                evidence["notice_source"] = {
                    key: list(value) if isinstance(value, list) else value
                    for key, value in contract["notice_source"].items()}
                evidence["notice_source_binding"] = {
                    "pkgbuild_sha256": source_binding["pkgbuild_sha256"],
                    "binary_member": dict(source_binding["binary_member"]),
                }
        return contract["license_expression"]
    if re.fullmatch(r"libcom_err\.so(?:\.[0-9]+)*", name) and source_package == "e2fsprogs":
        if "M.I.T. S.I.P.B." not in copyright_text or "advertising" not in copyright_text:
            raise ValueError("libcom_err requires the authentic MIT SIPB component notice")
        return "LicenseRef-MIT-SIPB"
    if re.fullmatch(r"lib(?:stdc\+\+|gcc_s|gomp)\.so(?:\.[0-9]+)*", name):
        if not re.search(r"GCC Runtime Library Exception", copyright_text, re.I):
            raise ValueError("GCC runtime component requires the authentic runtime exception notice")
        return "GPL-3.0-or-later WITH GCC-exception-3.1"
    mixed = None
    if source_package in {"util-linux", "util-linux-libs"}:
        if re.fullmatch(r"lib(?:mount|blkid)\.so(?:\.[0-9]+)*", name):
            mixed = "LGPL-2.1-or-later"
        elif re.fullmatch(r"libuuid\.so(?:\.[0-9]+)*", name):
            mixed = "BSD-3-Clause"
    elif source_package == "systemd" and re.fullmatch(r"libsystemd\.so(?:\.[0-9]+)*", name):
        mixed = "LGPL-2.1-or-later"
    elif source_package in {"qt6-base", "qt6-wayland"}:
        mixed = "LGPL-3.0-only"
    elif source_package in {"acl", "attr"} and re.fullmatch(r"lib(?:acl|attr)\.so(?:\.[0-9]+)*", name):
        mixed = "LGPL-2.1-or-later"
    elif source_package in {"libidn2", "libunistring"}:
        mixed = "LGPL-3.0-or-later OR GPL-2.0-or-later"
    elif source_package in {"xz", "xz-utils"} and re.fullmatch(r"liblzma\.so(?:\.[0-9]+)*", name):
        if "0BSD" in copyright_text:
            return "0BSD"
        if re.search(r"public domain", copyright_text, re.I):
            return "LicenseRef-Public-Domain"
        raise ValueError("liblzma requires its authentic component distribution terms")
    elif source_package in {"libcap", "libcap2"} and re.fullmatch(r"libcap\.so(?:\.[0-9]+)*", name):
        mixed = "BSD-3-Clause"
    elif source_package in {"freetype2", "freetype"} and re.fullmatch(r"libfreetype\.so(?:\.[0-9]+)*", name):
        if "FreeType" not in copyright_text:
            raise ValueError("FreeType requires its authentic FreeType license notice")
        return "FTL"
    elif source_package == "keyutils" and re.fullmatch(r"libkeyutils\.so(?:\.[0-9]+)*", name):
        mixed = "LGPL-2.0-or-later"
    elif source_package == "liburing" and re.fullmatch(r"liburing\.so(?:\.[0-9]+)*", name):
        mixed = "MIT"
    elif source_package == "dbus" and re.fullmatch(r"libdbus-1\.so(?:\.[0-9]+)*", name):
        if not re.search(r"GPL|General Public License", copyright_text):
            raise ValueError("Dbus requires its authentic selected GPL license notice")
        return "GPL-2.0-or-later"
    if mixed:
        if mixed.startswith("LGPL") and not re.search(r"LGPL|Lesser General Public|Library General Public", copyright_text):
            raise ValueError(f"missing authentic library terms for {name}")
        if mixed.startswith("BSD") and not re.search(r"BSD|Redistribution and use", copyright_text):
            raise ValueError(f"missing authentic BSD library terms for {name}")
        return mixed
    return expression


def _mtree_members(data, *, notice_entries=False):
    import gzip
    text = gzip.decompress(data).decode("utf-8") if data.startswith(b"\x1f\x8b") else data.decode("utf-8")
    result = {}
    defaults = {}
    for row in text.splitlines():
        if notice_entries and row.startswith("/set "):
            defaults.update(field.split("=", 1) for field in row.split()[1:] if "=" in field)
            continue
        if notice_entries and row.startswith("/unset "):
            for field in row.split()[1:]:
                if field == "all":
                    defaults.clear()
                else:
                    defaults.pop(field, None)
            continue
        if not row.startswith("./"):
            continue
        fields = row.split()
        if notice_entries:
            name = re.sub(r"\\([0-7]{3})", lambda match: chr(int(match[1], 8)), fields[0][2:])
            if not name:
                continue  # The archive root is not an installed notice member.
            if name in result or any(part in {"", ".", ".."} for part in name.split("/")):
                raise ValueError("invalid authenticated pacman notice member path")
            attrs = dict(defaults)
            attrs.update(field.split("=", 1) for field in fields[1:] if "=" in field)
            result[name] = {
                key: re.sub(r"\\([0-7]{3})", lambda match: chr(int(match[1], 8)), value)
                for key, value in attrs.items()
                if key in {"type", "mode", "link", "size", "sha256digest"}}
            mode = result[name].get("mode")
            if mode is not None:
                if not re.fullmatch(r"[0-7]{3,4}", mode):
                    raise ValueError("invalid authenticated pacman notice member mode")
                result[name]["mode"] = format(int(mode, 8), "o")
            continue
        checksum = next((field.removeprefix("sha256digest=") for field in fields[1:]
                         if field.startswith("sha256digest=")), None)
        if checksum is None:
            continue
        name = re.sub(r"\\([0-7]{3})", lambda match: chr(int(match[1], 8)), fields[0][2:])
        if name in result or not re.fullmatch(r"[a-f0-9]{64}", checksum):
            raise ValueError("invalid authenticated pacman member checksum")
        result[name] = checksum
    if not result:
        raise ValueError("authenticated pacman archive has no file checksum manifest")
    return result


def _resolve_pacman_notice(path, entries, root=Path("/")):
    """Resolve only signed links between installed notice roots, never staging paths."""
    root = Path(root)
    if not root.is_absolute() or not stat.S_ISDIR(root.lstat().st_mode):
        raise ValueError("pacman notice fixture root must be a real absolute directory")
    path = Path(path)

    def approved(name):
        return (name.startswith(("usr/share/licenses/", "usr/share/doc/"))
                or name in {"usr/share/licenses", "usr/share/doc"}
                or re.fullmatch(r"usr/lib/python3\.[0-9]+/LICENSE\.txt", name))

    try:
        name = path.relative_to(root).as_posix()
    except ValueError:
        raise ValueError(f"distro notice is outside its installed root: {path}") from None
    if ".." in Path(name).parts or not approved(name):
        raise ValueError(f"distro notice is outside approved notice roots: {path}")
    seen = set()
    while True:
        current = root
        parts = Path(name).parts
        for index, part in enumerate(parts):
            current /= part
            info = current.lstat()
            member = current.relative_to(root).as_posix()
            if stat.S_ISLNK(info.st_mode):
                entry = entries.get(member, {})
                spelling = os.readlink(current)
                if (entry.get("type") != "link" or entry.get("link") != spelling
                        or entry.get("mode") != format(stat.S_IMODE(info.st_mode), "o")
                        or member in seen or len(seen) >= 40):
                    raise ValueError(f"distro notice has an unauthenticated link: {current}")
                seen.add(member)
                target = (spelling.lstrip("/") if spelling.startswith("/")
                          else str(Path(member).parent / spelling))
                target = os.path.normpath(target)
                if not approved(target):
                    raise ValueError(f"distro notice link escapes approved roots: {current}")
                name = str(Path(target).joinpath(*parts[index + 1:]))
                if not approved(name):
                    raise ValueError(f"distro notice link escapes approved roots: {current}")
                break
            if index < len(parts) - 1 and not stat.S_ISDIR(info.st_mode):
                raise ValueError(f"distro notice parent is not a real directory: {current}")
        else:
            if stat.S_ISDIR(info.st_mode):
                return current
            _regular(current, 4 * 1024 * 1024)
            entry = entries.get(name, {})
            if (entry.get("type") != "file"
                    or entry.get("mode") != format(stat.S_IMODE(info.st_mode), "o")
                    or entry.get("size") != str(info.st_size)
                    or entry.get("sha256digest") != _digest(current)):
                raise ValueError(f"distro notice differs from its authenticated member: {current}")
            return current


def _read_pacman_notice(path, entries, root=Path("/")):
    """Recheck the exact bytes copied, including mode, against the signed member."""
    resolved = _resolve_pacman_notice(path, entries, root)
    with os.fdopen(os.open(resolved, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK), "rb") as source:
        info = os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= 4 * 1024 * 1024:
            raise ValueError(f"not a bounded regular distro notice: {resolved}")
        data = source.read(4 * 1024 * 1024 + 1)
    entry = entries.get(resolved.relative_to(root).as_posix(), {})
    if (entry.get("type") != "file"
            or entry.get("mode") != format(stat.S_IMODE(info.st_mode), "o")
            or entry.get("size") != str(len(data))
            or entry.get("sha256digest") != hashlib.sha256(data).hexdigest()):
        raise ValueError(f"copied distro notice differs from its authenticated member: {resolved}")
    return resolved, data


def _bind_pacman_member(source, provenance, members):
    name = source.relative_to(Path("/")).as_posix()
    expected = members.get(name)
    if expected is None or expected != provenance["original_sha256"]:
        raise ValueError(f"deployed ELF differs from authenticated package source identity: {source}")
    return {"path": name, "sha256": expected,
            "mtree_sha256": provenance["binary_package"]["mtree_sha256"]}


def _notice_source_binding(source, provenance, members):
    """Bind this source-derived notice route to the signed package recipe and ELF."""
    contract = _COMPONENT_NOTICES.get(provenance.get("source_package"))
    if not contract or "notice_source" not in contract or not re.fullmatch(contract["library"], source.name):
        return None
    expected = contract["notice_source"]
    binary = provenance.get("binary_package")
    if (provenance.get("manager") != "pacman" or
            provenance.get("package") != contract.get("package", provenance["source_package"]) or
            provenance.get("version") != contract["source_version"] or
            provenance.get("source_version") != contract["source_version"] or
            provenance.get("pkgbuild_sha256") != expected["pkgbuild_sha256"] or
            provenance.get("original_sha256") != expected["library_sha256"] or
            not isinstance(binary, dict) or
            binary.get("authentication") != "gpgv detached signature with installed pacman keyring" or
            not isinstance(binary.get("mtree_sha256"), str) or
            not re.fullmatch(r"[a-f0-9]{64}", binary["mtree_sha256"]) or
            ("mtree_sha256" in expected and binary["mtree_sha256"] != expected["mtree_sha256"]) or
            ("binary_archive_sha256" in expected and binary.get("sha256") != expected["binary_archive_sha256"])):
        raise ValueError(f"{source.name} requires its authenticated exact notice-source recipe identity")
    member = _bind_pacman_member(source, provenance, members)
    if member["path"] != "usr/lib/" + source.name:
        raise ValueError(f"{source.name} has an unexpected authenticated library member path")
    return {"pkgbuild_sha256": provenance["pkgbuild_sha256"], "binary_member": member}


def _pacman_source_identity(package, version, dependency_sources_dir):
    directory = Path("/var/lib/pacman/local") / f"{package}-{version}"
    desc = directory / "desc"
    _regular(desc, 1024 * 1024)
    fields = {}
    for match in re.finditer(r"(?m)^%([A-Z0-9_]+)%\n((?:(?!\n\n).)+)", desc.read_text(encoding="utf-8"), re.S):
        fields[match[1]] = match[2].strip()
    if fields.get("NAME") != package or fields.get("VERSION") != version or not fields.get("BASE"):
        raise ValueError(f"missing exact installed pacman source identity: {package}")
    result = {"source_package": fields["BASE"], "source_version": version,
              "package_license_declarations": fields.get("LICENSE", "").splitlines(),
              "source_identity_evidence": {"type": "installed-pacman-desc",
                                           "sha256": _digest(desc),
                                           "validation": fields.get("VALIDATION", "").splitlines(),
                                           "architecture": fields.get("ARCH")}}
    # Only a detached-signature-verified exact binary archive may bind its build recipe.
    if shutil.which("bsdtar") and shutil.which("gpgv"):
        archives = list(Path("/var/cache/pacman/pkg").glob(package + "-*.pkg.tar.*"))
        archives.extend((dependency_sources_dir / package / version).glob(package + "-*.pkg.tar.*"))
        for archive in sorted(set(archives)):
            if archive.name.endswith(".sig") or archive.is_symlink() or not archive.is_file():
                continue
            try:
                info = _run(["bsdtar", "-xOf", str(archive), ".PKGINFO"])
            except subprocess.CalledProcessError:
                continue
            if (re.findall(r"^pkgname = (.*)$", info, re.M) != [package]
                    or re.findall(r"^pkgver = (.*)$", info, re.M) != [version]
                    or re.findall(r"^arch = (.*)$", info, re.M) != [fields.get("ARCH")]):
                continue
            signature = archive.with_name(archive.name + ".sig")
            if not signature.is_file() or signature.is_symlink():
                continue
            _run(["gpgv", "--keyring", "/etc/pacman.d/gnupg/pubring.gpg", str(signature), str(archive)])
            buildinfo = _run(["bsdtar", "-xOf", str(archive), ".BUILDINFO"])
            hashes = re.findall(r"^pkgbuild_sha256sum = ([a-f0-9]{64})$", buildinfo, re.M)
            bases = re.findall(r"^pkgbase = (.*)$", buildinfo, re.M)
            if len(hashes) != 1 or bases != [fields["BASE"]]:
                raise ValueError(f"authenticated binary BUILDINFO identity mismatch: {package}")
            result["pkgbuild_sha256"] = hashes[0]
            mtree = _run(["bsdtar", "-xOf", str(archive), ".MTREE"], binary=True)
            result["_binary_members"] = _mtree_members(mtree)
            result["_binary_notice_entries"] = _mtree_members(mtree, notice_entries=True)
            result["binary_package"] = {
                "archive": archive.name, "sha256": _digest(archive), "size": archive.stat().st_size,
                "signature_sha256": _digest(signature),
                "authentication": "gpgv detached signature with installed pacman keyring",
                "buildinfo_sha256": hashlib.sha256(buildinfo.encode("utf-8")).hexdigest(),
                "mtree_sha256": hashlib.sha256(mtree).hexdigest()}
            break
    return result


def _com_err_notices(cache_root, source_version):
    """Keep the upstream component notice, not the package's unrelated GPL license."""
    version = source_version.split(":", 1)[-1].split("-", 1)[0]
    pins = {
        "1.47.4": {
            "NOTICE": "5da5ef153e559c1d990d4c3eedbedd4442db892d37eae1f35fff069de8ec9020",
            "lib/et/com_err.c": "53023a7ab28c50461dd2ecab85d518a0ecd439e176fdb1f047e24781d63a90c8",
            "lib/et/error_message.c": "bf0589587982ca7db1172a26181d44e18bdc4bbbde966aa69370b83f548c1bf6",
        }}
    if version not in pins:
        raise ValueError(f"no authenticated libcom_err notice pin for e2fsprogs {version}")
    from appimage.acquire import _download
    from dependency_sources import _directory
    result = []
    for relative, digest in pins[version].items():
        directory = cache_root / "notices" / ("e2fsprogs-" + version) / Path(relative).parent
        _directory(cache_root, directory)
        result.append(_download(directory, Path(relative).name,
                               f"https://raw.githubusercontent.com/tytso/e2fsprogs/v{version}/{relative}", digest))
    return result


_LIBARCHIVE_COMMENT_TOKENS = re.compile(
    rb"/\*[\s\S]*?\*/|//[^\r\n]*|\"(?:\\[\s\S]|[^\"\\])*\"|'(?:\\[\s\S]|[^'\\])*'")
_LIBARCHIVE_LEGAL_COMMENT = re.compile(
    rb"copyright|copyleft|public[\s*]+domain|licen[cs]e|permission[\s*]+(?:is|to|for|hereby|granted)|"
    rb"redistribut|all[\s*]+rights[\s*]+reserved|warrant|liabilit|\bregents\b|\bclause[\s*]+[0-9]|"
    rb"\bSPDX\b|\btrademark\b", re.IGNORECASE)


def _libarchive_corpus(sources):
    """Reproduce the pinned raw-comment corpus without editing grants or holder names."""
    chunks = [
        (f"libarchive {_LIBARCHIVE_NOTICE_SOURCE['version']} library source notices\n"
         f"Derived from primary upstream commit {_LIBARCHIVE_NOTICE_SOURCE['commit']}.\n").encode("ascii") +
        b"Scope: all versioned top-level libarchive/*.c, *.h and *.h.in; all conditional platforms included.\n"
        b"The preamble and labeled separators are derived metadata; source comment blocks are verbatim bytes.\n"
        b"Individual source notices control; unchanged upstream COPYING supplies its stated default where needed.\n"
        b"License choice for the four BLAKE2 reference files only: CC0-1.0; see cc0-1.0.txt.\n\n"
    ]
    for path in sorted(sources, key=lambda name: name.encode("ascii")):
        if not re.fullmatch(r"libarchive/[^/]+\.(?:c|h|h\.in)", path):
            raise ValueError(f"source outside libarchive notice scope: {path}")
        data = sources[path]
        checksum = hashlib.sha256(data).hexdigest()
        blob = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
        chunks.append((f"===== DERIVED SOURCE IDENTITY: {path} =====\n"
                       f"source-sha256: {checksum}\nsource-size: {len(data)}\n"
                       f"git-blob-sha1: {blob}\n").encode("ascii"))
        count = 0
        for token in _LIBARCHIVE_COMMENT_TOKENS.finditer(data):
            comment = token.group()
            if not comment.startswith((b"/*", b"//")) or not _LIBARCHIVE_LEGAL_COMMENT.search(comment):
                continue
            count += 1
            chunks.append((f"----- DERIVED COMMENT BLOCK {count}: source bytes "
                           f"[{token.start()}, {token.end()}) -----\n").encode("ascii"))
            chunks.extend((comment, b"\n\n"))
        if not count:
            chunks.append(b"No qualifying source comment; the unchanged upstream COPYING default applies. "
                          b"No author list is inferred.\n\n")
    return b"".join(chunks)


def _libarchive_archive_corpus(archive):
    """Read only regular pinned archive members; never extract a persistent source tree."""
    identity = _LIBARCHIVE_NOTICE_SOURCE
    _regular(archive)
    if archive.stat().st_size != identity["archive_size"] or _digest(archive) != identity["archive_sha256"]:
        raise ValueError("libarchive notice source archive differs from its pin")
    prefix = "libarchive-" + identity["commit"] + "/"
    sources = {}
    required = {}
    total = 0
    with tarfile.open(archive, "r:gz") as stream:
        for member in stream:
            if member.isdir() and member.name.rstrip("/") == prefix.rstrip("/"):
                continue
            if not member.name.startswith(prefix):
                raise ValueError("libarchive notice source member has an unexpected prefix")
            relative = member.name[len(prefix):]
            selected = re.fullmatch(r"libarchive/[^/]+\.(?:c|h|h\.in)", relative)
            if not selected and relative not in {"COPYING", "Makefile.am"}:
                continue
            if not member.isfile() or not 0 < member.size <= MAX_FILE:
                raise ValueError(f"not a bounded regular libarchive notice source: {relative}")
            total += member.size
            if total > MAX_TOTAL or relative in sources or relative in required:
                raise ValueError("duplicate or excessive libarchive notice source members")
            with stream.extractfile(member) as source:
                data = source.read()
            if len(data) != member.size:
                raise ValueError(f"truncated libarchive notice source: {relative}")
            (sources if selected else required)[relative] = data
    copying = required.get("COPYING", b"")
    manifest = required.get("Makefile.am", b"")
    if (len(copying) != 3089 or hashlib.sha256(copying).hexdigest() != _COMPONENT_NOTICES["libarchive"]["sha256"] or
            len(manifest) != identity["manifest_size"] or
            hashlib.sha256(manifest).hexdigest() != identity["manifest_sha256"]):
        raise ValueError("libarchive COPYING or library manifest differs from its primary pin")
    section = manifest[manifest.index(b"include_HEADERS"):manifest.index(b"# Manpages to install")]
    manifest_paths = {path.decode("ascii") for path in re.findall(
        rb"libarchive/[A-Za-z0-9_.-]+\.(?:h\.in|c|h)(?![A-Za-z0-9_.-])", section)}
    if len(sources) != identity["source_count"] or set(sources) != manifest_paths:
        raise ValueError("libarchive notice source scope differs from its library manifest")
    return _libarchive_corpus(sources)


def _libarchive_notices(cache_root):
    """Materialize only the three pinned notices, deriving the corpus ephemerally."""
    from appimage.acquire import _download
    from dependency_sources import _directory
    contract = _COMPONENT_NOTICES["libarchive"]
    directory = cache_root / "notices" / contract["notice_cache"]
    _directory(cache_root, directory)
    result = []
    for name, pin in (("COPYING", contract), *contract["supplemental_notices"].items()):
        if name != "library-notices.txt":
            path = _download(directory, name, pin["origin"], pin["sha256"])
        else:
            path = directory / name
            if path.exists() or path.is_symlink():
                _regular(path)
            else:
                with tempfile.TemporaryDirectory(prefix=".libarchive-notices-", dir=directory) as temporary:
                    identity = _LIBARCHIVE_NOTICE_SOURCE
                    archive = _download(Path(temporary), "source.tar.gz",
                                        identity["archive_url"], identity["archive_sha256"])
                    data = _libarchive_archive_corpus(archive)
                    if len(data) != pin["size"] or hashlib.sha256(data).hexdigest() != pin["sha256"]:
                        raise ValueError("derived libarchive notice corpus differs from its pin")
                    with tempfile.NamedTemporaryFile(dir=directory, prefix=".notice-", delete=False) as output:
                        staged = Path(output.name)
                        try:
                            output.write(data)
                            output.flush()
                            staged.chmod(0o644)
                            try:
                                os.link(staged, path)
                            except FileExistsError:
                                # A concurrent owner may publish the same authenticated material.
                                _regular(path)
                        finally:
                            staged.unlink(missing_ok=True)
        _regular(path)
        if path.stat().st_size != pin["size"] or _digest(path) != pin["sha256"]:
            raise ValueError(f"libarchive notice cache differs from its pin: {name}")
        result.append(path)
    return result


def _source_archive_notices(archive, contract):
    """Retain complete pinned source members without extracting or rewriting bytes."""
    identity = contract["notice_source"]
    _regular(archive)
    if archive.stat().st_size != identity["archive_size"] or _digest(archive) != identity["archive_sha256"]:
        raise ValueError("component notice source archive differs from its pin")
    pins = {pin["source_member"]: pin
            for pin in (contract, *contract.get("supplemental_notices", {}).values())}
    result, seen = {}, set()
    with tarfile.open(archive, "r:gz") as stream:
        for member in stream:
            name = member.name.rstrip("/") if member.isdir() else member.name
            if (not name or name.startswith("/") or "\\" in name or not name.isascii()
                    or any(part in {"", ".", ".."} for part in name.split("/"))
                    or any(ord(char) < 32 or ord(char) == 127 for char in name)
                    or name in seen):
                raise ValueError("unsafe or duplicate component notice source member")
            seen.add(name)
            if any(selected.startswith(name + "/") for selected in pins) and not member.isdir():
                raise ValueError("component notice source parent is not a directory")
            pin = pins.get(name)
            if pin is None:
                continue
            if (not member.isfile() or member.size != pin["size"]
                    or not 0 < member.size <= MAX_FILE or member.mode != pin["mode"]):
                raise ValueError(f"component notice source is not its pinned regular member: {name}")
            with stream.extractfile(member) as source:
                data = source.read(pin["size"] + 1)
            if len(data) != pin["size"] or hashlib.sha256(data).hexdigest() != pin["sha256"]:
                raise ValueError(f"component notice source bytes differ from their pin: {name}")
            result[name] = data
    if set(result) != set(pins):
        raise ValueError("component notice source omits its complete grant members")
    return result


def _cached_source_notices(cache_root, source, contract, provenance, members):
    """Validate the existing complete source record against this signed runtime identity."""
    from dependency_sources import IDENTITY, _directory, _safe_file, _validate

    if not isinstance(provenance, dict) or not isinstance(members, dict):
        raise ValueError("cached component notices require authenticated runtime source inputs")
    binding = _notice_source_binding(source, provenance, members)
    if binding is None:
        raise ValueError("cached component notices require an exact component source binding")
    identity = contract["notice_source"]
    inventory = json.loads(_safe_file(cache_root, "provenance.json").read_text(encoding="utf-8"))
    if inventory.get("format") != 1 or not isinstance(inventory.get("packages"), list):
        raise ValueError("invalid dependency source inventory for component notices")
    records = [item for item in inventory["packages"] if all(
        item.get(field) == provenance.get(field) for field in ("manager", "package", "version"))]
    if len(records) != 1:
        raise ValueError("component notices require one complete exact cached source record")
    expected = {field: provenance.get(field) for field in IDENTITY}
    expected.update(license_expression=contract["license_expression"],
                    pkgbuild_sha256=binding["pkgbuild_sha256"])
    record = records[0]
    _validate(cache_root, record, expected)
    authentication = record["authentication"]
    if authentication.get("binary_archive", {}).get("sha256") != provenance["binary_package"]["sha256"]:
        raise ValueError("cached component source recipe belongs to a different signed binary archive")
    materials = [item for item in record["materials"] if item["path"] == identity["archive_path"]]
    declarations = [item for item in authentication["recipe_sources"]
                    if item.get("material_path") == identity["archive_path"]]
    if (len(materials) != 1 or len(declarations) != 1
            or any(materials[0].get(field) != value for field, value in (
                ("role", "source"), ("sha256", identity["archive_sha256"]), ("size", identity["archive_size"])))
            or any(declarations[0].get(field) != value for field, value in (
                ("declared_source", identity["declared_source"]), ("git_commit", identity["commit"]),
                ("canonical_archive_sha256", identity["canonical_archive_sha256"])))):
        raise ValueError("cached component notice archive lacks its exact recipe-pinned source binding")
    data = _source_archive_notices(_safe_file(cache_root, identity["archive_path"]), contract)
    result = []
    for name, content in data.items():
        directory = cache_root / "notices" / contract["notice_cache"] / Path(name).parent
        _directory(cache_root, directory)
        path = directory / Path(name).name
        if not path.exists() and not path.is_symlink():
            with tempfile.NamedTemporaryFile(dir=directory, prefix=".notice-", delete=False) as output:
                staged = Path(output.name)
                try:
                    output.write(content)
                    output.flush()
                    staged.chmod(0o644)
                    try:
                        os.link(staged, path)
                    except FileExistsError:
                        pass  # The same pinned bytes may have been published concurrently.
                finally:
                    staged.unlink(missing_ok=True)
        _regular(path)
        if path.read_bytes() != content:
            raise ValueError(f"component notice cache differs from its complete source bytes: {name}")
        result.append(path)
    return result


def _component_notice_supplements(cache_root, source, source_package, source_version,
                                 *, provenance=None, members=None):
    """Acquire pinned additional notices only for the authenticated component release."""
    contract = _COMPONENT_NOTICES.get(source_package)
    if (not contract or not contract.get("supplemental_notices") or
            source_version != contract["source_version"] or
            not re.fullmatch(contract["library"], source.name)):
        return []
    if "notice_source" in contract:
        if contract["notice_source"]["collector"] == "cached-source-members-v1":
            return _cached_source_notices(cache_root, source, contract, provenance, members)
        return _libarchive_notices(cache_root)
    from appimage.acquire import _download
    from dependency_sources import _directory
    result = []
    for relative, pin in contract["supplemental_notices"].items():
        directory = cache_root / "notices" / contract["notice_cache"] / Path(relative).parent
        _directory(cache_root, directory)
        result.append(_download(directory, Path(relative).name, pin["origin"], pin["sha256"]))
    return result


def _dependency_target(name, resolved, cache, dbus_source):
    """Override only the explicitly authenticated D-Bus SONAME, even if ldd found the host."""
    if dbus_source is not None and name == "libdbus-1.so.3":
        return dbus_source
    return resolved.get(name) or cache.get(name)


def deploy(stage: Path, innoextract_dir: Path, dependency_sources_dir: Path | None = None) -> list[str]:
    """Return every newly installed regular relative path, sorted and unique.

    Input extractor directory is the digest-verified innoextract-1.9-linux archive
    root produced by packaging/appimage/acquire.py. License failures are fatal.
    Caller patches project bin/* RPATH and signs the resulting exact file list.
    """
    if sys.platform != "linux" or platform.machine() not in {"x86_64", "AMD64"}:
        raise ValueError("runtime deployment supports only Linux x86_64")
    for tool in ("patchelf", "readelf", "ldd", "ldconfig"):
        if not shutil.which(tool):
            raise ValueError(f"required runtime deployment tool missing: {tool}")
    if sys.version_info.major != 3 or sys.version_info.minor not in PYTHON_MINORS:
        raise ValueError("runtime deployment requires distro Python 3.11 through 3.14")
    stage = Path(stage)
    if not stat.S_ISDIR(stage.lstat().st_mode):
        raise ValueError("stage must be a real directory")
    stage = stage.resolve(strict=True)
    innoextract_dir = Path(innoextract_dir)
    if not stat.S_ISDIR(innoextract_dir.lstat().st_mode):
        raise ValueError("extractor root must be a real directory")
    innoextract_dir = innoextract_dir.resolve(strict=True)
    bundled_vulkan = _vulkan_runtime_required(stage)
    dependency_sources_dir = (Path(dependency_sources_dir) if dependency_sources_dir is not None
                              else Path(__file__).resolve().parents[1] / "work/tools/dependency-sources")
    dependency_sources_dir = dependency_sources_dir.absolute()
    distribution_info = platform.freedesktop_os_release()
    distribution = {"id": distribution_info.get("ID", ""),
                    "version_id": distribution_info.get("VERSION_ID", "")}
    if not distribution["id"]:
        raise ValueError("missing build distribution identity")
    components, installed, packages, excluded = {}, set(), {}, {}
    package_notice_text = {}
    package_binary_members = {}
    package_notice_entries = {}
    library_sources = {}
    total = 0
    library_dirs = []
    explicit_dirs = os.environ.get("C4_RUNTIME_LIBRARY_PATH", "")
    if explicit_dirs:
        if len(explicit_dirs) > 4096 or len(explicit_dirs.split(":")) > 8:
            raise ValueError("explicit runtime search path is too large")
        for name in explicit_dirs.split(":"):
            directory = Path(name)
            if not directory.is_absolute() or not stat.S_ISDIR(directory.lstat().st_mode):
                raise ValueError("explicit runtime search directories must be real absolute directories")
            library_dirs.append(directory.resolve(strict=True))
    ffmpeg_metadata = None
    ffmpeg_prefix = None
    ffmpeg_sources = {}
    if os.environ.get("C4_FFMPEG_PREFIX"):
        ffmpeg_metadata = validate_ffmpeg_prefix(Path(os.environ["C4_FFMPEG_PREFIX"]))
        ffmpeg_prefix = Path(ffmpeg_metadata["prefix"])
        ffmpeg_library_dir = ffmpeg_prefix / "lib"
        if ffmpeg_library_dir not in library_dirs:
            library_dirs.append(ffmpeg_library_dir)
        for record in ffmpeg_metadata["built_libraries"]:
            ffmpeg_sources[record["soname"]] = ffmpeg_prefix / record["path"]
    dbus_metadata = None
    dbus_prefix = None
    dbus_source = None
    if "C4_DBUS_PREFIX" in os.environ:
        dbus_metadata = validate_dbus_prefix(Path(os.environ["C4_DBUS_PREFIX"]))
        dbus_prefix = Path(dbus_metadata["prefix"])
        dbus_source = dbus_prefix / dbus_metadata["built_libraries"][0]["path"]
        # Do not add this directory to ldd's search path: only this SONAME is owned.

    def source_label(path):
        path = Path(path).resolve(strict=True)
        if path.is_relative_to(stage):
            return path.relative_to(stage).as_posix()
        if path.is_relative_to(innoextract_dir):
            return "innoextract-1.9-linux/" + path.relative_to(innoextract_dir).as_posix()
        if dbus_prefix and path.is_relative_to(dbus_prefix):
            return f"D-Bus-{dbus_metadata['version']}/" + path.relative_to(dbus_prefix).as_posix()
        packaging_dir = Path(__file__).resolve().parent
        if path.is_relative_to(dependency_sources_dir):
            return "dependency-sources/" + path.relative_to(dependency_sources_dir).as_posix()
        if path.is_relative_to(packaging_dir / "appimage"):
            return "packaging/" + path.relative_to(packaging_dir).as_posix()
        if ffmpeg_prefix and path.is_relative_to(ffmpeg_prefix):
            return f"FFmpeg-{ffmpeg_metadata['version']}/" + path.relative_to(ffmpeg_prefix).as_posix()
        for directory in library_dirs:
            prefix = directory.parent
            if prefix not in {Path("/"), Path("/usr")} and path.is_relative_to(prefix):
                return "SDL3-3.4.18/" + path.relative_to(prefix).as_posix()
        return str(path)

    def destination(name):
        if not allowed_runtime_path(name):
            raise ValueError(f"runtime path outside fixed contract: {name}")
        target = stage / name
        for parent in reversed(target.parents):
            if parent == stage or stage in parent.parents:
                if parent.exists() or parent.is_symlink():
                    if not stat.S_ISDIR(parent.lstat().st_mode):
                        raise ValueError(f"runtime parent is not a real directory: {parent}")
                else:
                    parent.mkdir()
        if target.exists() or target.is_symlink():
            raise ValueError(f"runtime path collision: {name}")
        return target

    def finish(target, provenance, glibc=(), isa_needed=None):
        nonlocal total
        _regular(target)
        total += target.stat().st_size
        if total > MAX_TOTAL:
            raise ValueError("runtime files exceed 1 GiB")
        name = target.relative_to(stage).as_posix()
        installed.add(name)
        components[name] = {"path": name, "sha256": _digest(target),
                            "size": target.stat().st_size, "provenance": provenance,
                            "glibc_max": ".".join(map(str, glibc)) or None,
                            "gnu_x86_isa_needed": isa_needed}

    def copy(source, name, provenance, rpath=None, executable=False):
        _regular(source)
        glibc = _elf(source)[2] if rpath is not None else ()
        target = destination(name)
        with source.open("rb") as src, target.open("xb") as dst:
            shutil.copyfileobj(src, dst, 1024 * 1024)
        if provenance.get("original_sha256") and _digest(target) != provenance["original_sha256"]:
            raise ValueError(f"pinned runtime input changed while copying: {name}")
        target.chmod(0o755 if executable else 0o644)
        if rpath is not None:
            _run(["patchelf", "--force-rpath", "--set-rpath", rpath, str(target)])
        finish(target, provenance, glibc,
               _gnu_x86_isa_needed(target) if rpath is not None else None)

    def aggregate_licenses(sources, name, provenance, notice_pins=None):
        target = destination(name)
        records = []
        size = 0
        with target.open("xb") as output:
            for source in sorted(set(sources), key=source_label):
                binding = (notice_pins or {}).get(source)
                if binding is not None:
                    source, data = _read_pacman_notice(source, binding["entries"])
                else:
                    source = source.resolve(strict=True)
                    _regular(source, 4 * 1024 * 1024)
                    data = source.read_bytes()
                header = f"\n===== {source.name} =====\n".encode("utf-8")
                size += len(header) + len(data) + 1
                if size > 4 * 1024 * 1024:
                    raise ValueError(f"aggregate license exceeds 4 MiB: {name}")
                output.write(header)
                output.write(data)
                output.write(b"\n")
                records.append({"source": source_label(source),
                                "sha256": hashlib.sha256(data).hexdigest()})
                if binding is not None:
                    entry = binding["entries"][source.relative_to(Path("/")).as_posix()]
                    records[-1]["binary_member"] = {
                        "path": source.relative_to(Path("/")).as_posix(),
                        "sha256": entry["sha256digest"], "size": len(data),
                        "mode": entry["mode"],
                        "mtree_sha256": binding["binary_package"]["mtree_sha256"]}
                    if binding["package"] != provenance["package"]:
                        records[-1]["notice_owner"] = {
                            key: binding[key] for key in ("package", "version", "binary_package")}
        target.chmod(0o644)
        finish(target, dict(provenance, notices=records))
        return records

    def package_provenance(source):
        source = source.resolve(strict=True)
        if dbus_metadata and re.fullmatch(r"libdbus-1\.so(?:\.[0-9.]+)?", source.name):
            if source != dbus_source:
                raise ValueError("explicit owned D-Bus cannot be replaced by a host or unverified library")
            key = ("upstream-source-build", "dbus")
            if key not in packages:
                notice_name = "share/licenses/citroen-c4-robot/dbus/LICENSE.txt"
                metadata = dict(
                    dbus_metadata, type="upstream-source-build", package="D-Bus",
                    license_expression=dbus_metadata["license"], license_paths=[notice_name],
                    source_required=True, license_alternatives="AFL-2.1 OR GPL-2.0-or-later")
                notice = next(item for item in dbus_metadata["licenses"] if item["path"] == "LICENSE.txt")
                copy(dbus_prefix / "share/licenses/D-Bus/LICENSE.txt", notice_name,
                     dict(metadata, type="upstream-license", original_sha256=notice["sha256"]))
                packages[key] = metadata
            record = dbus_metadata["built_libraries"][0]
            return dict(packages[key], source=source_label(source), original_sha256=record["sha256"])
        match = re.fullmatch(r"lib(avcodec|avformat|avutil|swscale)\.so\.[0-9.]+", source.name)
        if match:
            if not ffmpeg_metadata or source not in ffmpeg_sources.values():
                raise ValueError("production FFmpeg libraries must come from the verified minimal LGPL prefix")
            key = ("upstream-source-build", "ffmpeg")
            if key not in packages:
                notice_name = "share/licenses/citroen-c4-robot/ffmpeg/LICENSE.txt"
                metadata = {
                    "type": "upstream-source-build", "package": "FFmpeg",
                    "version": ffmpeg_metadata["version"], "url": ffmpeg_metadata["url"],
                    "source_sha256": ffmpeg_metadata["source_sha256"],
                    "license_expression": ffmpeg_metadata["license"], "license_paths": [notice_name],
                    "lock_sha256": ffmpeg_metadata["lock_sha256"],
                    "proof": ffmpeg_metadata["proof"],
                    "configuration": [{"path": item["path"], "sha256": item["sha256"]}
                                      for item in ffmpeg_metadata["configuration"]],
                    "source_terms": ffmpeg_metadata["source_terms"]}
                notice = next(item for item in ffmpeg_metadata["licenses"]
                              if item["path"] == "share/licenses/FFmpeg/LICENSE.txt")
                copy(ffmpeg_prefix / notice["path"], notice_name,
                     dict(metadata, type="upstream-license", original_sha256=notice["sha256"]))
                packages[key] = metadata
            record = next(item for item in ffmpeg_metadata["built_libraries"]
                          if ffmpeg_prefix / item["path"] == source)
            return dict(packages[key], source=source_label(source), original_sha256=record["sha256"])
        sdl_license = os.environ.get("C4_SDL_SOURCE_LICENSE")
        if (sdl_license and source.parent in library_dirs
                and re.fullmatch(r"libSDL3\.so\.[0-9.]+", source.name)):
            if _elf(source)[1] != "libSDL3.so.0":
                raise ValueError("rootless SDL build has an unexpected SONAME")
            license_source = Path(sdl_license)
            expected = source.parent.parent / "share/licenses/SDL3/LICENSE.txt"
            _regular(license_source, 4 * 1024 * 1024)
            if license_source.resolve(strict=True) != expected.resolve(strict=True):
                raise ValueError("rootless SDL notice is outside the explicit install prefix")
            if _digest(license_source) != "1c040b8271b37e5076359f8fd54240e371114112924d2df81ef87c7d6a1dfdfd":
                raise ValueError("rootless SDL license does not match the pinned upstream release")
            key = ("upstream-source-build", "sdl3")
            if key not in packages:
                notice_name = "share/licenses/citroen-c4-robot/sdl3/LICENSE.txt"
                metadata = {
                    "type": "upstream-source-build", "package": "SDL3", "version": "3.4.18",
                    "url": "https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz",
                    "source_sha256": "9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3",
                    "license_expression": "Zlib",
                    "license_paths": [notice_name]}
                aggregate_licenses([license_source], notice_name, metadata)
                packages[key] = metadata
            return dict(packages[key], source=source_label(source))
        if shutil.which("pacman"):
            package = _run(["pacman", "-Qqo", str(source)]).strip()
            version_line = _run(["pacman", "-Q", package]).strip().split()
            if len(version_line) != 2 or version_line[0] != package:
                raise ValueError(f"invalid pacman package identity: {source}")
            version, manager = version_line[1], "pacman"
        elif shutil.which("dpkg-query"):
            aliases = [str(source)]
            if str(source).startswith(("/usr/lib/", "/usr/bin/")):
                aliases.append(str(source).removeprefix("/usr"))
            packages_found = set()
            for alias in aliases:
                try:
                    owners = _run(["dpkg-query", "-S", alias]).splitlines()
                except subprocess.CalledProcessError:
                    continue
                packages_found.update(row.rsplit(": ", 1)[0] for row in owners
                                      if row.endswith(": " + alias))
            if len(packages_found) != 1:
                raise ValueError(f"missing/ambiguous Debian package owner: {source}")
            package = packages_found.pop()
            version = _run(["dpkg-query", "-W", "-f=${Version}", package]).strip()
            manager = "dpkg"
        else:
            raise ValueError("license provenance requires pacman or dpkg-query")
        key = (manager, package)
        if key not in packages:
            if manager == "pacman":
                source_identity = _pacman_source_identity(package, version, dependency_sources_dir)
                package_binary_members[key] = source_identity.pop("_binary_members", {})
                package_notice_entries[key] = source_identity.pop("_binary_notice_entries", {})
                if not package_notice_entries[key]:
                    raise ValueError(f"distro notices require an authenticated exact pacman archive: {package}")
            else:
                identity = _run(["dpkg-query", "-W",
                                 "-f=${Version}\t${source:Package}\t${source:Version}", package]).strip().split("\t")
                if len(identity) != 3 or identity[0] != version or not all(identity):
                    raise ValueError(f"missing exact Debian source identity: {package}")
                source_identity = {"source_package": identity[1], "source_version": identity[2],
                                   "source_identity_evidence": {"type": "installed-dpkg-query-source-fields"}}
            safe_package = package.split(":", 1)[0]
            license_sources = []
            license_expression = None
            license_declarations = []
            notice_pins = {}
            if manager == "pacman":
                entries = package_notice_entries[key]
                binding = {"entries": entries, "package": package, "version": version,
                           "binary_package": source_identity["binary_package"]}

                def retain_notice(path, pin):
                    resolved = _resolve_pacman_notice(path, pin["entries"])
                    if not resolved.is_file():
                        raise ValueError(f"distro notice is not a regular file: {path}")
                    license_sources.append(path)
                    notice_pins[path] = pin

                listing = _run(["pacman", "-Ql", package])
                for row in listing.splitlines():
                    filename = row.partition(" ")[2]
                    path = Path(filename)
                    if (re.fullmatch(r"/usr/lib/python3\.[0-9]+/LICENSE\.txt", filename)
                            or (filename.startswith("/usr/share/doc/") and path.name in {
                                "COPYING.LGPL", "COPYING.0BSD"})):
                        retain_notice(path, binding)
                    if filename.startswith("/usr/share/licenses/"):
                        candidate = _resolve_pacman_notice(path, entries)
                        if candidate.is_file():
                            retain_notice(path, binding)
                        elif candidate.is_dir() and candidate != Path("/usr/share/licenses"):
                            owner = _run(["pacman", "-Qqo", str(candidate)]).strip()
                            pin = binding
                            if owner != package:
                                # Only an authenticated directory alias authorizes a second owner.
                                if candidate == path or not any(
                                        entries.get(parent.relative_to(Path("/")).as_posix(), {}).get("type") == "link"
                                        for parent in (path, *path.parents) if parent != Path("/")):
                                    raise ValueError(f"distro notice directory has an unrelated owner: {path}")
                                identity = _run(["pacman", "-Q", owner]).strip().split()
                                if len(identity) != 2 or identity[0] != owner:
                                    raise ValueError(f"invalid pacman notice owner identity: {owner}")
                                target_identity = _pacman_source_identity(owner, identity[1], dependency_sources_dir)
                                target_entries = target_identity.pop("_binary_notice_entries", {})
                                target_identity.pop("_binary_members", None)
                                target_directory = candidate.relative_to(Path("/")).as_posix()
                                if target_entries.get(target_directory, {}).get("type") != "dir":
                                    raise ValueError(f"notice alias owner lacks an authenticated target directory: {owner}")
                                combined = dict(target_entries)
                                for member, attrs in entries.items():
                                    if attrs.get("type") == "link":
                                        if member in combined and combined[member] != attrs:
                                            raise ValueError(f"conflicting authenticated notice alias: {member}")
                                        combined[member] = attrs
                                pin = {"entries": combined, "package": owner, "version": identity[1],
                                       "binary_package": target_identity["binary_package"]}
                            owned = _run(["pacman", "-Ql", owner])
                            for entry in owned.splitlines():
                                member = Path(entry.partition(" ")[2])
                                if (member.is_relative_to(candidate)
                                        and (member.is_file() or member.is_symlink())):
                                    retain_notice(path / member.relative_to(candidate), pin)
                # Keep installed package declarations as written; resolve mixed components below.
                license_declarations = source_identity["package_license_declarations"]
                license_ids = [item for declaration in license_declarations for item in declaration.split()]
                license_expression = (license_declarations[0] if len(license_declarations) == 1
                                      else " AND ".join("(" + item + ")" for item in license_declarations))
                if source_identity["source_package"] == "e2fsprogs" and re.fullmatch(
                        r"libcom_err\.so(?:\.[0-9]+)*", source.name):
                    license_sources = _com_err_notices(dependency_sources_dir, source_identity["source_version"])
                license_sources.extend(_component_notice_supplements(
                    dependency_sources_dir, source, source_identity["source_package"],
                    source_identity["source_version"],
                    provenance={"manager": manager, "package": package, "version": version,
                                **source_identity, "distribution": distribution,
                                "license_expression": license_expression, "original_sha256": _digest(source)},
                    members=package_binary_members[key]))
                for identifier in license_ids:
                    identifier = identifier.strip("()")
                    if not identifier or identifier in {"AND", "OR", "WITH"}:
                        continue
                    found = _standard_license_text(identifier)
                    if found is not None:
                        license_sources.append(found.resolve(strict=True))
                    elif not license_sources:
                        raise ValueError(f"missing license text for {package}: {identifier}")
            else:
                license_sources = [Path("/usr/share/doc") / safe_package / "copyright"]
                text = license_sources[0].read_text(encoding="utf-8")
                license_declarations = sorted(set(re.findall(r"^License:\s*(.+)$", text, re.MULTILINE)))
                if source_identity["source_package"] == "e2fsprogs" and re.fullmatch(
                        r"libcom_err\.so(?:\.[0-9]+)*", source.name):
                    license_expression = "LicenseRef-MIT-SIPB"
                else:
                    license_expression = _debian_expression(text)
                for filename in sorted(set(re.findall(r"/usr/share/common-licenses/([A-Za-z0-9.+-]+)", text))):
                    license_sources.append(Path("/usr/share/common-licenses") / filename.rstrip("."))
            if (manager == "pacman" and any("GCC-exception-3.1" in item for item in license_declarations)):
                exception = innoextract_dir / "license/libstdc++.COPYING.RUNTIME"
                _regular(exception, 1024 * 1024)
                if _digest(exception) != "9d6b43ce4d8de0c878bf16b54d8e7a10d9bd42b75178153e3af6a815bdc90f74":
                    raise ValueError("GCC runtime exception notice differs from pinned original")
                license_sources.append(exception)
            if not license_sources:
                raise ValueError(f"no license/copyright text for {package}")
            package_notice_text[key] = "\n".join(
                (_read_pacman_notice(path, notice_pins[path]["entries"])[1].decode("utf-8")
                 .replace("\r\n", "\n").replace("\r", "\n")
                 if path in notice_pins else path.read_text(encoding="utf-8"))
                for path in license_sources)
            provenance = {"type": "distro", "manager": manager, "package": package,
                          "version": version, **source_identity, "distribution": distribution,
                          "license_paths": [], "license_expression": license_expression,
                          "license_declarations": license_declarations}
            name = f"share/licenses/citroen-c4-robot/{safe_package}/LICENSE.txt"
            provenance["license_notices"] = aggregate_licenses(license_sources, name, {
                "type": "license", "manager": manager, "package": package, "version": version,
                "license_expression": license_expression, "license_declarations": license_declarations},
                notice_pins=notice_pins)
            provenance["license_paths"].append(name)
            packages[key] = provenance
        provenance = dict(packages[key], source=source_label(source), original_sha256=_digest(source))
        evidence = {"notice_paths": provenance["license_paths"],
                    "scope": "copied component, not packagewide declarations"}
        try:
            notice_binding = _notice_source_binding(source, provenance, package_binary_members.get(key, {}))
            provenance["license_expression"] = _component_terms(
                source, provenance["source_package"], provenance["license_expression"],
                package_notice_text[key], provenance["license_notices"], evidence,
                source_version=provenance["source_version"], source_binding=notice_binding)
            provenance["source_required"] = _source_required(provenance["license_expression"])
        except ValueError as error:
            raise ValueError(
                f"component license classification failed for {source.name} "
                f"({package} {version}, source {provenance['source_package']} "
                f"{provenance['source_version']}): {error}") from error
        if manager == "pacman" and provenance["source_required"]:
            provenance["binary_member"] = _bind_pacman_member(source, provenance, package_binary_members[key])
        if provenance["source_package"] == "dbus":
            provenance["license_alternatives"] = "AFL-2.1 OR GPL-2.0-or-later"
        provenance["component_license_evidence"] = evidence
        return provenance

    cache = {}
    for row in _run(["ldconfig", "-p"]).splitlines():
        match = re.match(r"\s*(\S+)\s+\(([^)]+)\)\s+=>\s+(\S+)", row)
        if match and "x86-64" in match[2]:
            cache.setdefault(match[1], Path(match[3]))
    for directory in library_dirs:
        source = directory / "libSDL3.so.0"
        if source.exists():
            resolved = source.resolve(strict=True)
            if resolved.parent != directory or _elf(resolved)[1] != "libSDL3.so.0":
                raise ValueError("explicit SDL library escapes its trusted directory/SONAME")
            # Explicit trusted build prefix deliberately overrides the distro SDL.
            cache["libSDL3.so.0"] = resolved
    cache.update(ffmpeg_sources)
    if dbus_source is not None:
        cache["libdbus-1.so.3"] = dbus_source

    def mappings(source):
        result = {}
        for row in _run(["ldd", str(source)], library_dirs).splitlines():
            match = re.match(r"\s*(\S+)\s+=>\s+(\S+)", row)
            if match and match[2].startswith("/"):
                result[match[1]] = Path(match[2])
        return result

    def library(source, expected=None):
        if source.is_relative_to(stage):
            _regular(source)
        source = source.resolve(strict=True)
        needed, soname, glibc = _elf(source)
        # Pulse's private helper has no DT_SONAME but has an exact DT_NEEDED name.
        name = soname or expected
        if not name or (expected and name != expected):
            raise ValueError(f"missing/mismatched dependency SONAME: {source} ({expected})")
        relative = "lib/" + name
        if not allowed_runtime_path(relative):
            raise ValueError(f"unapproved runtime library: {name}")
        if relative in library_sources:
            if source == stage / relative:
                _regular(source)
                if _digest(source) != components[relative]["sha256"]:
                    raise ValueError(f"installed runtime library changed: {relative}")
                return
            if library_sources[relative] != source and _digest(library_sources[relative]) != _digest(source):
                raise ValueError(f"runtime SONAME collision: {name}")
            return
        library_sources[relative] = source
        copy(source, relative, package_provenance(source), "$ORIGIN")
        dependencies(source, needed)

    def dependencies(source, needed=None):
        if needed is None:
            needed = _elf(source)[0]
        resolved = mappings(source)
        for name in needed:
            core = re.fullmatch(r"lib(avcodec|avformat|avutil|swscale)\.so(?:\.[0-9.]+)?", name)
            if core:
                if not ffmpeg_metadata:
                    raise ValueError("C4_FFMPEG_PREFIX with verified minimal LGPL FFmpeg is required")
                record = next(item for item in ffmpeg_metadata["libraries"] if item["name"] == core[1])
                if name != record["soname"]:
                    raise ValueError(f"native FFmpeg ABI {name} differs from pinned {record['soname']}; rebuild against the pinned prefix")
                library(ffmpeg_sources[name], name)
                continue
            if _host_dependency(name, bundled_vulkan=bundled_vulkan):
                excluded.setdefault(name, set()).add(source_label(source))
                continue
            target = _dependency_target(name, resolved, cache, dbus_source)
            if target is None:
                raise ValueError(f"unresolved runtime dependency {name}: {source}")
            library(target, name)

    if bundled_vulkan:
        loader = cache.get("libvulkan.so.1")
        if loader is None:
            raise ValueError("full Vulkan shader payload requires the distro libvulkan.so.1 loader")
        library(loader, "libvulkan.so.1")

    for name in ("citroen-c4-robot", "c4-launcher", "c4-updater"):
        dependencies(stage / "bin" / name)

    lock_path = Path(__file__).parent / "appimage" / "tools.lock.json"
    tool_locks = json.loads(lock_path.read_text(encoding="utf-8"))["tools"]
    lock = tool_locks["innoextract"]
    if (lock["binary_path"] != "innoextract-1.9-linux/bin/amd64/innoextract"
            or set(lock["licenses"]) != {
                "innoextract-1.9-linux/license/" + filename for filename in INNO_LICENSES}):
        raise ValueError("trusted extractor archive layout does not match runtime contract")
    extractor = innoextract_dir / "bin/amd64/innoextract"
    _elf(extractor)
    if _digest(extractor) != lock["binary_sha256"]:
        raise ValueError("trusted extractor binary digest mismatch")
    extractor_provenance = {"type": "upstream-archive", "name": "innoextract",
                            "source": source_label(extractor),
                            "version": lock["release"], "url": lock["url"],
                            "archive_sha256": lock["sha256"], "license_paths": []}
    license_name = "share/licenses/citroen-c4-robot/innoextract/LICENSE.txt"
    extractor_notices = []
    for filename in INNO_LICENSES:
        source = innoextract_dir / "license" / filename
        _regular(source, 4 * 1024 * 1024)
        expected = lock["license_sha256"]["innoextract-1.9-linux/license/" + filename]
        if _digest(source) != expected:
            raise ValueError(f"trusted extractor notice digest mismatch: {filename}")
        extractor_notices.append(source)
    aggregate_licenses(extractor_notices, license_name,
                       {"type": "upstream-license", "archive_sha256": lock["sha256"]})
    extractor_provenance["license_paths"].append(license_name)
    copy(extractor, "tools/innoextract", extractor_provenance, "$ORIGIN/../lib", True)
    dependencies(extractor)

    runtime_lock = tool_locks["runtime"]
    runtime_notices = runtime_lock["notices"]
    if not runtime_notices:
        raise ValueError("AppImage runtime must provide authenticated upstream notices")
    notice_sources = []
    for notice in runtime_notices:
        if not re.fullmatch(r"runtime-[A-Za-z0-9-]{1,64}\.txt", notice["path"]):
            raise ValueError("invalid AppImage runtime notice filename")
        source = lock_path.parent / notice["path"]
        _regular(source, 4 * 1024 * 1024)
        if _digest(source) != notice["sha256"]:
            raise ValueError(f"AppImage runtime notice digest mismatch: {notice['path']}")
        notice_sources.append(source)
    aggregate_licenses(notice_sources, "share/licenses/citroen-c4-robot/appimage-runtime/LICENSE.txt", {
        "type": "upstream-license", "archive_sha256": runtime_lock["sha256"],
        "version": runtime_lock["commit"], "notices_provenance": runtime_notices})

    interpreter = Path(sys.executable).resolve(strict=True)
    python_provenance = package_provenance(interpreter)
    copy(interpreter, "runtime/bin/python3", python_provenance, "$ORIGIN/../../lib", True)
    dependencies(interpreter)
    version = f"3.{sys.version_info.minor}"
    stdlib = Path(sysconfig.get_path("stdlib")).resolve(strict=True)
    if stdlib != Path(f"/usr/lib/python{version}"):
        raise ValueError("runtime Python must be the package-managed /usr/lib distro interpreter")
    stdlib_provenance = {
        item["package"]: item for item in (
            package_provenance(stdlib / "os.py"),
            package_provenance(stdlib / "ctypes/__init__.py"))}
    stdlib_files = set()
    for item in stdlib_provenance.values():
        if item["manager"] == "pacman":
            paths = [line.partition(" ")[2] for line in
                     _run(["pacman", "-Ql", item["package"]]).splitlines()]
        else:
            paths = _run(["dpkg-query", "-L", item["package"]]).splitlines()
        stdlib_files.update(Path(name) for name in paths if name.endswith(".py"))
    zip_name = f"runtime/lib/python3{sys.version_info.minor}.zip"
    target = destination(zip_name)
    _write_stdlib_archive(target, stdlib, stdlib_files)
    target.chmod(0o644)
    finish(target, {"type": "distro-stdlib-archive", "source": str(stdlib),
                    "packages": [stdlib_provenance[name] for name in sorted(stdlib_provenance)]})
    # Isolated trusted distro imports identify transitive extensions, not site packages.
    script = ("import argparse,ctypes,errno,hashlib,json,mmap,os,pathlib,re,shutil,"
              "signal,stat,subprocess,sys,tempfile,zlib; "
              "hashlib.sha256(); hashlib.new('md5'); hashlib.new('sha1'); "
              "parser=argparse.ArgumentParser(); parser.add_argument('--installer'); parser.format_help(); "
              "print(json.dumps(sorted({m.__file__ for m in sys.modules.values() "
              "if getattr(m,'__file__','') and m.__file__.endswith('.so')})))")
    for filename in json.loads(_run([str(interpreter), "-I", "-S", "-c", script])):
        source = Path(filename).resolve(strict=True)
        if source.parent != stdlib / "lib-dynload":
            raise ValueError(f"stdlib extension outside trusted distro directory: {source}")
        name = f"runtime/lib/python{version}/lib-dynload/{source.name}"
        copy(source, name, package_provenance(source), "$ORIGIN/../../../../lib")
        dependencies(source)

    plugin_dirs = [Path("/usr/lib/qt6/plugins"), Path("/usr/lib/x86_64-linux-gnu/qt6/plugins")]
    platform_dirs = [path for path in plugin_dirs if (path / "platforms/libqxcb.so").is_file()]
    if len(platform_dirs) != 1:
        raise ValueError("expected one distro Qt6 xcb plugin directory")
    plugin_dir = platform_dirs[0]
    deployed_plugins = []
    for name in sorted(PLUGIN_PATHS):
        source = plugin_dir / name.removeprefix("plugins/")
        if source.exists():
            source = source.resolve(strict=True)
            copy(source, name, package_provenance(source), "$ORIGIN/../../lib")
            dependencies(source)
            deployed_plugins.append(name)
    if not any("libqwayland" in name for name in deployed_plugins):
        raise ValueError("Qt6 Wayland platform plugins are required")
    if "plugins/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so" not in deployed_plugins:
        raise ValueError("Qt6 Wayland EGL client integration is required for the OpenGL hardware probe")
    if "plugins/wayland-shell-integration/libxdg-shell.so" not in deployed_plugins:
        raise ValueError("Qt6 xdg-shell integration is required for Wayland desktop windows")
    if "plugins/wayland-decoration-client/libbradient.so" not in deployed_plugins:
        raise ValueError("Qt6 Bradient client decorations are required for normal Wayland dialogs")
    if "plugins/tls/libqopensslbackend.so" not in deployed_plugins:
        raise ValueError("Qt6 OpenSSL TLS backend is required for authenticated HTTPS updates")
    for dependency in ("ssl", "crypto"):
        name = ctypes.util.find_library(dependency)
        if not name or name not in cache:
            raise ValueError(f"required OpenSSL runtime dependency missing: {dependency}")
        library(cache[name], name)
    qtconf = destination("bin/qt.conf")
    qtconf.write_text("[Paths]\nPlugins=../plugins\n", encoding="utf-8")
    qtconf.chmod(0o644)
    finish(qtconf, {"type": "project-generated", "purpose": "relative Qt plugin lookup"})
    absent_backends = []
    for backend in SDL_BACKENDS:
        name = "libSDL3.so.0" if backend == "SDL3" and "libSDL3.so.0" in cache else ctypes.util.find_library(backend)
        if name is None:
            absent_backends.append(backend)
            continue
        source = cache.get(name)
        if source is None:
            raise ValueError(f"SDL dynamic backend not resolved by ldconfig: {name}")
        library(source, name)

    maxima = [tuple(map(int, component["glibc_max"].split(".")))
              for component in components.values() if component["glibc_max"]]
    isa_levels = {level for component in components.values()
                  for level in component["gnu_x86_isa_needed"] or ()}
    native_binary_requirements = []
    for binary in ("citroen-c4-robot", "c4-launcher", "c4-updater"):
        source = stage / "bin" / binary
        glibc = _elf(source)[2]
        isa_needed = _gnu_x86_isa_needed(source)
        maxima.append(glibc)
        isa_levels.update(isa_needed or ())
        native_binary_requirements.append({
            "path": "bin/" + binary, "glibc_max": ".".join(map(str, glibc)) or None,
            "gnu_x86_isa_needed": isa_needed})
    manifest = {"schema_version": 1, "os": "linux", "arch": "x86_64",
                "glibc_max": ".".join(map(str, max(maxima, default=()))) or None,
                "gnu_x86_isa_needed_max": max(isa_levels, key=GNU_X86_ISA_LEVELS.__getitem__, default=None),
                "cpu_isa_evidence": "GNU_PROPERTY_X86_ISA_1_NEEDED only; missing properties do not establish baseline CPU compatibility",
                "native_binary_requirements": native_binary_requirements,
                "components": [components[name] for name in sorted(components)],
                "host_dependencies": [{"soname": name, "required_by": sorted(excluded[name]),
                                       "reason": "host libc/loader or GPU/device driver"}
                                      for name in sorted(excluded)],
                "host_excluded_library_bases": sorted(HOST_BASES - {"vulkan"} if bundled_vulkan else HOST_BASES),
                "host_excluded_name_prefixes": ["ld-linux-", "libGLX_", "libEGL_", "libnvidia-"],
                "host_resources": ["system CA certificate trust store"] + (
                    ["system Vulkan ICD manifests and GPU driver libraries"] if bundled_vulkan else []),
                "absent_optional_sdl_backends": sorted(absent_backends)}
    target = destination("share/citroen-c4-robot/runtime-manifest.json")
    target.write_text(json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
    target.chmod(0o644)
    # Self-hash is supplied by the outer authenticated files[] release manifest.
    finish(target, {"type": "project-generated", "purpose": "runtime bill of materials"})
    return sorted(installed)
