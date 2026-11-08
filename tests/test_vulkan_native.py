#!/usr/bin/env python3
"""Exercise actual native gameplay source frames with Vulkan raster and hardware RT.

Replay the original Play/drive/robot-switch input at accepted 32 Hz source ticks.
Run on a real SDL display with the local validation-layer environment configured.
Only completed ray queries, denoise dispatches and captured PPM pixels prove RT;
inspector scenes, synthetic meshes and software tracing are not substitutes.
With --check-pbr-presets, also compare real source320 Enhanced/Ultra PBR captures
against same-preset PBR-off references to reject broad black-world collapse.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from test_native_window import ppm_token


def require(condition, message):
    if not condition:
        raise ValueError(message)


def capture_pixels(path, width, height):
    with path.open("rb") as stream:
        header = [ppm_token(stream) for _ in range(4)]
        require(header == [b"P6", str(width).encode(), str(height).encode(), b"255"],
                f"Unexpected capture header: {header!r}")
        pixels = stream.read()
    require(len(pixels) == width * height * 3, "Incomplete framebuffer capture")
    require(any(pixels[i:i + 3] != pixels[:3] for i in range(3, len(pixels), 3)),
            "Framebuffer is a single clear color, not original-world draw evidence")
    return hashlib.sha256(pixels).hexdigest(), pixels


def check_pbr_world(name, reference, pixels, width, height):
    # Source320 lower-middle world only: retain broad lighting/color, not pixel
    # parity. PBR may change individual materials; sky/HUD cannot rescue this gate.
    xs = range(width // 10, width * 9 // 10)
    ys = range(height * 35 // 100, height * 80 // 100)
    selected = []
    for y in ys:
        for x in xs:
            offset = (y * width + x) * 3
            rgb = reference[offset:offset + 3]
            if max(rgb) >= 64 and max(rgb) - min(rgb) >= 24:
                selected.append(offset)
    require(len(selected) >= max(100, len(xs) * len(ys) // 20),
            f"{name}: insufficient clearly lit colored reference-world pixels: {len(selected)}")
    nonblack = colored = 0
    for offset in selected:
        rgb = pixels[offset:offset + 3]
        if max(rgb) >= 32:
            nonblack += 1
            if max(rgb) - min(rgb) >= 16:
                colored += 1
    require(nonblack >= len(selected) * 0.5 and colored >= len(selected) * 0.35,
            f"{name}: broad black/colorless world collapse: reference={len(selected)}, "
            f"nonblack={nonblack}, colored={colored}")
    print(f"{name}: reference-world samples={len(selected)}; "
          f"nonblack retention={nonblack / len(selected):.1%}; "
          f"color retention={colored / len(selected):.1%}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--game-dir", required=True, type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--frames", type=int, default=320,
                        help="Exact accepted source-frame target, not a presentation count")
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--allow-fallback", action="store_true",
                        help="Check honest unsupported-RT fallback instead of requiring hardware RT")
    parser.add_argument("--require-validation", action="store_true")
    parser.add_argument("--check-pbr-presets", action="store_true",
                        help="Compare source320 Enhanced/Ultra PBR on/off world-color coverage "
                             "(RT off; requires --frames 320)")
    args = parser.parse_args()
    require(args.frames > 1 and args.width > 0 and args.height > 0,
            "Use at least two source frames and positive dimensions for temporal history")
    require(not args.check_pbr_presets or args.frames == 320,
            "--check-pbr-presets requires --frames 320 for its fixed gameplay camera mask")
    input_script = Path(__file__).resolve().with_name("source_backend_compare.input")
    temporary = None
    if args.output_dir is None:
        temporary = tempfile.TemporaryDirectory(prefix="citroen-vulkan-native-")
        root = Path(temporary.name)
    else:
        root = args.output_dir.resolve()
        root.mkdir(parents=True, exist_ok=False)
    environment = os.environ.copy()
    hashes = {}
    raster_state = None
    pbr_references = {}
    runs = [(name, mask, "custom", None) for name, mask in
            (("raster", 0), ("reflections", 1), ("shadows", 2), ("gi", 4), ("all", 7))]
    if args.check_pbr_presets:
        runs.extend((f"{preset}-pbr-{mode}", 0, preset, mode)
                    for preset in ("enhanced", "ultra") for mode in ("off", "on"))
    try:
        for name, mask, preset, pbr in runs:
            capture = root / f"{name}.ppm"
            report = root / f"{name}.json"
            environment["XDG_CONFIG_HOME"] = str(root / f"{name}-config")
            command = [str(args.runtime.resolve()), "--game-dir", str(args.game_dir.resolve()),
                       "--quality", preset, "--renderer", "vulkan", "--denoising", "standard",
                       "--width", str(args.width), "--height", str(args.height),
                       "--windowed", "--source-frames", str(args.frames), "--vsync", "off",
                       "--frame-limit", "0", "--input-script", str(input_script),
                       "--capture", str(capture), "--diagnostics-json", str(report)]
            if pbr is not None:
                command.extend(("--pbr", pbr))
            for profile in ("atq", "mtq", "stq", "cgm", "shq", "ano", "bloom"):
                command.extend((f"--source-{profile}", "0"))
            for flag, bit in (("--rt-reflections", 1), ("--rt-shadows", 2), ("--rt-gi", 4)):
                command.extend((flag, "on" if mask & bit else "off"))
            result = subprocess.run(command, cwd=root, env=environment,
                                    capture_output=True, text=True, errors="replace", timeout=args.timeout)
            (root / f"{name}.stdout").write_text(result.stdout)
            (root / f"{name}.stderr").write_text(result.stderr)
            require(result.returncode == 0,
                    f"{name}: runtime status {result.returncode}\n{result.stdout}\n{result.stderr}")
            data = json.loads(report.read_text())
            require(data["runtime_kind"] == "native-game"
                    and data["backend"] == "vulkan" and data["opengl"] is None,
                    f"{name}: Vulkan did not independently render actual gameplay")
            require(data["source_frames"] == args.frames
                    and data["requested_source_frames"] == args.frames
                    and data["completed_source_frames"] == args.frames
                    and data["last_source_frame_index"] == args.frames - 1
                    and data["target_reached"] is True
                    and data["finite_stop_failure_reason"] is None,
                    f"{name}: exact accepted source-frame target was not reached")
            require(data["rendered_frames"] >= 2,
                    f"{name}: fewer than two completed gameplay presentations")
            if args.frames >= 320:
                require(data["input_scenario_finished"],
                        f"{name}: original Play/drive/robot-switch input did not finish")
            require(Path(data["sources"]["input_script"]) == input_script,
                    f"{name}: runtime did not use the original gameplay input")
            game = data["game"]
            state = tuple(game[key] for key in
                          ("source_raw", "source_vector_raw", "source_array_raw", "source_state"))
            require(all(isinstance(value, dict) and value for value in state),
                    f"{name}: missing observed raw source-state comparison evidence")
            require(game["source_profile"] ==
                    {key: 0 for key in ("atq", "mtq", "stq", "cgm", "shq", "ano", "bloom")},
                    f"{name}: original source graphics profile changed")
            require(game["source_callback_frames"] == args.frames
                    and game["source_state"]["completed_source_frames"] == args.frames
                    and game["source_state"]["last_source_frame_index"] == args.frames - 1,
                    f"{name}: raw gameplay state does not describe the completed source target")
            if name == "raster":
                raster_state = state
            else:
                require(state == raster_state,
                        f"{name}: rendering effects changed observed raw source simulation state")
            scene = data["scene"]
            require(scene["world_instances"] > 0 and scene["world_models"] > 0
                    and scene["world_entities"] > 0 and scene["world_triangles"] > 0
                    and scene["draw_calls"] > 0,
                    f"{name}: no actual original-world geometry draw evidence")
            require(scene["world_entities"] == game["prepared_world_entities"]
                    and game["source_loaded_world_entities"] ==
                    game["prepared_original_world_entities"] > 0
                    and game["source_born_leaf_entities"] ==
                    game["source_initialized_leaf_entities"] ==
                    game["prepared_leaf_entities"] > 0
                    and game["prepared_world_entities"] ==
                    game["prepared_original_world_entities"] + game["prepared_helper_entities"]
                    and scene["world_entities"] ==
                    game["prepared_original_world_entities"] + game["source_initialized_leaf_entities"]
                    and scene["world_sprite_entities"] > 0
                    and scene["world_omitted_sprite_entities"] == 0
                    and scene["sprite_private_flags_unknown"] == 0
                    and scene["sprite_cached_euler_unknown"] == 0
                    and scene["source_sprite_omissions"]["total_omitted"] == 0
                    and scene["source_sprite_omissions"]["entries"] == []
                    and scene["source_sprite_omissions"]["list_truncated"] is False,
                    f"{name}: incomplete original-source entity/sprite packet")
            gpu = data["vulkan"]
            require(gpu["device_name"] and gpu["api_version"] > 0,
                    f"{name}: missing selected physical-device evidence")
            require(gpu["draw_calls"] ==
                    gpu["material_draw_calls"] + gpu["shadow_draw_calls"] +
                    gpu["particle_draw_calls"] + gpu["ui_draw_calls"] + gpu["post_passes"]
                    and gpu["ui_mesh_draw_calls"] <= gpu["ui_draw_calls"]
                    and scene["draw_calls"] == gpu["draw_calls"] - gpu["ui_draw_calls"]
                    and data["draw_calls"]["ui"] == gpu["ui_draw_calls"],
                    f"{name}: inconsistent actual per-frame Vulkan draw-call accounting")
            require(gpu["world_draw_calls"] > 0 and gpu["frames"] == data["rendered_frames"]
                    and gpu["source_sprite_omissions"]["total_omitted"] == 0
                    and gpu["source_sprite_omissions"]["entries"] == []
                    and gpu["source_sprite_omissions"]["list_truncated"] is False
                    and gpu["sprite_private_flags_unknown"] == 0
                    and gpu["sprite_cached_euler_unknown"] == 0
                    and not gpu["source_sprite_fallback_reason"],
                    f"{name}: Vulkan omitted or could not resolve source sprites")
            if pbr is not None:
                resolved = gpu["resolved_graphics"]
                require(resolved["pbr"] is (pbr == "on"),
                        f"{name}: requested PBR path was not applied")
                require(gpu["pbr_draw_calls"] > 0 if pbr == "on"
                        else gpu["pbr_draw_calls"] == 0,
                        f"{name}: incorrect actual PBR draw evidence: {gpu['pbr_draw_calls']}")
            rays = gpu["raytracing"]
            require(gpu["rt_requested"] == mask, f"{name}: request flags changed")
            require(gpu["validation_errors"] == 0, f"{name}: Vulkan validation errors")
            if args.require_validation:
                require(gpu["validation_enabled"], f"{name}: validation layer was not enabled")
            if gpu["rt_supported"]:
                require(rays["supported"] and rays["available_effects"] == 7
                        and rays["source_coverage_ready"] is True,
                        f"{name}: hardware pipeline did not accept the complete actual source packet")
            window = data["window"]
            width, height = window["drawable_width"], window["drawable_height"]
            hashes[name], pixels = capture_pixels(capture, width, height)
            if pbr == "off":
                pbr_references[preset] = (pixels, width, height)
            elif pbr == "on":
                reference, reference_width, reference_height = pbr_references[preset]
                require((width, height) == (reference_width, reference_height),
                        f"{name}: PBR on/off capture dimensions differ")
                check_pbr_world(name, reference, pixels, width, height)
            if mask and gpu["rt_supported"]:
                require(gpu["rt_enabled"] == mask and gpu["rt_supported_effects"] & mask == mask
                        and not gpu["rt_fallback_reason"],
                        f"{name}: supported requested effect not enabled")
                require(gpu["rt_triangles"] > 0 and gpu["rt_instances"] > 0
                        and gpu["rt_queries"] > 0
                        and gpu["rt_queries"] == sum(rays["ray_query_totals"].values()),
                        f"{name}: no real acceleration geometry and traversal evidence")
                require(rays["supported"]
                        and rays["requested_effects"] == mask
                        and rays["enabled_effects"] == mask
                        and rays["available_effects"] & mask == mask
                        and rays["unique_triangles"] > 0
                        and rays["instanced_triangles"] > 0 and rays["instances"] > 0
                        and 2 <= rays["completed_query_frames"] <= data["rendered_frames"]
                        and rays["last_valid_pixels"] > 0,
                        f"{name}: missing completed hardware traversal frames for original geometry")
                for effect, bit in (("reflection", 1), ("shadow", 2), ("gi", 4)):
                    total = rays["ray_query_totals"][effect]
                    last = rays["last_ray_queries"][effect]
                    require((total > 0 and last > 0) if mask & bit else (total == 0 and last == 0),
                            f"{name}: incorrect completed {effect} query evidence")
                require(gpu["rt_denoising"] == rays["denoising_mode"] == "standard"
                        and gpu["rt_denoise_passes"] == rays["denoise_passes"] == 1
                        and rays["completed_denoise_dispatches"] == rays["completed_query_frames"]
                        and rays["temporal_denoise_available"],
                        f"{name}: missing completed temporal-capable denoise dispatch evidence")
                if args.frames >= 320:
                    require(gpu["rt_blas_updates"] == rays["blas_updates"] > 0
                            and gpu["rt_tlas_updates"] == rays["tlas_updates"] > 1,
                            f"{name}: real gameplay geometry did not refit BLAS and rebuild TLAS")
                require(hashes[name] != hashes["raster"],
                        f"{name}: RT capture unchanged from raster")
            elif mask:
                require(args.allow_fallback, f"{name}: hardware RT unavailable: {gpu['rt_fallback_reason']}")
                require(gpu["rt_enabled"] == 0 and gpu["rt_fallback_reason"]
                        and gpu["rt_queries"] == 0 and rays["enabled_effects"] == 0
                        and rays["completed_query_frames"] == 0
                        and rays["completed_denoise_dispatches"] == 0
                        and all(value == 0 for value in rays["ray_query_totals"].values()),
                        f"{name}: unsupported effect did not honestly fall back")
            else:
                require(gpu["rt_enabled"] == 0 and gpu["rt_queries"] == 0
                        and rays["completed_query_frames"] == 0
                        and rays["completed_denoise_dispatches"] == 0
                        and all(value == 0 for value in rays["ray_query_totals"].values()),
                        "Original raster unexpectedly traversed or denoised rays")
            print(f"{name}: {gpu['device_name']}; RT requested={mask} enabled={gpu['rt_enabled']}; "
                  f"queries={gpu['rt_queries']}; completed RT frames={rays['completed_query_frames']}; "
                  f"denoise dispatches={rays['completed_denoise_dispatches']}; "
                  f"source frames={data['completed_source_frames']}; presents={data['rendered_frames']}; "
                  f"framebuffer={hashes[name]}")
        print(f"Vulkan capture/diagnostic evidence: {root}")
        return 0
    finally:
        if temporary is not None:
            temporary.cleanup()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        print(f"Native Vulkan regression failed: {error}")
        raise SystemExit(1)
