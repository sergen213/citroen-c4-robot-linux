#!/usr/bin/env python3
"""Import a local binary once, retain its Ghidra project, and export static evidence.

Usage: python tools/ghidra/analyze.py work/original/app/C4_Robot.exe
       python tools/ghidra/analyze.py work/original/app/C4_Robot.exe --select 00401000
No original binary is executed. Reports/decompilations remain local analysis data.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PATTERNS = {
    "renderer": r"direct3d|d3dx|render|shader|texture|wmb|\.fx\b",
    "resource": r"\.wrs|\.mdl|\.wmb|\.rc4|pak|archive|resource|file_open|file_read",
    "audio": r"directsound|dsound|sound|snd_|audio|ogg|wav|vorbis",
    "input": r"directinput|dinput|key_|mouse|joystick|GetAsyncKeyState",
    "loop": r"PeekMessage|GetMessage|QueryPerformanceCounter|timeGetTime|engine_frame|wait",
    "physics": r"phys|collision|collide|trace|move|newton|ode",
    "animation": r"anim|bone|skin|pose|vertex",
}

def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def rows(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))

def seed_functions(report: Path) -> tuple[list[str], list[dict[str, str]]]:
    """Select observed xref/call sites, not claimed subsystem implementations."""
    strings = {r["address"]: r["value"] for r in rows(report / "strings.tsv")}
    imports = {r["address"]: r["name"] for r in rows(report / "imports.tsv")}
    functions = {r["address"]: r for r in rows(report / "functions.tsv")}
    candidates: list[tuple[str, str, str]] = []
    for r in rows(report / "string_xrefs.tsv"):
        if r["function_address"]:
            candidates.append((r["function_address"], "string:" + r["string_address"], strings[r["string_address"]]))
    for r in rows(report / "calls.tsv"):
        candidates.append((r["caller_address"], "callee:" + r["callee_address"], imports.get(r["callee_address"], r["callee_name"])))
    selected, evidence = [], []
    for category, pattern in PATTERNS.items():
        addresses = set()
        for address, source, text in candidates:
            if not re.search(pattern, text, re.IGNORECASE) or address not in functions:
                continue
            if functions[address]["thunk"] == "true":
                continue
            evidence.append({"category": category, "address": address, "source": source, "text": text, "confidence": "HYPOTHESIS", "meaning": "candidate selected by observed name/string; inspect decompilation before attributing behavior"})
            if len(addresses) < 6:
                addresses.add(address)
        selected.extend(sorted(addresses))
    selected.extend(r["address"] for r in rows(report / "exports.tsv") if r["address"] in functions)
    return list(dict.fromkeys(selected))[:70], evidence

def self_check() -> None:
    assert re.search(PATTERNS["resource"], "models/car.mdl", re.I)
    assert re.search(PATTERNS["renderer"], "Direct3DCreate9", re.I)
    assert not re.search(PATTERNS["audio"], "CreateFileA", re.I)
    with tempfile.TemporaryDirectory() as directory:
        report = Path(directory)
        fixtures = {
            "strings.tsv": "address\tvalue\n00403000\tmodels/car.mdl\n",
            "imports.tsv": "address\tname\nEXTERNAL:01\tDirect3DCreate9\n",
            "functions.tsv": "address\tthunk\n00401000\tfalse\n00402000\ttrue\n",
            "string_xrefs.tsv": "string_address\tfunction_address\n00403000\t00401000\n",
            "calls.tsv": "caller_address\tcallee_address\tcallee_name\n00402000\tEXTERNAL:01\tDirect3DCreate9\n",
            "exports.tsv": "address\n00401000\n",
        }
        for name, content in fixtures.items():
            (report / name).write_text(content)
        selected, evidence = seed_functions(report)
        assert selected == ["00401000"]
        assert any(item["category"] == "resource" and item["address"] == "00401000" for item in evidence)

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", nargs="?", type=Path)
    parser.add_argument("--ghidra", type=Path, default=Path(os.environ.get("GHIDRA_HOME", "/opt/ghidra")))
    parser.add_argument("--project-dir", type=Path, default=ROOT / "work/ghidra")
    parser.add_argument("--reports", type=Path, default=ROOT / "work/reports/ghidra")
    parser.add_argument("--timeout", type=int, default=1200, help="Ghidra analysis timeout per binary, seconds")
    parser.add_argument("--select", action="append", default=[], help="Function entry address to decompile; use define:ADDRESS to explicitly define an analyst-confirmed entrypoint with its default Ghidra name; repeatable")
    parser.add_argument("--no-auto-select", action="store_true")
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    if args.self_check:
        self_check()
        return 0
    if args.binary is None:
        parser.error("binary is required")
    binary = args.binary.resolve(strict=True)
    sha = digest(binary)
    key = re.sub(r"[^A-Za-z0-9_.-]", "_", binary.name) + "_" + sha[:12]
    report = args.reports.resolve() / key
    report.mkdir(parents=True, exist_ok=True)
    project = args.project_dir.resolve()
    project.mkdir(parents=True, exist_ok=True)
    headless = args.ghidra.resolve() / "support/analyzeHeadless"
    if not headless.is_file():
        raise FileNotFoundError(headless)
    metadata_path = report / "invocations.json"
    metadata = json.loads(metadata_path.read_text()) if metadata_path.exists() else {
        "binary": str(binary), "sha256": sha, "size": binary.stat().st_size,
        "project": str(project / "CitroenC4Robot.gpr"), "folder": key,
        "ghidra_properties": (args.ghidra / "Ghidra/application.properties").read_text(),
        "invocations": [], "original_executed": False,
    }
    summary = report / "summary.tsv"
    reused = summary.exists() and (project / "CitroenC4Robot.gpr").exists()
    if reused and dict((r["key"], r["value"]) for r in rows(summary)).get("sha256", "").lower() != sha:
        raise RuntimeError("Saved project evidence hash does not match binary")

    def run(process: bool, selections: list[str]) -> None:
        command = [str(headless), str(project), "CitroenC4Robot/" + key]
        command += ["-process", binary.name, "-noanalysis"] if process else ["-import", str(binary), "-overwrite", "-analysisTimeoutPerFile", str(args.timeout)]
        command += ["-max-cpu", "2", "-scriptPath", str(Path(__file__).parent), "-postScript", "ExportEvidence.java", str(report), *selections,
                    "-log", str(report / "ghidra.log"), "-scriptlog", str(report / "script.log")]
        metadata["invocations"].append({"command": command, "mode": "reuse" if process else "import"})
        metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
        complete = report / "export-complete.txt"
        complete.unlink(missing_ok=True)
        result = subprocess.run(command, cwd=ROOT, check=False)
        metadata["invocations"][-1]["exit_code"] = result.returncode
        metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
        if result.returncode or not summary.exists() or not complete.exists() or complete.read_text().strip().lower() != sha:
            raise RuntimeError(f"Ghidra export failed; inspect {report / 'ghidra.log'} and {report / 'script.log'}")

    run(reused, args.select)
    if not args.no_auto_select and not args.select:
        selections, evidence = seed_functions(report)
        (report / "subsystem_candidates.json").write_text(json.dumps(evidence, indent=2) + "\n")
        if selections:
            run(True, selections)
    metadata["counts"] = dict((r["key"], r["value"]) for r in rows(summary))
    metadata["source_hash_after"] = digest(binary)
    if metadata["source_hash_after"] != sha:
        raise RuntimeError("Original source hash changed during static analysis")
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps({"report": str(report), "project": metadata["project"], "sha256": sha, "counts": metadata["counts"]}, indent=2))
    return 0

if __name__ == "__main__":
    sys.exit(main())
