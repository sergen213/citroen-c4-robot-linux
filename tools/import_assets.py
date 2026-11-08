#!/usr/bin/env python3
"""Import a locally owned Inno installer or original installation directory.

Installer extraction uses a trusted native innoextract (or the one on PATH).
Existing output directories are never replaced: choose a fresh versioned path.
A failed or cancelled attempt stops the extractor process group, reaps its direct
child, and removes only its private temporary staging directory. Retry uses a
new staging directory. SIGKILL may leave an uncommitted .import-* sibling.
"""
import argparse
import ctypes
import errno
import hashlib
import json
import mmap
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import zlib

MANIFEST_NAME = "asset-manifest.json"
CATALOG_NAME = "original-assets.json"
SOURCE_ENTRY_LIMIT = 100000
SOURCE_DEPTH_LIMIT = 64


def original_asset_records() -> list[dict]:
    """The inspected original installer file set, never source-supplied metadata."""
    with Path(__file__).with_name(CATALOG_NAME).open(encoding="utf-8") as stream:
        return json.load(stream)["files"]


def open_source_directory(path: Path, *, create: bool = False, forbidden: tuple | None = None) -> int:
    """Resolve/create components with nofollow, never entering the source to write."""
    descriptor = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for part in path.parts[1:]:
            if part in (".", "..") or any(ord(char) < 32 or 127 <= ord(char) <= 159 for char in part):
                raise ValueError(f"Unsafe source directory component: {part!r}")
            try:
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                                dir_fd=descriptor)
            except FileNotFoundError:
                if not create:
                    raise
                try:
                    os.mkdir(part, dir_fd=descriptor)
                except FileExistsError:
                    pass
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                                dir_fd=descriptor)
            os.close(descriptor)
            descriptor = child
            if forbidden is not None:
                info = os.fstat(descriptor)
                if (info.st_dev, info.st_ino) == forbidden:
                    raise ValueError("Destination/report parent enters the source directory")
        return descriptor
    except BaseException:
        os.close(descriptor)
        raise


def source_identity(info: os.stat_result) -> tuple:
    return (info.st_dev, info.st_ino, info.st_mode, info.st_nlink, info.st_size,
            info.st_mtime_ns, info.st_ctime_ns)


def source_inventory(root: int) -> dict:
    """Bounded nofollow walk; even excluded user files cannot hide unsafe objects."""
    inventory = {"": source_identity(os.fstat(root))}

    def walk(directory: int, prefix: str, depth: int) -> None:
        if depth > SOURCE_DEPTH_LIMIT:
            raise ValueError("Source directory nesting exceeds safety limit")
        with os.scandir(directory) as entries:
            for entry in entries:
                name = entry.name
                relative = prefix + name
                if relative != MANIFEST_NAME:
                    safe_relative_path(relative)
                if len(inventory) >= SOURCE_ENTRY_LIMIT:
                    raise ValueError("Source directory entry count exceeds safety limit")
                info = os.stat(name, dir_fd=directory, follow_symlinks=False)
                if not stat.S_ISDIR(info.st_mode) and (not stat.S_ISREG(info.st_mode) or info.st_nlink != 1):
                    raise ValueError(f"Source contains link, hardlink or special file: {relative}")
                inventory[relative] = source_identity(info)
                if stat.S_ISDIR(info.st_mode):
                    child = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                                    dir_fd=directory)
                    try:
                        if source_identity(os.fstat(child)) != inventory[relative]:
                            raise ValueError(f"Source directory changed: {relative}")
                        walk(child, relative + "/", depth + 1)
                    finally:
                        os.close(child)

    walk(root, "", 0)
    return inventory


def copy_original_file(root: int, relative: str, target: Path, expected: dict,
                       inventory: dict) -> None:
    directory = os.dup(root)
    descriptor = None
    try:
        parts = relative.split("/")
        prefix = ""
        for part in parts[:-1]:
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                            dir_fd=directory)
            os.close(directory)
            directory = child
            prefix += part
            if source_identity(os.fstat(directory)) != inventory[prefix]:
                raise ValueError(f"Source directory changed: {prefix}")
            prefix += "/"
        descriptor = os.open(parts[-1], os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC,
                             dir_fd=directory)
        before = os.fstat(descriptor)
        if (not stat.S_ISREG(before.st_mode) or before.st_nlink != 1
                or source_identity(before) != inventory[relative] or before.st_size != expected["size"]):
            raise ValueError(f"Source file changed or has unexpected size: {relative}")
        target.parent.mkdir(parents=True, exist_ok=True)
        digest = hashlib.sha256()
        copied = 0
        with target.open("xb") as stream:
            while chunk := os.read(descriptor, 1024 * 1024):
                copied += len(chunk)
                if copied > expected["size"]:
                    raise ValueError(f"Source file grew during copy: {relative}")
                digest.update(chunk)
                stream.write(chunk)
            stream.flush()
            os.fsync(stream.fileno())
        if (source_identity(os.fstat(descriptor)) != source_identity(before)
                or copied != expected["size"] or digest.hexdigest() != expected["sha256"]):
            raise ValueError(f"Source payload checksum mismatch or changed: {relative}")
    finally:
        if descriptor is not None:
            os.close(descriptor)
        os.close(directory)


def import_directory(source: Path, output: Path, report: Path | None = None) -> dict:
    source = Path(source).absolute()
    output = Path(output).absolute()
    resolved_source, resolved_output = source.resolve(), output.resolve()
    if (resolved_source.is_relative_to(resolved_output)
            or resolved_output.is_relative_to(resolved_source)):
        raise ValueError("Source and destination must not overlap")
    report_stream = None
    if report is not None:
        report = Path(report).absolute()
        resolved_report = report.resolve()
        if (resolved_report.is_relative_to(resolved_source)
                or resolved_source.is_relative_to(resolved_report)
                or resolved_report.is_relative_to(resolved_output)
                or resolved_output.is_relative_to(resolved_report)):
            raise ValueError("Report must be outside the source and destination")
    evidence = {"schema_version": 2, "source_kind": "directory",
                "source_directory": str(source), "output": str(output)}
    stage = None
    root = None
    output_parent = None
    try:
        if os.path.lexists(output):
            raise FileExistsError(f"Existing output preserved: {output}; choose a fresh versioned path")
        try:
            root = open_source_directory(source)
            inventory = source_inventory(root)
        except OSError as error:
            raise ValueError(f"Source must be a readable directory without symlink parents: {source}") from error
        if report is not None:
            report_parent = open_source_directory(report.parent, create=True, forbidden=inventory[""][:2])
            try:
                report_descriptor = os.open(report.name, os.O_WRONLY | os.O_CREAT | os.O_EXCL
                                            | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600, dir_fd=report_parent)
                report_stream = os.fdopen(report_descriptor, "w", encoding="utf-8")
            finally:
                os.close(report_parent)
        records = original_asset_records()
        wrapped = "app/C4_Robot.exe" in inventory
        if wrapped == ("C4_Robot.exe" in inventory):
            raise ValueError("Select the original application directory or its unambiguous app parent")
        selected = {record["path"]: record["path"] if wrapped else record["path"].removeprefix("app/")
                    for record in records}
        for record in records:
            relative = selected[record["path"]]
            if relative not in inventory or not stat.S_ISREG(inventory[relative][2]):
                raise ValueError(f"Missing required original asset: {relative}")
        output_parent = open_source_directory(output.parent, create=True, forbidden=inventory[""][:2])
        anchored_parent = Path(f"/proc/self/fd/{output_parent}")
        stage = Path(tempfile.mkdtemp(prefix=f".import-{output.name}-", dir=anchored_parent))
        for record in records:
            print(f"Copying original asset: {selected[record['path']]}", file=sys.stderr, flush=True)
            copy_original_file(root, selected[record["path"]], stage / record["path"], record, inventory)
        print("Checking complete original payload and SHA256 manifest…", file=sys.stderr, flush=True)
        copied_records = validate_tree(stage)
        if copied_records != records:
            raise ValueError("Copied payload does not match the inspected original file catalog")
        if source_inventory(root) != inventory:
            raise ValueError("Source directory changed during import; nothing committed")
        reopened = open_source_directory(source)
        try:
            if source_identity(os.fstat(reopened)) != inventory[""]:
                raise ValueError("Source directory path changed during import; nothing committed")
        finally:
            os.close(reopened)
        manifest = {"schema_version": 2, "source": {"kind": "directory", "path": str(source)},
                    "files": copied_records}
        with (stage / MANIFEST_NAME).open("x", encoding="utf-8") as stream:
            json.dump(manifest, stream, indent=2, ensure_ascii=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        current_parent = open_source_directory(output.parent)
        try:
            parent_info, current_info = os.fstat(output_parent), os.fstat(current_parent)
            if (parent_info.st_dev, parent_info.st_ino) != (current_info.st_dev, current_info.st_ino):
                raise ValueError("Destination parent changed during import; nothing committed")
        finally:
            os.close(current_parent)
        commit_new_directory(stage, anchored_parent / output.name)
        stage = None
        evidence["status"] = "success"
        evidence["file_count"] = len(copied_records)
        print(f"Imported {len(copied_records)} original directory files to {output}", file=sys.stderr, flush=True)
        return manifest
    except BaseException as error:
        evidence["status"] = "failed"
        evidence["error"] = str(error)
        raise
    finally:
        if root is not None:
            os.close(root)
        if stage is not None:
            shutil.rmtree(stage)
        if output_parent is not None:
            os.close(output_parent)
        if report_stream is not None:
            with report_stream:
                json.dump(evidence, report_stream, indent=2, ensure_ascii=False)
                report_stream.write("\n")


def sha256_file(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def safe_relative_path(value: str) -> str:
    parts = value.split("/")
    if (not value or any(part in ("", ".", "..") for part in parts)
            or PurePosixPath(value).is_absolute() or "\\" in value or ":" in value
            or any(ord(char) < 32 or ord(char) == 127 for char in value)):
        raise ValueError(f"Unsafe payload path: {value!r}")
    if value == MANIFEST_NAME:
        raise ValueError(f"Payload uses reserved metadata path: {value}")
    return value


def parse_listing(text: str) -> dict:
    """Parse documented innoextract --silent --list-sizes --list-checksums."""
    files = {}
    for line in text.splitlines():
        if not line:
            continue
        match = re.fullmatch(r"(\d+) (MD5|SHA-1|CRC32|Adler32) (?:0x)?([0-9a-fA-F]+) (.+)", line)
        if not match:
            raise ValueError(f"Unsupported extractor listing row: {line!r}")
        size, algorithm, checksum, name = match.groups()
        name = safe_relative_path(name)
        if name in files:
            raise ValueError(f"Duplicate payload path: {name}")
        width = {"MD5": 32, "SHA-1": 40, "CRC32": 8, "Adler32": 8}[algorithm]
        if len(checksum) > width:
            raise ValueError(f"Invalid {algorithm} checksum for {name}")
        # innoextract 1.9 omits leading hexadecimal zeroes in some MD5s.
        files[name] = {"size": int(size), "algorithm": algorithm,
                       "checksum": checksum.lower().zfill(width)}
    if not files:
        raise ValueError("Installer contains no listed payload files")
    return files


def validate_tree(root: Path) -> list[dict]:
    """Hash a private tree; reject links, special files and unsafe relative paths."""
    root = Path(root)
    if not stat.S_ISDIR(root.lstat().st_mode):
        raise ValueError(f"Payload root is not a real directory: {root}")
    resolved = root.resolve(strict=True)
    files = []
    for directory, dirs, names in os.walk(root, followlinks=False):
        for name in dirs + names:
            path = Path(directory) / name
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode) or not path.resolve(strict=True).is_relative_to(resolved):
                raise ValueError(f"Payload link/path escape: {path}")
            if stat.S_ISDIR(info.st_mode):
                continue
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
                raise ValueError(f"Payload is not an unlinked regular file: {path}")
            relative = path.relative_to(root).as_posix()
            if relative == MANIFEST_NAME:
                continue
            safe_relative_path(relative)
            files.append({"path": relative, "size": info.st_size, "sha256": sha256_file(path)})
    return sorted(files, key=lambda row: row["path"])


def payload_checksum(path: Path, algorithm: str) -> str:
    if algorithm in ("MD5", "SHA-1"):
        digest = hashlib.new("md5" if algorithm == "MD5" else "sha1", usedforsecurity=False)
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()
    value = 1 if algorithm == "Adler32" else 0
    update = zlib.adler32 if algorithm == "Adler32" else zlib.crc32
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value = update(chunk, value)
    return f"{value & 0xffffffff:08x}"


def verify_payload(root: Path, expected: dict) -> list[dict]:
    records = validate_tree(root)
    actual = {row["path"]: row for row in records}
    if set(actual) != set(expected):
        raise ValueError(f"Incomplete payload: missing={sorted(set(expected) - set(actual))}; "
                         f"extra={sorted(set(actual) - set(expected))}")
    for name, info in expected.items():
        if actual[name]["size"] != info["size"]:
            raise ValueError(f"Payload size mismatch: {name}")
        if payload_checksum(root / name, info["algorithm"]) != info["checksum"]:
            raise ValueError(f"Payload checksum mismatch: {name}")
    return records


def commit_new_directory(stage: Path, destination: Path) -> None:
    """Atomically publish without clobbering even an empty concurrent destination."""
    if not sys.platform.startswith("linux"):
        raise OSError("Safe directory publication requires Linux renameat2(RENAME_NOREPLACE)")
    libc = ctypes.CDLL(None, use_errno=True)
    try:
        rename = libc.renameat2
    except AttributeError as error:
        raise OSError("libc lacks renameat2; refusing unsafe replacement fallback") from error
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(stage), -100, os.fsencode(destination), 1) != 0:
        code = ctypes.get_errno()
        if code == errno.EEXIST:
            raise FileExistsError(code, "Existing output preserved; choose a fresh versioned path", str(destination))
        raise OSError(code, os.strerror(code), str(destination))


def run_extractor(tool: Path, installer: Path, options: list[str], log: list[dict]) -> str:
    command = [str(tool), "--color=0", "--progress=0", "--collisions", "error",
               "--no-extract-unknown", *options, "--", str(installer)]
    print(f"innoextract: {' '.join(options)}", file=sys.stderr, flush=True)
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, encoding="utf-8", errors="strict",
                          start_new_session=True) as process:
        try:
            stdout, stderr = process.communicate()
        except BaseException:
            # communicate()/__exit__ deliberately shorten KeyboardInterrupt
            # waits, so subprocess.run can kill without reaping. Own cleanup:
            # TERM the private group, then KILL any non-cooperating descendants.
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                pass
            finally:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
            raise
    log.append({"command": command, "returncode": process.returncode,
                "stdout": stdout, "stderr": stderr})
    if stderr:
        print(stderr, file=sys.stderr, end="" if stderr.endswith("\n") else "\n")
    if process.returncode != 0:
        raise ValueError(f"innoextract exited {process.returncode}: {stdout.strip()}")
    return stdout


def import_installer(installer: Path, output: Path, innoextract: str | None = None,
                     report: Path | None = None) -> dict:
    installer = Path(installer).absolute()
    output = Path(output).absolute()
    log = []
    evidence = {"schema_version": 1, "installer": str(installer), "output": str(output), "commands": log}
    before = None
    stage = None
    report_stream = None
    if report is not None:
        report = Path(report).absolute()
        resolved_report = report.resolve()
        if resolved_report == installer.resolve() or resolved_report.is_relative_to(output.resolve()):
            raise ValueError("Report must be outside the installer and destination")
        report.parent.mkdir(parents=True, exist_ok=True)
        report_stream = report.open("x", encoding="utf-8")
    try:
        if not stat.S_ISREG(installer.lstat().st_mode):
            raise ValueError("Installer must be a regular file, not a symlink")
        if os.path.lexists(output):
            raise FileExistsError(f"Existing output preserved: {output}; choose a fresh versioned path")
        tool_name = innoextract or shutil.which("innoextract")
        if not tool_name:
            raise ValueError("Native innoextract missing; supply --innoextract or install it on PATH")
        tool = Path(tool_name).resolve(strict=True)
        with tool.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                raise ValueError("innoextract must be a native ELF executable, not a Windows executable/wrapper")
        before = sha256_file(installer)
        evidence["installer_sha256_before"] = before
        evidence["extractor_sha256"] = sha256_file(tool)
        with installer.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
            if data[:2] != b"MZ" or not re.search(rb"Inno Setup Setup Data \([0-9.]+(?:[^\x00\r\n)]*)\)", data):
                raise ValueError("Not a PE installer with an Inno Setup data marker")
        version = run_extractor(tool, installer, ["--data-version"], log).strip()
        expected = parse_listing(run_extractor(tool, installer,
            ["--silent", "--list", "--list-sizes", "--list-checksums"], log))
        run_extractor(tool, installer, ["--test"], log)
        output.parent.mkdir(parents=True, exist_ok=True)
        # tempfile creates the only directory this importer is permitted to clean.
        stage = Path(tempfile.mkdtemp(prefix=f".import-{output.name}-", dir=output.parent))
        run_extractor(tool, installer,
            ["--extract", "--test", "--output-dir", str(stage)], log)
        if os.path.lexists(stage / MANIFEST_NAME):
            raise ValueError("Extractor created reserved manifest metadata")
        print("Checking complete payload and SHA256 manifest…", file=sys.stderr, flush=True)
        records = verify_payload(stage, expected)
        after = sha256_file(installer)
        evidence["installer_sha256_after"] = after
        if after != before:
            raise ValueError("Installer changed during import; nothing committed")
        manifest = {"schema_version": 1, "installer_sha256": before,
                    "installer_size": installer.stat().st_size, "inno_data_version": version,
                    "extractor_sha256": evidence["extractor_sha256"], "files": records}
        with (stage / MANIFEST_NAME).open("x", encoding="utf-8") as stream:
            json.dump(manifest, stream, indent=2, ensure_ascii=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        commit_new_directory(stage, output)
        stage = None
        evidence["status"] = "success"
        evidence["file_count"] = len(records)
        print(f"Imported {len(records)} files to {output}", file=sys.stderr)
        return manifest
    except BaseException as error:
        evidence["status"] = "failed"
        evidence["error"] = str(error)
        raise
    finally:
        if stage is not None:
            shutil.rmtree(stage)
        if before is not None and "installer_sha256_after" not in evidence:
            evidence["installer_sha256_after"] = sha256_file(installer)
        if report_stream is not None:
            with report_stream:
                json.dump(evidence, report_stream, indent=2, ensure_ascii=False)
                report_stream.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--installer", type=Path)
    source.add_argument("--directory", type=Path, help="Original game application directory or its app parent")
    parser.add_argument("--output", type=Path, required=True, help="New managed asset directory (never replaced)")
    parser.add_argument("--innoextract", help="Trusted native innoextract executable; defaults to PATH")
    parser.add_argument("--report", type=Path, help="New JSON evidence file outside asset output")
    args = parser.parse_args()
    if args.directory is not None and args.innoextract is not None:
        parser.error("--innoextract applies only to --installer")
    def cancel_import(signum, frame):
        # Repeated cancellation must not interrupt child reaping or stage cleanup.
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        raise KeyboardInterrupt(signal.Signals(signum).name)

    previous_sigterm = signal.signal(signal.SIGTERM, cancel_import)
    previous_sigint = signal.signal(signal.SIGINT, cancel_import)
    try:
        if args.directory is not None:
            import_directory(args.directory, args.output, args.report)
        else:
            import_installer(args.installer, args.output, args.innoextract, args.report)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Import failed: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Import interrupted; uncommitted staging removed", file=sys.stderr)
        return 130
    finally:
        signal.signal(signal.SIGTERM, previous_sigterm)
        signal.signal(signal.SIGINT, previous_sigint)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
