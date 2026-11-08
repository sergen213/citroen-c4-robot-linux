#!/usr/bin/env python3
"""Bundle pinned runtime source archives and explicit notices without executing them."""
from __future__ import annotations

import copy
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import tarfile
import tempfile

from .acquire import _download

LOCK = Path(__file__).with_name("runtime-sources.lock.json")
README = Path(__file__).with_name("runtime-SOURCE-README.txt")
FFMPEG_LOCK = LOCK.parent.parent / "ffmpeg.lock.json"
FFMPEG_RECIPE = FFMPEG_LOCK.with_name("build_ffmpeg.py")
DBUS_LOCK = FFMPEG_LOCK.with_name("dbus.lock.json")
DBUS_RECIPE = FFMPEG_LOCK.with_name("build_dbus.py")
ROOT = "runtime-corresponding-source"
CONFIGURATION_ROOT = "share/licenses/FFmpeg/source-proof/"
CONFIGURATION_FILES = ("config.h", "config_components.h", "ffbuild/config.mak",
                       "ffbuild/config.sh")
TOOL_OPTIONS = {"--cc", "--ld", "--ar", "--ranlib", "--strip", "--nm", "--x86asmexe"}
# Prose, diagnostics and argv/search lists delimit paths; punctuation is not
# part of an installation/runtime identity.
PATH_END = r"[/:\s;'\"),\[\]`<>]"
ABSOLUTE_PATH = re.compile(r"(?:^|[\s=:,'\"(\[\]`<>]|-L|-I)(/[A-Za-z0-9_.-][^\s;'\"),\[\]`<>]*)")
# Standalone paths and attached compiler operands share literal identity rules.
ABSOLUTE_OPERAND = re.compile(r"(?:-I|-L)?(/[A-Za-z0-9_.-].*)", re.S)
# Only log diagnostics and colon-separated absolute operands have colon
# delimiters. A literal metadata path such as /usr/lib:private stays unknown.
LOG_ABSOLUTE_PATH = re.compile(
    r"(?:^|[\s=:,'\"(\[\]`<>]|-L|-I)"
    r"(/[A-Za-z0-9_.-](?:(?!:(?:/|\s|$))[^\s;'\"),\[\]`<>])*)")


def _filename(value: str) -> str:
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", value):
        raise ValueError(f"Unsafe source bundle filename: {value!r}")
    return value


def _regular(path: Path) -> None:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"Source bundle input must be a regular file: {path}")


def _entry(name: str, size: int, epoch: int) -> tarfile.TarInfo:
    entry = tarfile.TarInfo(f"{ROOT}/{name}")
    entry.size = size
    entry.mode = 0o644
    entry.uid = entry.gid = 0
    entry.uname = entry.gname = ""
    entry.mtime = epoch
    return entry


def _add_file(archive: tarfile.TarFile, path: Path, name: str,
              epoch: int, digest: str) -> None:
    _regular(path)
    with path.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != digest:
            raise ValueError(f"Source bundle input digest mismatch: {path}")
        stream.seek(0)
        archive.addfile(_entry(name, os.fstat(stream.fileno()).st_size, epoch), stream)


def _reject_absolute(value, *, system_constants: bool = False,
                     allowed_paths: frozenset[str] = frozenset()) -> None:
    if isinstance(value, dict):
        for key, item in value.items():
            _reject_absolute(key, system_constants=system_constants, allowed_paths=allowed_paths)
            _reject_absolute(item, system_constants=system_constants, allowed_paths=allowed_paths)
    elif isinstance(value, list):
        for item in value:
            _reject_absolute(item, system_constants=system_constants, allowed_paths=allowed_paths)
    elif isinstance(value, str):
        allowed = {"/bin/sh", "/usr/include", "/usr/lib", "/usr/lib64"} if system_constants else set()
        allowed.update(allowed_paths)
        # A standalone JSON/path operand is not prose: preserve punctuation in
        # its identity. In particular, unknown /run] is not the default /run.
        operand = ABSOLUTE_OPERAND.match(value)
        if operand:
            if operand[1].rstrip("/") not in allowed:
                raise ValueError(f"Unexplained absolute source provenance path: {operand[1]}")
            return
        for match in ABSOLUTE_PATH.finditer(value):
            if match[1].rstrip("/") not in allowed:
                raise ValueError(f"Unexplained absolute source provenance path: {match[1]}")


def _replace_paths(text: str, replacements: dict[str, str]) -> str:
    literal = bool(ABSOLUTE_OPERAND.match(text))
    boundary = r"[/\s]" if literal else PATH_END
    for original, label in sorted(replacements.items(), key=lambda item: -len(item[0])):
        def replace(match):
            suffix = re.split(r"\s" if literal else r"[:\s;'\"),\[\]`<>]",
                              text[match.end():], maxsplit=1)[0]
            if ".." in suffix.split("/"):
                raise ValueError("Source provenance path traverses its declared root")
            if label.startswith("tool-directory/") and suffix.strip("/"):
                raise ValueError("Undeclared descendant of a tool search directory")
            return label
        text = re.sub(r"(?:(?<![A-Za-z0-9_./-])|(?<=-I)|(?<=-L))" + re.escape(original)
                      + r"(?=$|" + boundary + r")", replace, text)
    return text


def _reject_dbus_log(original: str, portable: str, metadata: dict, lock: dict,
                     allowed: frozenset[str]) -> None:
    """Check typed GCC search candidates and upstream runtime defaults in place.

    Candidates can be nonexistent and contain GCC's authored ../ layout. They
    are not source roots, nor permission to publish arbitrary host directories.
    """
    typed_lines = {}
    gcc = metadata.get("tool_paths", {}).get("gcc")
    if gcc:
        pattern = (r"^Command line: `" + re.escape(gcc)
                   + r" [^\n]* --print-search-dirs` -> 0\nstdout:\n"
                   + r"(install: (/usr/lib/gcc/([A-Za-z0-9][A-Za-z0-9_.-]*)/([0-9]+)/)\n)"
                   + r"(programs: =([^\n]+)\n)(libraries: =([^\n]+))$")
        for match in re.finditer(pattern, original, re.M):
            install, target, version = match[2], match[3], match[4]
            if install.rstrip("/") not in allowed:
                raise ValueError("Undeclared GCC installation directory")
            triplet_version = target + "/" + version + "/"
            programs = {install, install.rsplit("/", 2)[0] + "/",
                        install + "../../../../" + target + "/bin/",
                        install + "../../../../" + target + "/bin/" + triplet_version}
            libraries = {install, install + "../../../",
                         install + "../../../" + triplet_version,
                         install + "../../../../lib/"}
            target_lib = install + "../../../../" + target + "/lib/"
            libraries.update({target_lib, target_lib + triplet_version,
                              target_lib + "../lib/"})
            for base in ("/lib/", "/usr/lib/"):
                libraries.update({base, base + "../lib/", base + triplet_version})
            for line, candidates, expected in (
                    (match[5].rstrip("\n"), match[6].split(":"), programs),
                    (match[7], match[8].split(":"), libraries)):
                if any(candidate not in expected for candidate in candidates):
                    raise ValueError("Unexplained GCC search-directory candidate")
                typed_lines[line] = frozenset(candidate.rstrip("/") for candidate in candidates)
            typed_lines[match[1].rstrip("\n")] = frozenset({install.rstrip("/")})
    runtime = lock.get("meson_options", {}).get("runtime_dir")
    for raw_line, line in zip(original.splitlines(), portable.splitlines(), strict=True):
        permitted = allowed | typed_lines.get(raw_line, frozenset())
        # The owned recipe is Linux x86-64; this is the GNU ld ABI interpreter
        # operand, not a free-form path found somewhere in compiler stdout.
        if (raw_line.startswith("/usr/bin/ld ") and "/usr/bin/ld" in allowed
                and re.search(r"(?:^|\s)-m elf_x86_64(?:\s|$)", raw_line)
                and re.search(r"(?:^|\s)-dynamic-linker /lib64/ld-linux-x86-64\.so\.2(?:\s|$)", raw_line)):
            permitted |= frozenset({"/lib64/ld-linux-x86-64.so.2"})
        pid = re.fullmatch(r"\s*System bus PID file\s*:\s*(\S+)\s*", raw_line)
        if (pid and metadata.get("version") == "1.16.2"
                and isinstance(runtime, str) and runtime in lock.get("default_paths", [])
                and pid[1] == runtime.rstrip("/") + "/dbus/pid"):
            permitted |= frozenset({pid[1]})
        for path in LOG_ABSOLUTE_PATH.findall(line):
            _reject_absolute(path, system_constants=True, allowed_paths=permitted)


def _canonical_ffmpeg(metadata: dict, archive_name: str) -> dict:
    """Replace only declared provenance locations; preserve all integrity evidence."""
    result = copy.deepcopy(metadata)
    prefix = result["prefix"]
    result["prefix"] = "ffmpeg-prefix"
    result["source_cache_path"] = "archive/" + archive_name
    directories = {
        "--prefix": prefix, "--libdir": prefix + "/lib",
        "--shlibdir": prefix + "/lib", "--pkgconfigdir": prefix + "/lib/pkgconfig",
    }
    if "provenance_roots" in result:
        result["provenance_roots"] = {
            key: "ffmpeg-" + key for key in result["provenance_roots"]
        }
    if "tool_paths" in result:
        expected_tools = {
            option.removeprefix("--"): value
            for argument in result["configure_args"]
            for option, separator, value in [argument.partition("=")]
            if option in TOOL_OPTIONS and separator
        }
        if (set(result["tool_paths"]) != {option[2:] for option in TOOL_OPTIONS}
                or result["tool_paths"] != expected_tools):
            raise ValueError("FFmpeg tool provenance differs from actual configure arguments")
        result["tool_paths"] = {
            key: "tool/" + _filename(Path(value).name)
            for key, value in result["tool_paths"].items()
        }
    arguments = []
    for argument in result["configure_args"]:
        option, separator, value = argument.partition("=")
        if option in directories:
            if not separator or value != directories[option]:
                raise ValueError(f"Unexpected FFmpeg configure directory: {argument}")
            argument = option + "=ffmpeg-prefix" + value[len(prefix):]
        elif option in TOOL_OPTIONS:
            if not separator or not Path(value).is_absolute():
                raise ValueError(f"Unexpected FFmpeg configure tool: {argument}")
            argument = option + "=tool/" + _filename(Path(value).name)
        arguments.append(argument)
    result["configure_args"] = arguments

    _reject_absolute(result)
    return result


def _ffmpeg_materials(tools_dir: Path) -> tuple[list, dict]:
    # Import lazily so the absent-prefix runtime-only route keeps its old dependencies.
    from build_ffmpeg import acquire_source
    from runtime_bundle import validate_ffmpeg_prefix

    prefix = Path(os.environ["C4_FFMPEG_PREFIX"])
    if not os.environ["C4_FFMPEG_PREFIX"]:
        raise ValueError("C4_FFMPEG_PREFIX must identify an actual built prefix")
    actual = validate_ffmpeg_prefix(prefix)
    _regular(FFMPEG_LOCK)
    _regular(FFMPEG_RECIPE)
    lock_bytes = FFMPEG_LOCK.read_bytes()
    lock = json.loads(lock_bytes)
    if actual["lock_sha256"] != hashlib.sha256(lock_bytes).hexdigest():
        raise ValueError("FFmpeg source lock differs from the actual built prefix")
    archive_name = _filename(lock["archive"])
    source = acquire_source(tools_dir)
    if source.name != archive_name:
        raise ValueError("FFmpeg acquisition returned an unexpected archive")
    proof = lock["provenance"]
    files = [
        ("ffmpeg/archive/" + archive_name, source, lock["source_sha256"]),
        ("ffmpeg/archive/" + archive_name + ".asc", source.with_name(archive_name + ".asc"),
         proof["signature_sha256"]),
        ("ffmpeg/archive/ffmpeg-devel.asc", source.with_name("ffmpeg-devel.asc"),
         proof["key_sha256"]),
    ]
    configuration = {}
    for record in actual["configuration"]:
        name = record["path"]
        if name not in {CONFIGURATION_ROOT + item for item in CONFIGURATION_FILES} or name in configuration:
            raise ValueError(f"Unexpected FFmpeg configure proof: {name}")
        path = Path(actual["prefix"]) / name
        _regular(path)
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != record["sha256"]:
            raise ValueError(f"FFmpeg configure proof digest mismatch: {name}")
        configuration[name] = data.decode("utf-8")
    if set(configuration) != {CONFIGURATION_ROOT + item for item in CONFIGURATION_FILES}:
        raise ValueError("Missing FFmpeg configure proof")
    roots = actual.get("provenance_roots")
    if roots is None:
        makefile = configuration[CONFIGURATION_ROOT + "ffbuild/config.mak"]
        shell = configuration[CONFIGURATION_ROOT + "ffbuild/config.sh"]
        # Pinned FFmpeg 8.1.3 configure uses source_path='.' for this in-tree recipe.
        if (re.findall(r"^SRC_PATH=(.*)$", makefile, re.M) != ["."]
                or not re.search(r"^source_path=(?:\.|'\.'|\"\.\")$", shell, re.M)):
            raise ValueError("Cannot establish FFmpeg in-tree configure source/build root")
        roots = {"source": ".", "build": ".", "prefix": actual["prefix"]}
    if (set(roots) not in ({"source", "build", "prefix"},
                          {"source", "build", "work", "prefix"})
            or roots["prefix"] != actual["prefix"]
            or any(not isinstance(value, str) or value != "."
                   and (not Path(value).is_absolute() or value == "/")
                   for value in roots.values())):
        raise ValueError("Invalid FFmpeg declared provenance roots")
    if "work" in roots and (Path(roots["source"]).parent != Path(roots["work"])
                           or roots["build"] != roots["source"]):
        raise ValueError("FFmpeg work/source provenance differs from the in-tree recipe")
    replacements = {actual["source_cache_path"]: "archive/" + archive_name}
    for kind in ("work", "build", "source", "prefix"):
        if kind in roots and roots[kind] != ".":
            replacements[roots[kind]] = "ffmpeg-" + kind
    for argument in actual["configure_args"]:
        option, separator, value = argument.partition("=")
        if option in TOOL_OPTIONS and separator:
            replacements[value] = "tool/" + _filename(Path(value).name)
    canonical = _canonical_ffmpeg(actual, archive_name)
    metadata = {
        "ffmpeg/ffmpeg.lock.json": lock_bytes,
        "ffmpeg/build_ffmpeg.py": FFMPEG_RECIPE.read_bytes(),
    }
    for record in canonical["configuration"]:
        text = configuration[record["path"]]
        text = _replace_paths(text, replacements)
        _reject_absolute(text, system_constants=True)
        data = text.encode("utf-8")
        name = "configuration/" + record["path"].removeprefix(CONFIGURATION_ROOT)
        metadata["ffmpeg/" + name] = data
        record["original_sha256"] = record["sha256"]
        record["published_sha256"] = hashlib.sha256(data).hexdigest()
        record["published_path"] = name
    metadata["ffmpeg/ffmpeg-source.json"] = (
        json.dumps(canonical, indent=2, sort_keys=True) + "\n").encode("utf-8")
    return files, metadata

def _dbus_materials(tools_dir: Path) -> tuple[list, dict]:
    from build_dbus import source_materials
    from runtime_bundle import validate_dbus_prefix

    actual = validate_dbus_prefix(Path(os.environ["C4_DBUS_PREFIX"]))
    _regular(DBUS_LOCK)
    _regular(DBUS_RECIPE)
    lock_bytes = DBUS_LOCK.read_bytes()
    lock = json.loads(lock_bytes)
    if (actual["lock_sha256"] != hashlib.sha256(lock_bytes).hexdigest()
            or actual["recipe_sha256"] != hashlib.sha256(DBUS_RECIPE.read_bytes()).hexdigest()):
        raise ValueError("Owned D-Bus source lock/recipe differs from its built prefix")
    files = [("dbus/" + name, path, digest)
             for name, path, digest in source_materials(tools_dir)]
    canonical = copy.deepcopy(actual)
    replacements = {actual["prefix"]: "dbus-prefix",
                    actual["source_cache_path"]: "archive/" + _filename(lock["archive"])}
    for kind, path in actual.get("provenance_roots", {}).items():
        if path != ".":
            if not isinstance(path, str) or not Path(path).is_absolute() or path == "/":
                raise ValueError("Invalid declared D-Bus source/build root")
            replacements[path] = "dbus-" + _filename(kind)
    tool_directories = {}
    for name, path in actual.get("tool_paths", {}).items():
        replacements[path] = "tool/" + _filename(Path(path).name)
        parent = str(Path(path).parent)
        if not parent.startswith(("/usr/", "/lib/", "/bin/", "/sbin/")) and parent not in {"/bin", "/lib", "/lib64", "/sbin"}:
            tool_directories.setdefault(parent, "tool-directory/" + _filename(name))
    allowed = frozenset(lock.get("default_paths", []) + actual.get("system_paths", []))

    def canonicalize(value):
        if isinstance(value, dict):
            return {key: canonicalize(item) for key, item in value.items()}
        if isinstance(value, list):
            return [canonicalize(item) for item in value]
        if isinstance(value, str):
            value = _replace_paths(value, replacements)
        return value

    canonical = canonicalize(canonical)
    canonical["prefix"] = "dbus-prefix"
    canonical["source_cache_path"] = "archive/" + _filename(lock["archive"])
    path_environment = canonical.get("build_environment", {}).get("PATH")
    if path_environment is not None:
        parts = []
        for part in path_environment.split(os.pathsep):
            # A tool's parent is a typed PATH directory, never a general source
            # root that authorizes other files or executables beneath it.
            part = _replace_paths(part, replacements)
            part = _replace_paths(part, tool_directories)
            _reject_absolute(part, system_constants=True, allowed_paths=allowed)
            parts.append(part)
        path_environment = os.pathsep.join(parts)
        canonical["build_environment"]["PATH"] = path_environment
        # Literal metadata retains the full PATH token, after checking every
        # directory independently; log-list punctuation has separate semantics.
        allowed |= frozenset(match[1] for match in ABSOLUTE_PATH.finditer(path_environment))
    metadata = {"dbus/dbus.lock.json": lock_bytes, "dbus/build_dbus.py": DBUS_RECIPE.read_bytes()}
    canonical["recipe_helpers"] = copy.deepcopy(lock.get("recipe_helpers", []))
    for helper in canonical["recipe_helpers"]:
        parts = helper["path"].split("/")
        for part in parts:
            _filename(part)
        path = DBUS_RECIPE.parent.joinpath(*parts)
        _regular(path)
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != helper["sha256"]:
            raise ValueError("Owned D-Bus imported build helper changed")
        helper["published_path"] = "helpers/" + helper["path"]
        metadata["dbus/" + helper["published_path"]] = data
    proof_root = "share/licenses/D-Bus/source-proof/"
    for record in canonical["configuration"]:
        original = next(item for item in actual["configuration"] if item["path"] == record["path"])
        path = Path(actual["prefix"]) / original["path"]
        _regular(path)
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != original["sha256"]:
            raise ValueError("Owned D-Bus build configuration changed")
        if not original["path"].startswith(proof_root):
            raise ValueError("Owned D-Bus configuration escapes its explicit proof root")
        name = original["path"].removeprefix(proof_root)
        if any(part in {"", ".", ".."} for part in name.split("/")):
            raise ValueError("Unsafe owned D-Bus proof filename")
        text = canonicalize(data.decode("utf-8"))
        if name == "meson-logs/meson-log.txt":
            _reject_dbus_log(data.decode("utf-8"), text, actual, lock, allowed)
        elif name.endswith(".json"):
            # Check decoded original leaves before prose replacement can hide a
            # bracket/colon-bearing literal behind a declared-path alias.
            # This view is validation only; publish the normalized raw bytes.
            _reject_absolute(canonicalize(json.loads(data.decode("utf-8"))),
                             system_constants=True, allowed_paths=allowed)
        else:
            _reject_absolute(text, system_constants=True, allowed_paths=allowed)
        published = text.encode("utf-8")
        metadata["dbus/configuration/" + name] = published
        record["original_sha256"] = original["sha256"]
        record["published_sha256"] = hashlib.sha256(published).hexdigest()
        record["published_path"] = "configuration/" + name
    material_digests = {name.removeprefix("dbus/"): digest for name, _, digest in files}
    for record in canonical.get("authentication_files", []):
        name = record["path"].removeprefix(proof_root)
        published = "proof/" + name
        if material_digests.get(published) != record["sha256"]:
            raise ValueError("Owned D-Bus published authentication differs from its prefix")
        record["published_path"] = published
    _reject_absolute(canonical, system_constants=True, allowed_paths=allowed)
    metadata["dbus/dbus-source.json"] = (json.dumps(canonical, indent=2, sort_keys=True) + "\n").encode()
    return files, metadata


def _dependency_materials(tools_dir: Path, runtime_manifest: dict) -> tuple[list, dict]:
    """Publish only source families needed by the deployed ELF bill of materials."""
    from dependency_sources import IDENTITY, _validate, acquire, required_packages

    required = required_packages(runtime_manifest)
    acquire(runtime_manifest, tools_dir)
    cache = Path(tools_dir).resolve() / "dependency-sources"
    inventory_path = cache / "provenance.json"
    if not required:
        inventory = {"format": 1, "packages": []}
    else:
        _regular(inventory_path)
        inventory = json.loads(inventory_path.read_bytes())
    if inventory.get("format") != 1:
        raise ValueError("Unsupported dependency source inventory")
    identities = IDENTITY
    files = {}
    published = []
    for provenance in required:
        candidates = [record for record in inventory["packages"]
                      if all(record.get(key) == provenance.get(key) for key in identities)]
        if len(candidates) != 1:
            raise ValueError(f"Missing/ambiguous exact corresponding source: "
                             f"{provenance['package']} {provenance['version']}")
        record = copy.deepcopy(candidates[0])
        _validate(cache, record, provenance)
        for material in record["materials"]:
            path = cache / material["path"]
            digest = material["sha256"]
            name = "dependencies/materials/" + digest
            files[name] = (name, path, digest)
            material["published_path"] = name
        record["covered_components"] = [
            {"path": component["path"], "sha256": component["sha256"],
             "original_sha256": component["provenance"].get("original_sha256")}
            for component in runtime_manifest["components"]
            if component["provenance"].get("source_required") is True
            and all(component["provenance"].get(key) == provenance.get(key) for key in identities)
        ]
        published.append(record)
    proof = {"format": 1, "packages": published,
             "runtime_manifest_sha256": hashlib.sha256(
                 json.dumps(runtime_manifest, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
             "packaging_transform": "ELF RPATH rewritten to relative $ORIGIN; original and deployed hashes retained"}
    return list(files.values()), {
        "dependencies/provenance.json": (json.dumps(proof, indent=2, sort_keys=True) + "\n").encode(),
        "dependencies/runtime-manifest.json": (
            json.dumps(runtime_manifest, sort_keys=True, separators=(",", ":"))).encode(),
    }


def create(output: Path, epoch: int, tools_dir: Path,
           runtime_manifest: dict | None = None, *, include_appimage_runtime: bool = True) -> Path:
    """Create a fresh atomic source supplement with exact deployed source coverage.

    Without a bill of materials this remains a runtime-source-only acquisition route.
    Native distributable packages always supply their actual deployed bill of materials.
    Input archives remain byte-exact; only the outer distribution is normalized.
    """
    if type(epoch) is not int or not 0 <= epoch <= 0xffffffff:
        raise ValueError("Source epoch must be an integer in the gzip timestamp range")
    output = Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise FileExistsError(output)
    _regular(README)
    files = []
    metadata = {"README.txt": README.read_bytes()}
    if include_appimage_runtime:
        _regular(LOCK)
        lock_bytes = LOCK.read_bytes()
        specification = json.loads(lock_bytes)
        if specification.get("format") != 1:
            raise ValueError("Unsupported runtime source lock format")
        sources = specification["sources"]
        notices = specification["notices"]
        for records, key in ((sources, "name"), (notices, "path")):
            names = [_filename(record[key]) for record in records]
            if len(names) != len(set(names)):
                raise ValueError("Duplicate runtime source lock filenames")
            for record in records:
                if not re.fullmatch(r"[0-9a-f]{64}", record["sha256"]):
                    raise ValueError("Invalid runtime source SHA-256")
        if any(not source["url"].startswith("https://") for source in sources):
            raise ValueError("Runtime source downloads must use HTTPS")
        cache = Path(tools_dir).resolve() / "runtime-sources"
        if cache.is_symlink():
            raise ValueError("Runtime source cache must not be a symlink")
        cache.mkdir(parents=True, exist_ok=True)
        for source in sources:
            path = _download(cache, source["name"], source["url"], source["sha256"])
            files.append((f"archives/{source['name']}", path, source["sha256"]))
        for notice in notices:
            files.append((f"notices/{notice['path']}", LOCK.parent / notice["path"],
                          notice["sha256"]))
        metadata["runtime-sources.lock.json"] = lock_bytes
    if runtime_manifest is not None:
        dependency_files, dependency_metadata = _dependency_materials(tools_dir, runtime_manifest)
        files.extend(dependency_files)
        metadata.update(dependency_metadata)
        deployed_ffmpeg = [component["provenance"] for component in runtime_manifest["components"]
                           if component["provenance"].get("type") == "upstream-source-build"
                           and component["provenance"].get("package") == "FFmpeg"]
        if deployed_ffmpeg and "C4_FFMPEG_PREFIX" not in os.environ:
            raise ValueError("Deployed FFmpeg requires its authenticated corresponding source")
        deployed_dbus = [component["provenance"] for component in runtime_manifest["components"]
                         if component["provenance"].get("type") == "upstream-source-build"
                         and component["provenance"].get("package") == "D-Bus"]
        if deployed_dbus and "C4_DBUS_PREFIX" not in os.environ:
            raise ValueError("Deployed owned D-Bus requires its authenticated source prefix")
    if "C4_FFMPEG_PREFIX" in os.environ:
        ffmpeg_files, ffmpeg_metadata = _ffmpeg_materials(tools_dir)
        files.extend(ffmpeg_files)
        metadata.update(ffmpeg_metadata)
        if runtime_manifest is not None:
            canonical = json.loads(ffmpeg_metadata["ffmpeg/ffmpeg-source.json"])
            for provenance in deployed_ffmpeg:
                if any(provenance.get(key) != canonical.get(key)
                       for key in ("version", "source_sha256", "lock_sha256", "proof")):
                    raise ValueError("FFmpeg corresponding source differs from deployed provenance")
    if "C4_DBUS_PREFIX" in os.environ:
        dbus_files, dbus_metadata = _dbus_materials(tools_dir)
        files.extend(dbus_files)
        metadata.update(dbus_metadata)
        if runtime_manifest is not None:
            canonical = json.loads(dbus_metadata["dbus/dbus-source.json"])
            for provenance in deployed_dbus:
                if any(provenance.get(key) != canonical.get(key)
                       for key in ("version", "source_sha256", "lock_sha256", "recipe_sha256", "proof")):
                    raise ValueError("Owned D-Bus source differs from deployed provenance")
                if (provenance.get("built_libraries") != canonical.get("built_libraries")
                        or provenance.get("original_sha256") not in {
                            item["sha256"] for item in canonical["built_libraries"]}
                        or [{"path": item["path"], "sha256": item["sha256"]}
                            for item in provenance.get("configuration", [])]
                        != [{"path": item["path"], "sha256": item["sha256"]}
                            for item in canonical["configuration"]]):
                    raise ValueError("Owned D-Bus binary/configuration differs from deployed source proof")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, prefix=".source-bundle-",
                                     delete=False) as stream:
        temporary = Path(stream.name)
        try:
            with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=epoch) as compressed:
                with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.USTAR_FORMAT) as archive:
                    for name, path, digest in sorted(files):
                        _add_file(archive, path, name, epoch, digest)
                    for name, data in sorted(metadata.items()):
                        archive.addfile(_entry(name, len(data), epoch), io.BytesIO(data))
            stream.flush()
            os.fsync(stream.fileno())
            temporary.chmod(0o644)
            os.utime(temporary, (epoch, epoch))
            # Publish atomically without replacing even a concurrently created path.
            os.link(temporary, output)
        finally:
            temporary.unlink(missing_ok=True)
    return output
