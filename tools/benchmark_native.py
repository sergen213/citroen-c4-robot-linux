#!/usr/bin/env python3
"""Benchmark actual native source320 gameplay, not inspection or synthetic scenes.

Default: masks 0..7 x off/standard/high, then untouched Original/Enhanced/Ultra
presets, each with an isolated cold/warm XDG cache pair. Run on a real SDL display.
Outputs must be outside this repository and the read-only original game directory.
No performance thresholds, hidden-state parity, input-to-photon or VRR claims.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import statistics
import subprocess
import time
import unittest


SOURCE_FRAMES = 320
SOURCE_FIELDS = ("source_raw", "source_vector_raw", "source_array_raw", "source_state")
SOURCE_PROFILE = dict.fromkeys(("atq", "mtq", "stq", "cgm", "shq", "ano", "bloom"), 0)
REPO = Path(__file__).resolve().parents[1]
INPUT = REPO / "tests" / "source_backend_compare.input"
LIMITATIONS = [
    "Exact equality compares only existing reported source_raw/source_vector_raw/"
    "source_array_raw/source_state dictionaries, not hidden engine state or pixels.",
    "Source ticks, not render presents, are authoritative; a --frames safety stop "
    "before source320 is incomplete, never a successful shorter benchmark.",
    "Wall time includes process startup, scene preparation and shutdown; it is not "
    "CPU frame time. Runtime GPU/CPU distributions are retained only when measured.",
    "A fresh XDG cache is cold only for these isolated paths; OS/device/compositor "
    "caches and externally configured driver cache paths are not flushed.",
    "Warm means a second actual process uses the same cache path, not a proven "
    "pipeline-cache hit or guaranteed faster rendering.",
    "Scripted accepted-tick input and presentation intervals do not measure "
    "input-to-photon latency, scanout, physical VRR or full original parity.",
    "Capture hashes identify files; different hashes are not image-quality acceptance.",
    "Unsupported features/fallbacks are reported as observed, not counted as successful "
    "hardware effect acceptance. OpenGL GPU/upload/cache timing may be unavailable.",
]


def positive_int(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def positive_seconds(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be positive and finite")
    return number


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, help="Actual executable native CLI")
    parser.add_argument("--game-dir", type=Path, help="Read-only installed original assets")
    parser.add_argument("--out-dir", type=Path, help="New external evidence directory")
    parser.add_argument("--suite", choices=("all", "masks", "presets"), default="all")
    parser.add_argument("--masks", nargs="+", type=int, choices=range(8), default=list(range(8)))
    parser.add_argument("--denoising", nargs="+", choices=("off", "standard", "high"),
                        default=["off", "standard", "high"])
    parser.add_argument("--presets", nargs="+", choices=("original", "enhanced", "ultra"),
                        default=["original", "enhanced", "ultra"])
    parser.add_argument("--repeats", type=positive_int, default=1,
                        help="Independent cold/warm pairs per configuration (default 1)")
    parser.add_argument("--width", type=positive_int, default=640)
    parser.add_argument("--height", type=positive_int, default=480)
    parser.add_argument("--timeout", type=positive_seconds, default=120,
                        help="Bounded seconds per native process (default 120)")
    parser.add_argument("--frames", type=positive_int,
                        help="Optional positive present-count safety bound, NOT the source target")
    parser.add_argument("--material-override", action="append", default=[], metavar="NAME=M,R[,F]",
                        help="Repeatable exact native CLI material override; applied to every run")
    parser.add_argument("--self-check", action="store_true",
                        help="Run one stdlib parsing/source-comparison test without a runtime")
    args = parser.parse_args(argv)
    if not args.self_check:
        for name in ("runtime", "game_dir", "out_dir"):
            if getattr(args, name) is None:
                parser.error(f"--{name.replace('_', '-')} is required")
    return args


def source_tuple(data):
    game = data.get("game")
    if not isinstance(game, dict):
        return None
    values = [game.get(key) for key in SOURCE_FIELDS]
    return dict(zip(SOURCE_FIELDS, values)) if all(isinstance(v, dict) and v for v in values) else None


def source_complete(data):
    game = data.get("game") or {}
    state = game.get("source_state") or {}
    return (data.get("runtime_kind") == "native-game"
            and all(data.get(key) == SOURCE_FRAMES for key in
                    ("source_frames", "requested_source_frames", "completed_source_frames"))
            and data.get("last_source_frame_index") == SOURCE_FRAMES - 1
            and data.get("target_reached") is True
            and data.get("finite_stop_failure_reason") is None
            and data.get("input_scenario_finished") is True
            and game.get("source_callback_frames") == SOURCE_FRAMES
            and state.get("completed_source_frames") == SOURCE_FRAMES
            and state.get("last_source_frame_index") == SOURCE_FRAMES - 1
            and game.get("source_profile") == SOURCE_PROFILE
            and source_tuple(data) is not None)


def compare_source(reference, current):
    if reference is None or current is None:
        return {"status": "unavailable", "different_fields": []}
    different = [key for key in SOURCE_FIELDS if reference[key] != current[key]]
    return {"status": "different" if different else "equal", "different_fields": different}


def distribution(values):
    """Nearest-rank p95; never manufacture samples from a reported average."""
    if not values:
        return None
    ordered = sorted(values)
    return {"samples": len(ordered), "median": statistics.median(ordered),
            "p95": ordered[math.ceil(len(ordered) * .95) - 1],
            "p95_method": "nearest_rank"}


def cache_snapshot(path):
    files = []
    for item in sorted(path.rglob("*")):
        if item.is_file() and not item.is_symlink():
            files.append({"path": str(item.relative_to(path)), "bytes": item.stat().st_size})
    return {"files": files, "total_bytes": sum(item["bytes"] for item in files)}


def reported_distribution(data, count_key, median_key, p95_key):
    if not data.get(count_key, 0) or median_key not in data or p95_key not in data:
        return None
    return {key: data[key] for key in (count_key, median_key, p95_key)}


def measured_evidence(data):
    gpu = data.get("vulkan") or {}
    return {
        "timings": data.get("timings"),
        "history_epoch": data.get("history_epoch"),
        "gpu": {key: value for key, value in gpu.items()
                if key.startswith("gpu_") and key not in ("gpu_median_ns", "gpu_p95_ns")},
        "gpu_distribution_ns": reported_distribution(gpu, "gpu_distribution_samples", "gpu_median_ns", "gpu_p95_ns"),
        "uploads": {key: value for key, value in gpu.items()
                    if key.startswith(("upload_", "frame_upload_"))
                    and key not in ("upload_gpu_median_ns", "upload_gpu_p95_ns")},
        "upload_gpu_distribution_ns": reported_distribution(gpu, "upload_gpu_samples",
                                                           "upload_gpu_median_ns", "upload_gpu_p95_ns"),
        "pipeline_cache": {key: value for key, value in gpu.items() if key.startswith("pipeline_cache_")},
        "features": {key: value for key, value in gpu.items()
                     if key.startswith("rt_") or key in ("raytracing", "resolved_graphics", "pbr_draw_calls",
                         "quality_fallback_reason", "frames", "world_draw_calls", "validation_enabled", "validation_errors")},
        "graphics": data.get("graphics"), "window": data.get("window"),
        "display": data.get("display"), "backend": data.get("backend"),
        "device": {key: gpu[key] for key in ("device_name", "api_version", "driver_version", "vendor_id",
                                            "device_id", "present_mode") if key in gpu},
        "opengl": data.get("opengl"),
    }


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def configurations(args):
    cases = []
    if args.suite in ("all", "masks"):
        cases.extend((f"mask{mask}-{mode}", ["--quality", "custom", "--renderer", "vulkan",
                      "--denoising", mode] +
                      [value for flag, bit in (("--rt-reflections", 1), ("--rt-shadows", 2), ("--rt-gi", 4))
                       for value in (flag, "on" if mask & bit else "off")])
                     for mask in dict.fromkeys(args.masks) for mode in dict.fromkeys(args.denoising))
    if args.suite in ("all", "presets"):
        # No backend, RT or denoising flags: these must exercise the actual preset.
        cases.extend((f"preset-{preset}", ["--quality", preset]) for preset in dict.fromkeys(args.presets))
    return cases


def run_native(command, directory, environment, timeout):
    record = {"command": command, "cwd": str(directory), "exit_code": None, "timed_out": False,
              "stdout": str(directory / "stdout.txt"), "stderr": str(directory / "stderr.txt")}
    write_json(directory / "command.json", {"command": command, "cwd": str(directory),
               "XDG_CACHE_HOME": environment["XDG_CACHE_HOME"],
               "XDG_CONFIG_HOME": environment["XDG_CONFIG_HOME"], "timeout_seconds": timeout})
    started = time.perf_counter()
    try:
        with (directory / "stdout.txt").open("wb") as stdout, (directory / "stderr.txt").open("wb") as stderr:
            process = subprocess.Popen(command, cwd=directory, env=environment, stdout=stdout,
                                       stderr=stderr, start_new_session=True)
            try:
                record["exit_code"] = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                record["timed_out"] = True
            finally:
                # Own/reap the direct child and kill its group on timeout/cancellation.
                if record["exit_code"] is None:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    record["exit_code"] = process.wait()
    except OSError as error:
        record["launch_error"] = str(error)
    record["wall_seconds"] = time.perf_counter() - started
    return record


def main(argv=None):
    args = parse_args(argv)
    if args.self_check:
        result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(SelfCheck))
        return 0 if result.wasSuccessful() else 1
    runtime = args.runtime.resolve(strict=True)
    game_dir = args.game_dir.resolve(strict=True)
    root = args.out_dir.resolve()
    if not runtime.is_file() or not os.access(runtime, os.X_OK):
        raise ValueError("--runtime must be an executable file")
    if not game_dir.is_dir():
        raise ValueError("--game-dir must be an existing original asset directory")
    if root.is_relative_to(REPO) or root.is_relative_to(game_dir):
        raise ValueError("--out-dir must be outside the repository and game directory")
    if not INPUT.is_file():
        raise ValueError(f"Missing original gameplay input: {INPUT}")
    root.mkdir(parents=True, exist_ok=False)
    summary = {"schema_version": 1, "runtime": str(runtime), "game_dir": str(game_dir),
               "out_dir": str(root), "source_frames_target": SOURCE_FRAMES,
               "input_script": str(INPUT), "input_sha256": hashlib.sha256(INPUT.read_bytes()).hexdigest(),
               "source_profile": SOURCE_PROFILE, "frames_safety_bound": args.frames,
               "material_overrides": args.material_override,
               "source_comparison_fields": SOURCE_FIELDS, "limitations": LIMITATIONS,
               "inherited_cache_environment": {key: value for key, value in os.environ.items()
                    if key.startswith(("MESA_", "__GL_SHADER_DISK_CACHE", "AMD_", "VK_"))},
               "runs": [], "wall_distributions_seconds": {}, "reference_run": None,
               "status": "running"}
    reference = None
    common = ["--game-dir", str(game_dir), "--windowed", "--width", str(args.width),
              "--height", str(args.height), "--source-frames", str(SOURCE_FRAMES),
              "--input-script", str(INPUT), "--vsync", "off", "--frame-limit", "0"]
    common.extend(value for key in SOURCE_PROFILE for value in (f"--source-{key}", "0"))
    common.extend(value for override in args.material_override for value in ("--material-override", override))
    if args.frames is not None:
        common.extend(("--frames", str(args.frames)))
    write_json(root / "summary.json", summary)
    try:
        for name, options in configurations(args):
            for repeat in range(1, args.repeats + 1):
                pair = root / f"{name}-{repeat:03d}"
                cache = pair / "cache"
                cache.mkdir(parents=True)
                for phase in ("cold", "warm"):
                    directory = pair / phase
                    config = directory / "config"
                    config.mkdir(parents=True)
                    environment = os.environ.copy()
                    environment.update(XDG_CACHE_HOME=str(cache), XDG_CONFIG_HOME=str(config))
                    capture, report = directory / "capture.ppm", directory / "diagnostics.json"
                    command = [str(runtime)] + common + options + ["--capture", str(capture),
                                                                   "--diagnostics-json", str(report)]
                    before = cache_snapshot(cache)
                    record = run_native(command, directory, environment, args.timeout)
                    record.update(configuration=name, repeat=repeat, phase=phase,
                                  cache_before=before, cache_after=cache_snapshot(cache),
                                  XDG_CACHE_HOME=str(cache), XDG_CONFIG_HOME=str(config),
                                  diagnostics_file=str(report), capture={"path": str(capture), "exists": capture.is_file()})
                    if capture.is_file():
                        record["capture"].update(bytes=capture.stat().st_size,
                            sha256=hashlib.sha256(capture.read_bytes()).hexdigest())
                    data = None
                    try:
                        data = json.loads(report.read_text(encoding="utf-8"))
                        if not isinstance(data, dict):
                            raise ValueError("diagnostics root is not an object")
                    except (OSError, ValueError) as error:
                        record["diagnostics_error"] = str(error)
                    record["diagnostics"] = data
                    complete = data is not None and source_complete(data)
                    record["source_target_status"] = "complete" if complete else "incomplete"
                    record["source_comparison"] = {"status": "unavailable", "different_fields": []}
                    if data is not None:
                        record["measured"] = measured_evidence(data)
                        sources = data.get("sources") or {}
                        record["input_script_matches"] = sources.get("input_script") == str(INPUT)
                        if complete and record["exit_code"] == 0 and not record["timed_out"]:
                            current = source_tuple(data)
                            if reference is None:
                                reference = current
                                summary["reference_run"] = str(directory)
                            record["source_comparison"] = compare_source(reference, current)
                    record["ok"] = (complete and record["exit_code"] == 0 and not record["timed_out"]
                                    and record["capture"].get("bytes", 0) > 0
                                    and record.get("input_script_matches") is True
                                    and record["source_comparison"]["status"] == "equal")
                    summary["runs"].append(record)
                    write_json(directory / "result.json", record)
                    write_json(root / "summary.json", summary)
                    print(f"{name} #{repeat} {phase}: exit={record['exit_code']} "
                          f"source={record['source_target_status']} "
                          f"tuple={record['source_comparison']['status']} wall={record['wall_seconds']:.3f}s",
                          flush=True)
        for name, _ in configurations(args):
            summary["wall_distributions_seconds"][name] = {
                phase: distribution([r["wall_seconds"] for r in summary["runs"]
                                     if r["configuration"] == name and r["phase"] == phase and r["ok"]])
                for phase in ("cold", "warm")}
        summary["status"] = "complete" if all(r["ok"] for r in summary["runs"]) else "failed"
    except KeyboardInterrupt:
        summary["status"] = "interrupted"
    finally:
        write_json(root / "summary.json", summary)
    print(f"Evidence: {root / 'summary.json'}")
    return 0 if summary["status"] == "complete" else 1


class SelfCheck(unittest.TestCase):
    def test_parsing_and_observed_source_comparison(self):
        args = parse_args(["--self-check", "--suite", "masks", "--masks", "0", "7",
                           "--denoising", "high", "--frames", "600",
                           "--material-override", "body=0.2,0.8", "--material-override", "glass=0,0.1,0.9"])
        self.assertEqual(args.frames, 600)
        self.assertEqual(args.material_override, ["body=0.2,0.8", "glass=0,0.1,0.9"])
        self.assertEqual(len(configurations(args)), 2)
        with self.assertRaises(SystemExit):
            parse_args(["--self-check", "--frames", "0"])
        data = {"runtime_kind": "native-game", "source_frames": 320, "requested_source_frames": 320,
                "completed_source_frames": 320, "last_source_frame_index": 319,
                "target_reached": True, "finite_stop_failure_reason": None, "input_scenario_finished": True,
                "game": {key: {"observed": 1} for key in SOURCE_FIELDS}}
        data["game"].update(source_callback_frames=320, source_profile=SOURCE_PROFILE.copy())
        data["game"]["source_state"].update(completed_source_frames=320, last_source_frame_index=319)
        self.assertTrue(source_complete(data))
        reference = source_tuple(data)
        self.assertEqual(compare_source(reference, source_tuple(data))["status"], "equal")
        data["game"]["source_raw"] = {"observed": 2}
        self.assertEqual(compare_source(reference, source_tuple(data))["different_fields"], ["source_raw"])
        data["completed_source_frames"] = 319
        self.assertFalse(source_complete(data))
        self.assertEqual(compare_source(None, source_tuple(data))["status"], "unavailable")
        self.assertIsNone(distribution([]))
        self.assertIsNone(reported_distribution({"gpu_distribution_samples": 0}, "gpu_distribution_samples",
                                               "gpu_median_ns", "gpu_p95_ns"))
        self.assertEqual(distribution([3, 1, 2])["median"], 2)
        self.assertEqual(distribution([3, 1, 2])["p95"], 3)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        raise SystemExit(f"benchmark_native: {error}")
