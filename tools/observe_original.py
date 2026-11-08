#!/usr/bin/env python3
"""Bounded, offline Wine observation of the unchanged original launcher; never an installer.

Example: python3 tools/observe_original.py --output work/reference/c4-run \\
    --entry C4_Robot.exe --launcher-action offline-start --bundled-d3dx \\
    --game-action race --driving-inputs --duration 110
Each output directory must be new. Wine is analysis-only, not a native runtime.
Only C4_Robot.exe is invoked, with no added original-launcher arguments.
Optional --input-plan JSON supplies evidence-labelled finite inputs after race startup;
labels describe requests, not observed in-game success. --capture-interval controls
fresh native X11 screenshots (0.5..5 seconds). Observation is bounded to 300 seconds
normally, or 600 seconds only with the analysis-only private launcher GUI prelude.
The prelude is bounded to 15..300 seconds after opening; reserve the complete
shifted offline/menu input schedule inside --duration (maximum 600 seconds).
With --launcher-gui-seconds, AYARLAR opens at 8s in the private display. Inspect
the fresh output frame, then publish launcher-input-01.json (through -32.json)
in the output directory: {"action":"click","x":0,"y":0,"label":"observed control",
"source_frame":"frame-000.00.png"}. Coordinates must come from the actual frame.
After GUI KAYDET/GERI, publish {"action":"finish","label":"saved and back",
"source_frame":"frame-000.00.png"}; automatic offline/menu inputs shift together.
{"action":"stop"} stops without starting. No request before the deadline means
stop, not automatic fallback. Publish complete files atomically; malformed
requests stop. Screenshots/dispatch are not proof of selections or High rendering:
retain displayed choices AND the launcher's exact natural C4.exe command.
"""
import argparse
import ctypes
import ctypes.util
import errno
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n")


def command(args, log, timeout=10, env=None):
    with open(log, "wb") as stream:
        try:
            return subprocess.run(args, stdout=stream, stderr=subprocess.STDOUT,
                                  timeout=timeout, env=env).returncode
        except subprocess.TimeoutExpired:
            return "timeout"


def capture_frame(name, env):
    return command(["/usr/bin/ffmpeg", "-nostdin", "-loglevel", "error",
                    "-f", "x11grab", "-video_size", "1280x960", "-i", ":91",
                    "-frames:v", "1", "/scratch/" + name],
                   "/scratch/" + name + ".log", 8, env)


def keyboard_repeat_proof(display_name, expected_repeat=0):
    class XKeyboardState(ctypes.Structure):
        _fields_ = [("key_click_percent", ctypes.c_int), ("bell_percent", ctypes.c_int),
                    ("bell_pitch", ctypes.c_uint), ("bell_duration", ctypes.c_uint),
                    ("led_mask", ctypes.c_ulong), ("global_auto_repeat", ctypes.c_int),
                    ("auto_repeats", ctypes.c_char * 32)]

    proof = {"display": display_name, "expected_global_auto_repeat": expected_repeat, "passed": False}
    try:
        library = ctypes.util.find_library("X11")
        if not library:
            raise RuntimeError("Native X11 library was not found")
        proof["library"] = library
        x11 = ctypes.CDLL(library)
        x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        x11.XOpenDisplay.restype = ctypes.c_void_p
        x11.XGetKeyboardControl.argtypes = [ctypes.c_void_p, ctypes.POINTER(XKeyboardState)]
        x11.XGetKeyboardControl.restype = ctypes.c_int
        x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        x11.XCloseDisplay.restype = ctypes.c_int
        display = x11.XOpenDisplay(display_name.encode("utf-8"))
        if not display:
            raise RuntimeError("Private X11 display could not be opened")
        try:
            state = XKeyboardState()
            status = x11.XGetKeyboardControl(display, ctypes.byref(state))
            proof.update(query_exit=status, global_auto_repeat=state.global_auto_repeat,
                         passed=status != 0 and state.global_auto_repeat == expected_repeat)
        finally:
            x11.XCloseDisplay(display)
    except (OSError, AttributeError, ValueError, RuntimeError) as error:
        proof["error"] = str(error)
    return proof


def dispatch_held_mouse(input_command, held_key, plan_number, env, report, started, settle_seconds=0):
    record = {"plan_number": plan_number, "requested_held_key": held_key,
              "scope": "Native key dispatch timing only; engine key state is not proven"}
    record["requested_key_settle_seconds"] = settle_seconds
    report.setdefault("held_key_dispatches", []).append(record)
    down_log, up_log = f"plan-{plan_number:02d}-keydown.log", f"plan-{plan_number:02d}-keyup.log"
    record["keydown_log"] = down_log
    record["keyup_log"] = up_log
    record["keydown_started_seconds"] = round(time.monotonic() - started, 3)
    held_started = time.monotonic()
    try:
        record["keydown_exit"] = command(["/usr/bin/xdotool", "keydown", held_key],
                                        "/scratch/" + down_log, env=env)
        record["keydown_completed_seconds"] = round(time.monotonic() - started, 3)
        if record["keydown_exit"] == 0 and settle_seconds:
            time.sleep(settle_seconds)
        record["mouse_started_seconds"] = round(time.monotonic() - started, 3)
        rc = command(input_command, f"/scratch/plan-{plan_number:02d}-input.log", env=env)
        record["mouse_command_exit"] = rc
        record["mouse_completed_seconds"] = round(time.monotonic() - started, 3)
        elapsed = time.monotonic() - started
        name = f"held-key-plan-{plan_number:02d}.png"
        capture_exit = capture_frame(name, env)
        report["captures"].append({
            "seconds": round(elapsed, 3), "completed_seconds": round(time.monotonic() - started, 3),
            "file": name, "capture_exit": capture_exit,
            "capture_kind": "between-keydown-and-keyup-dispatch", "held_key": held_key})
        return rc
    finally:
        record["keyup_started_seconds"] = round(time.monotonic() - started, 3)
        record["keyup_exit"] = command(["/usr/bin/xdotool", "keyup", held_key],
                                      "/scratch/" + up_log, env=env)
        record["keyup_completed_seconds"] = round(time.monotonic() - started, 3)
        record["actual_held_key_dispatch_seconds"] = round(time.monotonic() - held_started, 3)


def digest(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load_input_plan(path, duration):
    if not path.is_file():
        raise ValueError("Input plan must be a regular JSON file")
    raw = path.read_bytes()
    snapshot = raw.decode("utf-8")
    plan = json.loads(snapshot)
    if not isinstance(plan, dict):
        raise ValueError("Input plan must be a JSON object")
    evidence = plan.get("source_evidence")
    if (not isinstance(evidence, list) or not evidence
            or any(not isinstance(item, str) or not item.strip() for item in evidence)):
        raise ValueError("Input plan source_evidence must contain nonempty strings")
    actions = plan.get("actions")
    if not isinstance(actions, list) or len(actions) > 32:
        raise ValueError("Input plan actions must be a list of at most 32 actions")
    allowed_keys = {"Up", "Down", "Left", "Right", "d", "s", "space", "c", "Escape", "F1"}
    previous_end = 75
    for index, action in enumerate(actions):
        if not isinstance(action, dict):
            raise ValueError(f"Input plan action {index} must be an object")
        at, hold = action.get("at_seconds"), action.get("hold_seconds")
        if any(type(value) not in (int, float)
               or (type(value) is float and not math.isfinite(value)) for value in (at, hold)):
            raise ValueError(f"Input plan action {index} requires finite numeric times")
        if at < 75 or at < previous_end or not 0.05 <= hold <= 3:
            raise ValueError(f"Input plan action {index} must start >=75, not overlap, and hold 0.05..3 seconds")
        if at > duration - 5:
            raise ValueError(f"Input plan action {index} exceeds the bounded observation duration")
        if sum(name in action for name in ("key", "keys", "mouse")) != 1:
            raise ValueError(f"Input plan action {index} requires exactly one of key, keys, or mouse")
        settle = 0
        if "mouse" in action:
            mouse = action["mouse"]
            if (not isinstance(mouse, dict)
                    or type(mouse.get("x")) is not int or not 0 <= mouse["x"] <= 1279
                    or type(mouse.get("y")) is not int or not 0 <= mouse["y"] <= 959
                    or type(mouse.get("button")) is not int or mouse["button"] != 1):
                raise ValueError(f"Input plan action {index} requires mouse x=0..1279, y=0..959, button=1")
            if "hold_key" in mouse and (
                    not isinstance(mouse["hold_key"], str) or mouse["hold_key"] not in {"Escape", "F1"}):
                raise ValueError(f"Input plan action {index} mouse hold_key must be Escape or F1")
            if "key_settle_seconds" in mouse:
                settle = mouse["key_settle_seconds"]
                if ("hold_key" not in mouse or type(settle) not in (int, float)
                        or (type(settle) is float and not math.isfinite(settle))
                        or not 0 <= settle <= 2):
                    raise ValueError(f"Input plan action {index} mouse key_settle_seconds requires hold_key and finite 0..2 seconds")
        else:
            keys = action["keys"] if "keys" in action else [action["key"]]
            if (not isinstance(keys, list) or not 1 <= len(keys) <= 3
                    or any(not isinstance(key, str) or key not in allowed_keys for key in keys)
                    or len(set(keys)) != len(keys)):
                raise ValueError(f"Input plan action {index} requires 1..3 unique allowed keys")
        label = action.get("label")
        if not isinstance(label, str) or not label.strip():
            raise ValueError(f"Input plan action {index} requires a nonempty label")
        previous_end = at + settle + hold
    if actions and previous_end > duration - 5:
        raise ValueError("Last input plan action must end at least 5 seconds before duration")
    return {"input_plan": plan, "input_plan_snapshot": snapshot,
            "input_plan_sha256": hashlib.sha256(raw).hexdigest()}


def load_launcher_request(path, captures, minimum_capture_seconds=0):
    """Only finite private-display clicks; never shell/keys/config/credentials."""
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 16384:
        raise ValueError("Launcher request must be a regular non-symlink file <=16384 bytes")
    raw = path.read_bytes()
    if len(raw) > 16384:
        raise ValueError("Launcher request grew beyond its byte bound")
    request = json.loads(raw)
    if not isinstance(request, dict):
        raise ValueError("Launcher request must be an object")
    action = request.get("action")
    if action == "stop" and set(request) == {"action"}:
        return request, hashlib.sha256(raw).hexdigest()
    if not isinstance(action, str) or action not in {"click", "finish"}:
        raise ValueError("Unsupported launcher action")
    expected = {"action", "label", "source_frame"} | ({"x", "y"} if action == "click" else set())
    if set(request) != expected:
        raise ValueError("Launcher request has unsupported/missing fields")
    if (not isinstance(request["label"], str) or not request["label"].strip()
            or len(request["label"]) > 256):
        raise ValueError("Launcher label must be a nonempty string <=256 characters")
    if (not isinstance(request["source_frame"], str)
            or not any(row["file"] == request["source_frame"] and row["capture_exit"] == 0
                       and row.get("capture_started_seconds", row["seconds"]) >= minimum_capture_seconds
                       for row in captures)):
        raise ValueError("Launcher request must reference a successful fresh capture from this run")
    if action == "click" and (
            type(request["x"]) is not int or not 0 <= request["x"] <= 1279
            or type(request["y"]) is not int or not 0 <= request["y"] <= 959):
        raise ValueError("Launcher click requires x=0..1279 and y=0..959")
    return request, hashlib.sha256(raw).hexdigest()


def validate_passive_plan(path):
    with path.open("rb") as stream:
        raw = stream.read(65537)
    if not raw or len(raw) > 65536:
        raise ValueError("Passive TSV plan must be nonempty and at most 64 KiB")
    snapshot = raw.decode("ascii")
    lines = snapshot.split("\n")
    if lines[-1] == "":
        lines.pop()
    if not 1 <= len(lines) <= 64:
        raise ValueError("Passive TSV plan requires 1..64 queries, without a header")
    modules = {"c4.exe", "acknex.dll", "c4_1.dll", "c4_2.dll", "c4_3.dll", "c4_4.dll", "d3dx9_30.dll"}
    total = 0
    entity_list_queries = 0
    cam_ibre_lookup_queries = 0
    ibre_onscreen_lookup_queries = 0
    labels = set()
    label_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.:-"
    for index, line in enumerate(lines):
        fields = line.removesuffix("\r").split("\t")
        if len(fields) != 5:
            raise ValueError(f"Passive query {index} requires five TSV fields")
        label, module, root, offsets, byte_count = fields
        if (not 1 <= len(label) <= 96 or any(char not in label_chars for char in label)
                or label.casefold() in labels):
            raise ValueError(f"Passive query {index} requires a unique ASCII [A-Za-z0-9_.:-] label of 1..96 characters")
        labels.add(label.casefold())
        if module.casefold() not in modules:
            raise ValueError(f"Passive query {index} has an unsupported module basename")
        entity_list = offsets == "entity-list"
        cam_ibre_lookup = offsets == "cam-ibre-lookup"
        ibre_onscreen_lookup = offsets == "ibre-onscreen-lookup"
        steps = [] if offsets == "-" or entity_list or cam_ibre_lookup or ibre_onscreen_lookup else offsets.split(",")
        if len(steps) > 8:
            raise ValueError(f"Passive query {index} exceeds eight pointer steps")
        for value in [root, *steps]:
            digits = value[2:] if value.lower().startswith("0x") else value
            if not 1 <= len(digits) <= 8 or any(char not in "0123456789abcdefABCDEF" for char in digits):
                raise ValueError(f"Passive query {index} requires unsigned 32-bit hex RVAs/offsets")
            if int(digits, 16) % 4:
                raise ValueError(f"Passive query {index} requires four-byte-aligned root/offset values")
        if (not 1 <= len(byte_count) <= 5 or any(char not in "0123456789" for char in byte_count)
                or not 1 <= int(byte_count) <= 16384):
            raise ValueError(f"Passive query {index} byte count must be decimal 1..16384")
        if module.casefold() == "d3dx9_30.dll" and (
                int(root, 16) != 0x21E064 or offsets != "-" or int(byte_count) != 4):
            raise ValueError(f"Passive query {index} d3dx9_30.dll permits only direct RVA 0x21e064/4 bytes")
        if entity_list:
            if module.casefold() != "acknex.dll" or int(root, 16) != 0x227064 or int(byte_count) != 1412:
                raise ValueError(f"Passive query {index} entity-list requires acknex.dll/root RVA 0x227064/1412 bytes")
            entity_list_queries += 1
            if entity_list_queries > 1:
                raise ValueError("Passive TSV plan permits at most one entity-list query")
        elif cam_ibre_lookup:
            if module.casefold() != "acknex.dll" or int(root, 16) != 0x2246D0 or int(byte_count) != 148:
                raise ValueError(f"Passive query {index} cam-ibre-lookup requires acknex.dll/root RVA 0x2246d0/148 bytes")
            cam_ibre_lookup_queries += 1
            if cam_ibre_lookup_queries > 1:
                raise ValueError("Passive TSV plan permits at most one cam-ibre-lookup query")
            total += 2020
        elif ibre_onscreen_lookup:
            if module.casefold() != "acknex.dll" or int(root, 16) != 0x2246D0 or int(byte_count) != 1412:
                raise ValueError(f"Passive query {index} ibre-onscreen-lookup requires acknex.dll/root RVA 0x2246d0/1412 bytes")
            ibre_onscreen_lookup_queries += 1
            if ibre_onscreen_lookup_queries > 1:
                raise ValueError("Passive TSV plan permits at most one ibre-onscreen-lookup query")
            total += 3820
        else:
            total += int(byte_count) + 4 * len(steps)
    if total > 1048576:
        raise ValueError("Passive query budget exceeds 1 MiB including pointer reads")
    return {"plan_snapshot": snapshot, "plan_sha256": hashlib.sha256(raw).hexdigest(),
            "entity_list_queries": entity_list_queries, "cam_ibre_lookup_queries": cam_ibre_lookup_queries,
            "ibre_onscreen_lookup_queries": ibre_onscreen_lookup_queries}


def process_snapshot():
    # Called only inside the isolated PID namespace, against its native /proc mount.
    pids = sorted((p for p in Path("/proc").iterdir() if p.name.isdecimal()),
                  key=lambda p: int(p.name))
    processes = []
    for path in pids[:256]:
        row = {"pid": int(path.name)}
        try:
            with (path / "comm").open("rb") as stream:
                row["comm"] = stream.read(256).decode("utf-8", errors="replace").rstrip("\n")
            with (path / "cmdline").open("rb") as stream:
                row["cmdline"] = stream.read(4096).decode("utf-8", errors="replace").replace("\0", " ").rstrip()
        except OSError as error:
            row["read_error"] = str(error)
        processes.append(row)
    return {"visible_pid_count": len(pids), "pid_limit": 256,
            "cmdline_bytes_limit": 4096, "processes": processes}


def prove_isolation(host_netns, private_path):
    interfaces = [line.split(":")[0].strip() for line in
                  Path("/proc/net/dev").read_text().splitlines()[2:]]
    netns = os.readlink("/proc/self/ns/net")
    mount = next(line for line in Path("/proc/self/mountinfo").read_text().splitlines()
                 if line.split()[4] == "/original")
    hidden = {p: not Path(p).exists() for p in
              [private_path, "/mnt/2nd4TB", "/root/.ssh", "/home"]}
    # Opening without truncating or writing proves read-only protection harmlessly.
    try:
        fd = os.open("/original/C4.exe", os.O_WRONLY)
    except OSError as error:
        readonly_errno = error.errno
    else:
        os.close(fd)
        readonly_errno = None
    evidence = {"network_namespace": netns, "host_network_namespace": host_netns,
                "interfaces": interfaces, "hidden_host_paths": hidden,
                "original_mount": mount, "original_write_open_errno": readonly_errno}
    native_gate = (netns != host_netns and interfaces == ["lo"]
                   and all(hidden.values()) and readonly_errno == errno.EROFS
                   and "ro" in mount.split()[5].split(","))
    if native_gate:
        sock = socket.socket()
        sock.settimeout(1)
        try:
            sock.connect(("192.0.2.1", 443))
        except OSError as error:
            evidence["external_connect_errno"] = error.errno
        else:
            evidence["external_connect_errno"] = None
        finally:
            sock.close()
    evidence["passed"] = native_gate and evidence.get("external_connect_errno") == errno.ENETUNREACH
    save("/scratch/isolation.json", evidence)
    if not evidence["passed"]:
        raise RuntimeError("Isolation proof failed; no Windows code was executed")


def inside(args):
    os.umask(0o077)
    prove_isolation(args.host_netns, args.private_path)
    shutil.copytree("/original", "/scratch/app")
    os.makedirs("/scratch/home")
    os.makedirs("/tmp/.X11-unix", mode=0o1777)
    env = dict(os.environ, HOME="/scratch/home", WINEPREFIX="/scratch/prefix",
               DISPLAY=":91", WINEDEBUG="+timestamp,+pid,+tid,+loaddll,+seh,+file,+process",
               WINEDLLOVERRIDES="winemenubuilder.exe=d", LIBGL_ALWAYS_SOFTWARE="1")
    if args.trace_d3d:
        env["WINEDEBUG"] += ",+d3d9,+d3d"
    if args.bundled_d3dx:
        assert digest("/original/d3dx9_30.dll") == digest("/scratch/app/d3dx9_30.dll")
        env["WINEDLLOVERRIDES"] += ";d3dx9_30=n"
    payload_hash = digest("/original/" + args.entry)
    assert payload_hash == digest("/scratch/app/" + args.entry)
    report = {"status": "starting", "entry": args.entry, "arguments": [],
              "entry_sha256": payload_hash, "requested_duration_seconds": args.duration,
              "framebuffer": "1280x960x24 private Xvfb; software rendering",
              "inputs_sent": [], "captures": [], "windows": []}
    report["requested_capture_interval_seconds"] = args.capture_interval
    report["disable_key_repeat_requested"] = args.disable_key_repeat
    report["enable_key_repeat_requested"] = args.enable_key_repeat
    report["requested_key_repeat_mode"] = (
        "disabled" if args.disable_key_repeat else "enabled" if args.enable_key_repeat else "default")
    report["input_result_scope"] = "Input command exit is not evidence of in-game action success"
    report["requested_game_input_offset_seconds"] = args.game_input_offset
    report["game_action_dispatch_warning"] = (
        "Game-action requests and input count prove dispatch only, not menu readiness or an actual race")
    report["launcher_gui"] = {
        "requested_seconds": args.launcher_gui_seconds,
        "status": "pending" if args.launcher_gui_seconds else "disabled",
        "request_limit": 32, "requests": [],
        "scope": "Private GUI only; displayed settings and natural child command are separate evidence",
        "automatic_schedule_shift_seconds": 0}
    if args.input_plan:
        report.update(args.input_plan_data)
        Path("/scratch/input-plan.json").write_bytes(args.input_plan_data["input_plan_snapshot"].encode("utf-8"))
    if args.passive_reader:
        report["passive_observer"] = {
            "requested_at_seconds": args.passive_at_seconds,
            "reader_sha256": args.passive_data["reader_sha256"],
            "plan_sha256": args.passive_data["plan_sha256"],
            "plan_snapshot_file": "passive-plan.tsv", "status": "pending",
            "scope": "Separate own PE: read-only CPU snapshot of unchanged naturally spawned C4.exe",
            "retry_policy": "One attempt only; no fallback or retries after refusal or failure"}
        if args.passive_data["entity_list_queries"]:
            report["passive_observer"]["requested_entity_list"] = {
                "module": "acknex.dll", "root_rva": "0x227064", "node_bytes": 1412,
                "max_nodes": 4096, "read_budget_bytes": 8388608,
                "scope": "Explicit read-only engine-list traversal, not arbitrary memory scanning"}
        if args.passive_data["cam_ibre_lookup_queries"]:
            report["passive_observer"]["requested_cam_ibre_lookup"] = {
                "module": "acknex.dll", "root_rva": "0x2246d0", "payload_bytes": 148,
                "symbol": "cam_ibre", "max_nodes": 64, "bucket_offset": "0xcc8",
                "read_budget_bytes": 2020, "shared_generic_budget_bytes": 1048576,
                "scope": "Explicit gated, read-only symbol lookup; no engine calls or arbitrary memory scanning"}
        if args.passive_data["ibre_onscreen_lookup_queries"]:
            report["passive_observer"]["requested_ibre_onscreen_lookup"] = {
                "module": "acknex.dll", "root_rva": "0x2246d0", "payload_bytes": 1412,
                "symbol": "ibre_onscreen", "max_nodes": 64, "bucket_offset": "0x178",
                "read_budget_bytes": 3820, "shared_generic_budget_bytes": 1048576,
                "optional_boxed_alias": {
                    "raw_tag": "0x1e", "box_bytes": 24, "indirect_word_offset": "0x14",
                    "effective_tag_word_offset": "0x04", "raw_payload_word_offset": "0x10",
                    "optional_indirect_payload_bytes": 4, "extra_read_reservation_bytes": 28,
                    "unsupported_effective_tags": ["0x3", "0x9", "0x1e"],
                    "scope": "Only fixed ibre alias; full DWORD controls one optional payload read"},
                "optional_source_image_descriptor": {
                    "image_header_bytes": 100, "extra_read_reservation_bytes": 188,
                    "optional_image_name_bytes": 64,
                    "source_image_offsets": {
                        "flags": "0x20", "requested_mip_levels": "0x58", "selected_format": "0x5c",
                        "source_stride": "0x50", "opaque_gpu_handle": "0x24", "optional_name": "0x08"},
                    "opaque_gpu_handle_policy": "Record opaque value only; no dereference or calls",
                    "nominal_extent_offsets": ["0x44", "0x48"],
                    "nominal_extent_scope": "Before caps; not physical GPU texture extent",
                    "scope": "Named base IMAGE metadata only, not effective LOD draw resource or RAM pixels",
                    "unsupported_metadata_policy": "SKIPPED_UNKNOWN; matched ENTITY remains separate evidence"},
                "scope": "Explicit gated, read-only fixed symbol/alias lookup; ENTITY payload, no engine calls"}
        Path("/scratch/passive-plan.tsv").write_bytes(args.passive_data["plan_snapshot"].encode("utf-8"))
        if os.path.lexists("/scratch/passive-memory.jsonl"):
            report["passive_observer"]["status"] = "existing_output_refused"
            save("/scratch/observation.json", report)
            raise RuntimeError("Passive observer output already exists; no Wine command was executed")
    plan_actions = args.input_plan_data["input_plan"]["actions"] if args.input_plan else []
    next_plan = 0
    next_auto = next_drive = next_gui = 0
    gui_opened = False
    gui_finished = not args.launcher_gui_seconds
    schedule_shift = 0
    automatic = []
    if args.launcher_action:
        automatic.append((8, args.launcher_action,
                          810, 476 if args.launcher_action == "settings" else 400, "input.log"))
    if args.launcher_action == "offline-start":
        automatic += [(14, "blank-offline-confirm", 810, 667, "offline-input.log"),
                      (20, "offline-scores-yes", 605, 534, "offline-yes-input.log")]
    if args.game_action:
        automatic.append((40, "new-game", 640, 462, "new-game-input.log"))
    if args.game_action == "race":
        automatic += [(48, "continue-default-silver", 640, 586, "color-continue-input.log"),
                      (56, "no-ghost", 640, 555, "no-ghost-input.log"),
                      (64, "start-offline-race", 640, 586, "race-start-input.log")]
    report["spawned_child_exit"] = None
    report["spawned_child_exit_status"] = "unknown; spawned Windows children are not individually waited"
    report["process_lifecycle"] = {
        "scope": "Native /proc PIDs visible only inside the sandbox PID namespace",
        "child_exit_codes": "Unknown; process disappearance does not establish a Windows child exit code"}
    report["process_exit_scope"] = "Invoked Wine entrypoint process only, not spawned C4.exe children"
    report["descendants_lifetime"] = {
        "individually_observed": False, "may_outlive_invoked_entrypoint": True,
        "sampling_policy": ("Continue through entrypoint exit until duration limit"
                            if args.launcher_action == "offline-start" else
                            "Stop sampling on invoked entrypoint exit or duration limit"),
        "termination_boundary": "Request wineserver -k at observation stop; remaining processes are bounded by sandbox PID namespace teardown"}
    report["wine_dll_overrides"] = env["WINEDLLOVERRIDES"]
    report["wine_debug"] = env["WINEDEBUG"]
    report["bundled_d3dx_sha256"] = digest("/original/d3dx9_30.dll") if args.bundled_d3dx else None
    save("/scratch/observation.json", report)
    xvfb_log = open("/scratch/xvfb.log", "wb")
    xvfb = subprocess.Popen(["/usr/bin/Xvfb", ":91", "-screen", "0", "1280x960x24",
                             "-nolisten", "tcp", "-noreset",
                             *(["-r"] if args.disable_key_repeat else
                               ["r"] if args.enable_key_repeat else [])], stdout=xvfb_log,
                            stderr=subprocess.STDOUT, env=env)
    try:
        ready = False
        for _ in range(40):
            if xvfb.poll() is not None:
                break
            if command(["/usr/bin/xdpyinfo"], "/scratch/display.txt", env=env) == 0:
                ready = True
                break
            time.sleep(0.1)
        if not ready:
            report["status"] = "xvfb_failed"
            return
        if args.disable_key_repeat or args.enable_key_repeat:
            report["keyboard_repeat_proof"] = keyboard_repeat_proof(
                env["DISPLAY"], expected_repeat=int(args.enable_key_repeat))
            if not report["keyboard_repeat_proof"]["passed"]:
                report["status"] = "key_repeat_proof_failed"
                raise RuntimeError("Native key-repeat proof failed; no Wine command was executed")
        command(["/usr/bin/wine", "--version"], "/scratch/wine-version.txt", env=env)
        boot = command(["/usr/bin/wineboot", "--init"], "/scratch/wineboot.log", 45, env)
        report["wineboot_exit"] = boot
        if boot != 0:
            report["status"] = "wineboot_failed"
            return
        started = time.monotonic()
        game_log = open("/scratch/game.log", "wb")
        game = subprocess.Popen(["/usr/bin/wine", args.entry],
                                cwd="/scratch/app", env=env, stdout=game_log,
                                stderr=subprocess.STDOUT)
        report["invoked_entrypoint_pid"] = game.pid
        report["invoked_entrypoint_exit_observed_seconds"] = None
        try:
            next_capture = started + 1
            deadline = started + args.duration
            while time.monotonic() < deadline:
                elapsed = time.monotonic() - started
                if args.launcher_gui_seconds and not gui_finished:
                    gui = report["launcher_gui"]
                    if elapsed >= 8 + args.launcher_gui_seconds:
                        gui["status"] = report["status"] = "launcher_gui_deadline_stop"
                        break
                    if not gui_opened and elapsed >= 8:
                        rc = command(["/usr/bin/xdotool", "mousemove", "810", "476", "click", "1"],
                                     "/scratch/launcher-settings-input.log", env=env)
                        gui_opened = True
                        gui["last_ui_request_seconds"] = time.monotonic() - started
                        next_capture = time.monotonic()
                        gui["status"] = "awaiting_observed_request"
                        report["inputs_sent"].append({"action": "launcher-gui-settings", "x": 810,
                                                     "y": 476, "seconds": round(elapsed, 3), "exit": rc})
                        if rc != 0:
                            gui["status"] = report["status"] = "launcher_gui_dispatch_failed_stop"
                            break
                    request_path = Path(f"/scratch/launcher-input-{next_gui + 1:02d}.json")
                    if gui_opened and os.path.lexists(request_path):
                        try:
                            request, request_hash = load_launcher_request(
                                request_path, report["captures"], gui["last_ui_request_seconds"])
                        except (OSError, UnicodeError, ValueError) as error:
                            gui["error"] = str(error)
                            gui["status"] = report["status"] = "launcher_gui_invalid_request_stop"
                            break
                        record = {"request_file": request_path.name, "request": request,
                                  "request_sha256": request_hash, "seconds": round(elapsed, 3)}
                        gui["requests"].append(record)
                        next_gui += 1
                        if request["action"] == "stop":
                            gui["status"] = report["status"] = "launcher_gui_requested_stop"
                            break
                        record["source_frame_sha256"] = digest("/scratch/" + request["source_frame"])
                        if request["action"] == "finish":
                            schedule_shift = elapsed - 2  # Start is 6s after this observed GUI handoff.
                            last_input = max((at + (args.game_input_offset if at >= 40 else 0)
                                              for at, *_ in automatic), default=0) + schedule_shift
                            if (last_input > args.duration - 5
                                    or (plan_actions and plan_actions[0]["at_seconds"] < last_input + 1)
                                    or (args.passive_reader and args.passive_at_seconds < last_input + 1)
                                    or (args.driving_inputs and 97 + schedule_shift + args.game_input_offset
                                        > args.duration - 5)):
                                gui["status"] = report["status"] = "launcher_gui_schedule_does_not_fit_stop"
                                break
                            gui_finished = True
                            gui["status"] = "handoff_requested_not_high_proof"
                            gui["automatic_schedule_shift_seconds"] = round(schedule_shift, 3)
                            gui["automatic_schedule"] = [
                                {"action": action, "at_seconds": at + schedule_shift
                                 + (args.game_input_offset if at >= 40 else 0)}
                                for at, action, *_ in automatic]
                        else:
                            rc = command(["/usr/bin/xdotool", "mousemove", str(request["x"]),
                                          str(request["y"]), "click", "1"],
                                         f"/scratch/launcher-gui-{next_gui:02d}-input.log", env=env)
                            gui["last_ui_request_seconds"] = time.monotonic() - started
                            record["exit"] = rc
                            report["inputs_sent"].append({"action": "launcher-gui-click", **record})
                            next_capture = time.monotonic()  # Fresh evidence after every UI request.
                            if rc != 0:
                                gui["status"] = report["status"] = "launcher_gui_dispatch_failed_stop"
                                break
                        if next_gui == 32 and not gui_finished:
                            gui["status"] = report["status"] = "launcher_gui_request_limit_stop"
                            break
                if gui_finished and next_auto < len(automatic):
                    at, action, x, y, input_log = automatic[next_auto]
                    due = at + schedule_shift + (args.game_input_offset if at >= 40 else 0)
                    if time.monotonic() >= started + due:
                        focus = (["search", "--onlyvisible", "--name", "^C4 ROBOT$", "windowfocus", "%@"]
                                 if action == "new-game" else [])
                        mouse = (["mousedown", "1", "sleep", "0.5", "mouseup", "1"]
                                 if at >= 40 else ["click", "1"])
                        rc = command(["/usr/bin/xdotool", *focus, "mousemove", str(x), str(y), *mouse],
                                     "/scratch/" + input_log, env=env)
                        report["inputs_sent"].append({"action": action, "x": x, "y": y,
                                                     "scheduled_seconds": due,
                                                     "seconds": round(time.monotonic() - started, 3), "exit": rc})
                        next_auto += 1
                        if args.launcher_gui_seconds and rc != 0:
                            report["status"] = "launcher_after_gui_dispatch_failed_stop"
                            break
                if args.driving_inputs and gui_finished and next_auto == len(automatic):
                    driving = [(85, "Up", 3), (91, "Left", 0.5), (95, "Down", 2)]
                    if next_drive < len(driving):
                        seconds, key, held = driving[next_drive]
                        if time.monotonic() >= started + seconds + schedule_shift + args.game_input_offset:
                            rc = command(["/usr/bin/xdotool", "keydown", key, "sleep", str(held), "keyup", key],
                                         "/scratch/" + key + "-input.log", env=env)
                            report["inputs_sent"].append({"action": "driving-key", "key": key,
                                                         "held_seconds": held, "exit": rc,
                                                         "completed_seconds": round(time.monotonic() - started, 3)})
                            next_drive += 1
                if gui_finished and next_auto == len(automatic) and next_plan < len(plan_actions):
                    action = plan_actions[next_plan]
                    if time.monotonic() >= started + action["at_seconds"]:
                        input_log = f"plan-{next_plan + 1:02d}-input.log"
                        if "mouse" in action:
                            mouse = action["mouse"]
                            input_command = [
                                "/usr/bin/xdotool", "mousemove", str(mouse["x"]), str(mouse["y"]),
                                "mousedown", "1", "sleep", str(action["hold_seconds"]), "mouseup", "1"]
                        else:
                            keys = action["keys"] if "keys" in action else [action["key"]]
                            input_command = [
                                "/usr/bin/xdotool", "keydown", *keys, "sleep",
                                str(action["hold_seconds"]), "keyup", *reversed(keys)]
                        input_started = time.monotonic() - started
                        held_key = action.get("mouse", {}).get("hold_key")
                        if held_key:
                            rc = dispatch_held_mouse(input_command, held_key, next_plan + 1,
                                                     env, report, started,
                                                     settle_seconds=action["mouse"].get("key_settle_seconds", 0))
                        else:
                            rc = command(input_command, "/scratch/" + input_log, env=env)
                        report["inputs_sent"].append({
                            "action": "planned-mouse" if "mouse" in action else "planned-key",
                            "requested_label": action["label"], "requested_action": action,
                            **{name: action[name] for name in ("key", "keys", "mouse") if name in action},
                            "held_seconds": action["hold_seconds"],
                            "scheduled_seconds": action["at_seconds"],
                            "started_seconds": round(input_started, 3),
                            "completed_seconds": round(time.monotonic() - started, 3),
                            "exit": rc, "input_log": input_log})
                        next_plan += 1
                if (args.passive_reader and report["passive_observer"]["status"] == "pending"
                        and time.monotonic() >= started + args.passive_at_seconds):
                    passive = report["passive_observer"]
                    passive["status"] = "dispatched"
                    passive["started_seconds"] = round(time.monotonic() - started, 3)
                    passive["log"] = "passive-reader.log"
                    passive["output"] = "passive-memory.jsonl"
                    passive["exit"] = command([
                        "/usr/bin/wine", r"Z:\passive-reader.exe", r"Z:\passive-plan.tsv",
                        r"Z:\scratch\passive-memory.jsonl"], "/scratch/passive-reader.log", 20, env)
                    passive["completed_seconds"] = round(time.monotonic() - started, 3)
                    if passive["exit"] != 0:
                        passive["status"] = "timeout" if passive["exit"] == "timeout" else "nonzero_exit"
                        passive["stop_policy"] = "Immediately stop original observation; no retry, fallback, or further captures"
                        passive["failure_detail"] = "Inspect retained log/JSONL for exact reader refusal or bounded failure"
                        report["status"] = (
                            "passive_observer_timeout_stop" if passive["exit"] == "timeout" else
                            "passive_observer_nonzero_stop")
                        break
                    passive["status"] = "command_returned"
                if time.monotonic() >= next_capture:
                    elapsed = time.monotonic() - started
                    name = f"frame-{elapsed:06.2f}.png"
                    rc = capture_frame(name, env)
                    report["captures"].append({"seconds": round(elapsed, 3), "file": name,
                                                "capture_exit": rc, "capture_started_seconds": elapsed})
                    window_log = f"windows-{elapsed:06.2f}.txt"
                    window_rc = command(["/usr/bin/xdotool", "search", "--onlyvisible", "--name", ".", 
                                         "getwindowname", "%@"], "/scratch/" + window_log, env=env)
                    report["windows"].append({"seconds": round(elapsed, 3), "file": window_log,
                                               "query_exit": window_rc})
                    geometry_log = f"geometry-{elapsed:06.2f}.txt"
                    command(["/usr/bin/xdotool", "search", "--onlyvisible", "--name", ".",
                             "getwindowgeometry", "--shell", "%@"], "/scratch/" + geometry_log, env=env)
                    next_capture = time.monotonic() + args.capture_interval
                    if args.launcher_gui_seconds:
                        save("/scratch/observation.json", report)
                # The launcher exits after spawning C4.exe; keep sampling that child.
                entry_exit = game.poll()
                if entry_exit is not None and report["invoked_entrypoint_exit_observed_seconds"] is None:
                    report["invoked_entrypoint_exit_observed_seconds"] = round(time.monotonic() - started, 3)
                if entry_exit is not None and args.launcher_action != "offline-start":
                    report["status"] = "exited"
                    break
                time.sleep(0.1)
            else:
                report["status"] = "duration_limit"
            report["elapsed_seconds"] = round(time.monotonic() - started, 3)
            report["game_exit_before_stop"] = game.poll()
            report["invoked_entrypoint_exit_before_stop"] = report["game_exit_before_stop"]
            report["input_plan_next_index"] = next_plan
            if args.passive_reader and report["passive_observer"]["status"] == "pending":
                report["passive_observer"]["status"] = "not_dispatched_before_observation_end"
        finally:
            report["process_lifecycle"]["pre_stop"] = {
                "seconds": round(time.monotonic() - started, 3), **process_snapshot()}
            report["stop_method"] = "wineserver -k, then bounded wait for invoked entrypoint"
            report["stop_requested_seconds"] = round(time.monotonic() - started, 3)
            report["wineserver_stop_exit"] = command(["/usr/bin/wineserver", "-k"],
                                                      "/scratch/stop.log", 5, env)
            report["process_lifecycle"]["post_wineserver_stop"] = {
                "seconds": round(time.monotonic() - started, 3), **process_snapshot()}
            try:
                game.wait(timeout=5)
            except subprocess.TimeoutExpired:
                game.kill()
                report["stop_method"] += "; invoked entrypoint SIGKILL after 5-second wait"
                game.wait()
            report["game_exit_after_stop"] = game.returncode
            report["invoked_entrypoint_exit_after_stop"] = game.returncode
            game_log.close()
        report["changed_payload_files"] = [str(p.relative_to("/scratch/app")) for p in
            Path("/scratch/app").rglob("*") if p.is_file() and
            (not (Path("/original") / p.relative_to("/scratch/app")).exists() or
             digest(p) != digest(Path("/original") / p.relative_to("/scratch/app")))]
        for name in report["changed_payload_files"]:
            target = Path("/scratch/changed-payload") / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(Path("/scratch/app") / name, target)
    finally:
        save("/scratch/observation.json", report)
        xvfb.terminate()
        try:
            xvfb.wait(timeout=5)
        except subprocess.TimeoutExpired:
            xvfb.kill()
            xvfb.wait()
        xvfb_log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, default=Path("work/original/app"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--entry", choices=["C4_Robot.exe"], default="C4_Robot.exe")
    parser.add_argument("--duration", type=int, default=30, choices=range(1, 601),
                        help="Total observation seconds: 1..300 normally; up to 600 only with launcher GUI")
    parser.add_argument("--capture-interval", type=float, default=5,
                        help="Seconds between fresh native X11 captures (0.5..5)")
    parser.add_argument("--input-plan", type=Path,
                        help="JSON finite, source-evidenced race key/chord/mouse observation plan")
    parser.add_argument("--launcher-action", choices=["settings", "start", "offline-start"])
    parser.add_argument("--launcher-gui-seconds", type=int, default=0,
                        choices=[0, *range(15, 301)],
                        help="Bounded private settings GUI prelude (15..300s after opening); "
                             "fresh-frame-labelled output-directory requests, no guessed High preset")
    parser.add_argument("--bundled-d3dx", action="store_true",
                        help="Analyze using the unchanged original bundled shader compiler")
    parser.add_argument("--trace-d3d", action="store_true",
                        help="Analysis-only: enable installed Wine d3d9/d3d debug traces")
    repeat_group = parser.add_mutually_exclusive_group()
    repeat_group.add_argument("--disable-key-repeat", action="store_true",
                              help="Analysis-only: Xvfb -r; native X11 repeat-off proof required before Wine")
    repeat_group.add_argument("--enable-key-repeat", action="store_true",
                              help="Analysis-only: Xvfb r; native X11 repeat-on proof required before Wine")
    parser.add_argument("--game-action", choices=["new-game", "race"])
    parser.add_argument("--game-input-offset", type=int, choices=range(0, 121), default=0,
                        help="Delay only game-menu/color/ghost/race inputs by 0..120 seconds; plan times remain absolute")
    parser.add_argument("--driving-inputs", action="store_true",
                        help="Exercise documented Up/Left/Down keys only; never transformation")
    parser.add_argument("--passive-reader", type=Path,
                        help="Analysis-only own read-only observer PE; requires --passive-plan")
    parser.add_argument("--passive-plan", type=Path,
                        help="Read-only finite TSV query plan for the own passive observer")
    parser.add_argument("--passive-at-seconds", type=float, default=155,
                        help="Absolute own observer dispatch time; reserve 20 seconds before duration")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--host-netns", default="", help=argparse.SUPPRESS)
    parser.add_argument("--private-path", default="", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.duration > 300 and not args.launcher_gui_seconds:
        parser.error("Durations above 300 seconds require the analysis-only launcher GUI prelude")
    if args.launcher_gui_seconds:
        if args.launcher_action != "offline-start" or not args.bundled_d3dx:
            parser.error("Launcher GUI requires unchanged offline launcher and bundled compiler")
        last_input = 97 if args.driving_inputs else 64 if args.game_action == "race" else 40 if args.game_action else 20
        menu_offset = args.game_input_offset if args.game_action else 0
        reserve = max(20, last_input - 2 + menu_offset + 5)
        if args.duration < 8 + args.launcher_gui_seconds + reserve:
            parser.error("Launcher GUI deadline must reserve the complete shifted input schedule "
                         "and 5 seconds within the total duration")
    if args.game_action and (args.launcher_action != "offline-start" or not args.bundled_d3dx):
        parser.error("Game action requires the observed offline launcher and bundled compiler path")
    if args.driving_inputs and args.game_action != "race":
        parser.error("Driving keys require the observed race startup path")
    if not 0.5 <= args.capture_interval <= 5:
        parser.error("Capture interval must be finite and within 0.5..5 seconds")
    if args.input_plan:
        if args.driving_inputs:
            parser.error("Input plan and --driving-inputs cannot be combined")
        if args.game_action != "race":
            parser.error("Input plan requires the observed race startup path")
        try:
            args.input_plan = args.input_plan.resolve(strict=True)
            args.input_plan_data = load_input_plan(args.input_plan, args.duration)
        except (OSError, UnicodeError, ValueError) as error:
            parser.error(str(error))
    if bool(args.passive_reader) != bool(args.passive_plan):
        parser.error("Passive reader and passive plan must be supplied together")
    if args.passive_reader:
        if (args.launcher_action != "offline-start" or not args.bundled_d3dx
                or args.game_action != "race"):
            parser.error("Passive observer requires unchanged launcher/no arguments/offline-start/bundled/race")
        if not 0 <= args.passive_at_seconds <= args.duration - 20:
            parser.error("Passive observer time must be finite and reserve 20 seconds before duration")
        try:
            args.passive_reader = args.passive_reader.resolve(strict=True)
            args.passive_plan = args.passive_plan.resolve(strict=True)
            if not args.passive_reader.is_file() or not args.passive_plan.is_file():
                raise ValueError("Passive reader and plan must be regular files")
            with args.passive_reader.open("rb") as stream:
                header = stream.read(64)
                if len(header) != 64 or header[:2] != b"MZ":
                    raise ValueError("Passive reader must be an existing PE executable")
                stream.seek(int.from_bytes(header[60:64], "little"))
                if stream.read(4) != b"PE\0\0":
                    raise ValueError("Passive reader must be an existing PE executable")
            args.passive_data = validate_passive_plan(args.passive_plan)
            args.passive_data["reader_sha256"] = digest(args.passive_reader)
        except (OSError, UnicodeError, ValueError) as error:
            parser.error(str(error))
    if args.inside:
        inside(args)
        return
    original = args.original.resolve(strict=True)
    if not (original / args.entry).is_file():
        parser.error("Extracted entrypoint is missing")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    output.chmod(0o700)
    script = Path(__file__).resolve()
    sandbox = ["/usr/bin/bwrap", "--unshare-all", "--die-with-parent", "--new-session",
               "--cap-drop", "ALL", "--clearenv", "--ro-bind", "/usr", "/usr",
               "--ro-bind", "/bin", "/bin",
               "--ro-bind", "/lib", "/lib", "--ro-bind", "/lib64", "/lib64",
               "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp",
               "--ro-bind", str(original), "/original", "--bind", str(output), "/scratch",
               "--ro-bind", str(script), "/observer.py"]
    for path in ["/etc/fonts", "/etc/ld.so.cache"]:
        if Path(path).exists():
            sandbox += ["--ro-bind", path, path]
    if args.input_plan:
        sandbox += ["--ro-bind", str(args.input_plan), "/input-plan.json"]
    if args.passive_reader:
        sandbox += ["--ro-bind", str(args.passive_reader), "/passive-reader.exe",
                    "--ro-bind", str(args.passive_plan), "/passive-plan.tsv"]
    sandbox += ["--setenv", "PATH", "/usr/bin", "--setenv", "LANG", "C.UTF-8",
                "--chdir", "/scratch", "--", "/usr/bin/python3", "/observer.py",
                "--inside", "--output", "/scratch", "--entry", args.entry,
                "--duration", str(args.duration), "--host-netns", os.readlink("/proc/self/ns/net"),
                "--private-path", str(Path.home())]
    sandbox += ["--capture-interval", str(args.capture_interval)]
    if args.input_plan:
        sandbox += ["--input-plan", "/input-plan.json"]
    if args.launcher_action:
        sandbox += ["--launcher-action", args.launcher_action]
    if args.launcher_gui_seconds:
        sandbox += ["--launcher-gui-seconds", str(args.launcher_gui_seconds)]
    if args.bundled_d3dx:
        sandbox += ["--bundled-d3dx"]
    if args.trace_d3d:
        sandbox += ["--trace-d3d"]
    if args.disable_key_repeat:
        sandbox += ["--disable-key-repeat"]
    if args.enable_key_repeat:
        sandbox += ["--enable-key-repeat"]
    if args.game_action:
        sandbox += ["--game-action", args.game_action]
    sandbox += ["--game-input-offset", str(args.game_input_offset)]
    if args.driving_inputs:
        sandbox += ["--driving-inputs"]
    if args.passive_reader:
        sandbox += ["--passive-reader", "/passive-reader.exe", "--passive-plan", "/passive-plan.tsv",
                    "--passive-at-seconds", str(args.passive_at_seconds)]
    invocation = {"command": sandbox, "original_entry_sha256":
                  digest(original / args.entry), "original_read_only": True,
                  "observer_script_sha256": digest(script),
                  "original_arguments": [],
                  "host_display_used": False, "installer_executed": False}
    if args.passive_reader:
        invocation["passive_reader_sha256"] = args.passive_data["reader_sha256"]
        invocation["passive_plan_sha256"] = args.passive_data["plan_sha256"]
    save(output / "invocation.json", invocation)
    rc = command(sandbox, output / "sandbox.log", args.duration + 100)
    save(output / "sandbox-exit.json", {"exit": rc})
    if rc == 0:
        for name in ["prefix", "home", "app"]:
            path = output / name
            if path.is_symlink():
                path.unlink()
            elif path.exists():
                shutil.rmtree(path)
    print(json.dumps({"output": str(output), "sandbox_exit": rc}))
    if rc != 0:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
