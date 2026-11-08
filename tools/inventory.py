#!/usr/bin/env python3
"""Inventory recovered files using hashes and static native-tool evidence."""

import argparse
from collections import Counter
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile


def run_tool(command: list[str]) -> dict:
    """Capture evidence, failing rather than silently omitting failed analysis."""
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env={**os.environ, "LC_ALL": "C"},
    )
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic output"
        raise ValueError(
            f"{command[0]} failed (exit {result.returncode}) for {command[1:]!r}: {detail}"
        )
    return {
        "command": command,
        "returncode": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
    }


def build_inventory(root: Path) -> dict:
    """Return the complete payload inventory; never execute recovered binaries."""
    if __package__:
        from .import_assets import validate_tree
    else:
        from import_assets import validate_tree

    # Validation also examines metadata, though metadata is not recovered payload.
    records = validate_tree(root)
    root = root.resolve(strict=True)
    tools = {}
    for name in ("file", "objdump"):
        executable = shutil.which(name)
        if executable is None:
            package = "file" if name == "file" else "binutils"
            raise ValueError(
                f"Required tool {name!r} not found on PATH; install {package} and retry."
            )
        executable = str(Path(executable).resolve(strict=True))
        if Path(executable).is_relative_to(root):
            raise ValueError(f"Refusing to execute tool from recovered asset root: {executable}")
        tools[name] = {
            "path": executable,
            "version": run_tool([executable, "--version"]),
        }

    files = []
    extensions = Counter()
    extension_bytes = Counter()
    for record in records:
        if record["path"] == "asset-manifest.json":
            continue
        path = root / record["path"]
        if path.is_symlink() or not path.resolve(strict=True).is_relative_to(root):
            raise ValueError(f"File escapes recovered asset root or is a symlink: {path}")
        with path.open("rb") as source:
            magic = source.read(32)
            pe_offset = None
            if magic.startswith(b"MZ"):
                source.seek(0x3C)
                offset_bytes = source.read(4)
                if len(offset_bytes) == 4:
                    offset = struct.unpack("<I", offset_bytes)[0]
                    source.seek(offset)
                    if source.read(4) == b"PE\x00\x00":
                        pe_offset = offset

        extension = path.suffix.lower()
        type_evidence = run_tool([tools["file"]["path"], "--brief", "--", str(path)])
        entry = {
            **record,
            "extension": extension,
            "magic_hex": magic.hex(),
            "type": type_evidence["stdout"].rstrip("\n"),
            "file_evidence": type_evidence,
        }
        if pe_offset is not None:
            headers = run_tool([tools["objdump"]["path"], "-p", "--", str(path)])
            sections = run_tool([tools["objdump"]["path"], "-h", "--", str(path)])
            entry["pe"] = {
                "signature_offset": pe_offset,
                "headers_and_imports": headers,
                "sections": sections,
                "dll_imports": re.findall(
                    r"^\s*DLL Name:\s*(.+?)\s*$", headers["stdout"], flags=re.MULTILINE
                ),
            }
        files.append(entry)
        extensions[extension] += 1
        extension_bytes[extension] += record["size"]

    return {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "root": str(root),
        "excluded_metadata": ["asset-manifest.json"],
        "provenance": {
            "python": {"executable": sys.executable, "version": sys.version},
            "tools": tools,
            "magic_prefix_bytes": 32,
            "extension_normalization": "lowercase final suffix; empty string for no suffix",
            "analysis": "Static file classification and PE inspection; payloads are never executed.",
        },
        "file_count": len(files),
        "total_size": sum(entry["size"] for entry in files),
        "extension_counts": dict(sorted(extensions.items())),
        "extension_sizes": dict(sorted(extension_bytes.items())),
        "files": files,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="Recovered asset directory")
    parser.add_argument(
        "--output", type=Path, required=True, help="Fresh JSON report path outside asset root"
    )
    args = parser.parse_args()
    temporary = None
    try:
        if os.path.lexists(args.output):
            raise ValueError(f"--output already exists; choose a fresh report path: {args.output}")
        if args.output.resolve().is_relative_to(args.root.resolve(strict=True)):
            raise ValueError("--output must be outside --root to avoid inventorying its own report.")
        report = build_inventory(args.root)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=args.output.parent, prefix=".inventory-", delete=False
        ) as target:
            temporary = Path(target.name)
            json.dump(report, target, indent=2, ensure_ascii=True)
            target.write("\n")
        os.link(temporary, args.output)
        temporary.unlink()
        temporary = None
    except (OSError, ValueError, ImportError) as error:
        print(f"inventory: {error}", file=sys.stderr)
        return 1
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
