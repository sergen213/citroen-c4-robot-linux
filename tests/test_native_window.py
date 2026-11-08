#!/usr/bin/env python3
"""Exercise the actual SDL X11 window and OpenGL framebuffer under private Xvfb."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def ppm_token(stream):
    whitespace = b" \t\r\n\v\f"
    byte = stream.read(1)
    while byte and (byte in whitespace or byte == b"#"):
        if byte == b"#":
            stream.readline()
        byte = stream.read(1)
    token = byte
    byte = stream.read(1)
    while byte and byte not in whitespace:
        token += byte
        byte = stream.read(1)
    if not token or not byte:
        raise ValueError("Incomplete PPM header")
    # Consume only the token's terminating delimiter, never binary pixel whitespace.
    return token


def check_capture(capture):
    with capture.open("rb") as stream:
        header = [ppm_token(stream) for _ in range(4)]
        if header != [b"P6", b"640", b"480", b"255"]:
            raise ValueError(f"Unexpected PPM header: {header!r}")
        pixel_bytes = len(stream.read())
        if pixel_bytes != 640 * 480 * 3:
            raise ValueError(f"Unexpected PPM pixel byte count: {pixel_bytes}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--xvfb-run", required=True, type=Path)
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    xvfb_run = args.xvfb_run.resolve()
    stdout = stderr = ""
    try:
        with tempfile.TemporaryDirectory(prefix="citroen-native-window-") as directory:
            root = Path(directory)
            game = root / "game"
            config = root / "config"
            game.mkdir()
            config.mkdir()
            capture = root / "frame.ppm"
            environment = os.environ.copy()
            environment.update(
                XDG_CONFIG_HOME=str(config),
                SDL_VIDEODRIVER="x11",
                # Mesa software OpenGL still renders the real native framebuffer.
                LIBGL_ALWAYS_SOFTWARE="1",
            )
            metadata = subprocess.run(
                [str(runtime), "--diagnostics", "--game-dir", str(root / "missing-assets")],
                cwd=root, env=environment, capture_output=True, text=True,
                errors="replace", timeout=30,
            )
            stdout, stderr = metadata.stdout, metadata.stderr
            if metadata.returncode != 0:
                raise ValueError("Headless metadata diagnostics attempted to load game assets")
            empty_game = subprocess.run(
                [str(runtime), "--game-dir", str(game), "--frames", "1"],
                cwd=root, env=environment, capture_output=True, text=True,
                errors="replace", timeout=30,
            )
            stdout, stderr = empty_game.stdout, empty_game.stderr
            if empty_game.returncode == 0 or "bogazici.wmb" not in stderr:
                raise ValueError("Default gameplay did not reject missing original world assets")
            query = subprocess.run(
                [str(xvfb_run), "-a", str(runtime), "--graphics-capabilities",
                 "--game-dir", str(root / "missing-assets")],
                cwd=root, env=environment, capture_output=True, text=True,
                errors="replace", timeout=30,
            )
            stdout, stderr = query.stdout, query.stderr
            if query.returncode != 0:
                raise ValueError("Graphics capability query attempted to load game assets or failed")
            if any(config.iterdir()):
                raise ValueError("Metadata, rejected gameplay or capability query created user configuration")
            result = subprocess.run(
                [str(xvfb_run), "-a", str(runtime),
                 "--inspect", "--game-dir", str(game), "--width", "640", "--height", "480",
                 "--windowed", "--frames", "3", "--vsync", "off",
                 "--frame-limit", "0", "--capture", str(capture)],
                cwd=root, env=environment, capture_output=True, text=True,
                errors="replace", timeout=30,
            )
            stdout, stderr = result.stdout, result.stderr
            if result.returncode != 0:
                raise ValueError(f"Runtime exited with status {result.returncode}")
            check_capture(capture)

            # A real missing ICD must not turn an implicit quality choice into
            # an unplayable startup. Explicit Vulkan requests remain errors.
            missing_icd = root / "nonexistent-vulkan-icd.json"
            for case in ("implicit-enhanced", "forced-vulkan", "forced-capabilities"):
                case_config = root / f"{case}-config"
                case_config.mkdir()
                case_environment = environment.copy()
                case_environment.update(
                    XDG_CONFIG_HOME=str(case_config),
                    VK_DRIVER_FILES=str(missing_icd),
                    VK_ICD_FILENAMES=str(missing_icd),
                )
                if case == "forced-capabilities":
                    options = [
                        "--graphics-capabilities", "--renderer", "vulkan",
                        "--game-dir", str(root / "missing-assets"),
                    ]
                else:
                    options = [
                        "--inspect", "--game-dir", str(game),
                        "--width", "640", "--height", "480", "--windowed",
                        "--frames", "3", "--vsync", "off", "--frame-limit", "0",
                    ]
                    if case == "implicit-enhanced":
                        fallback_capture = root / "fallback.ppm"
                        report = root / "fallback.json"
                        options += [
                            "--quality", "enhanced", "--capture", str(fallback_capture),
                            "--diagnostics-json", str(report),
                        ]
                    else:
                        options += ["--renderer", "vulkan"]
                result = subprocess.run(
                    [str(xvfb_run), "-a", str(runtime), *options],
                    cwd=root, env=case_environment, capture_output=True, text=True,
                    errors="replace", timeout=30,
                )
                stdout, stderr = result.stdout, result.stderr
                if case != "implicit-enhanced":
                    if result.returncode == 0:
                        raise ValueError(f"{case} silently accepted unavailable Vulkan")
                    continue
                if result.returncode != 0:
                    raise ValueError(f"Implicit Enhanced failed with unavailable Vulkan: {result.returncode}")
                check_capture(fallback_capture)
                diagnostics = json.loads(report.read_text(encoding="utf-8"))
                if diagnostics["backend"] != "opengl" or diagnostics["requested_backend"] != "vulkan":
                    raise ValueError("Fallback did not preserve requested Vulkan and activate OpenGL")
                reason = diagnostics["backend_fallback_reason"]
                if not isinstance(reason, str) or not reason.strip():
                    raise ValueError("Backend fallback lacks an explicit reason")
                if not diagnostics["opengl"]["renderer"] or diagnostics["vulkan"] is not None:
                    raise ValueError("Fallback lacks an actual GL renderer or reports a Vulkan RT backend")
                if diagnostics["rendered_frames"] <= 0:
                    raise ValueError("Fallback did not present native inspection frames")
                capabilities = diagnostics["graphics"]["capabilities"]
                resolved = diagnostics["graphics"]["resolved"]
                if resolved["requested_preset"] != "enhanced" or resolved["effective_preset"] != "custom":
                    raise ValueError("Fallback did not preserve Enhanced intent and report effective Custom")
                # Empty inspection has no connected scene PBR/shadow pipelines.
                # Assert only features supported by this actual fallback context.
                for feature in ("ssao", "pbr", "internal_hdr", "bloom"):
                    if resolved[feature] != capabilities[feature]:
                        raise ValueError(f"Enhanced fallback did not resolve observed {feature} capability")
                expected_post_aa = "fxaa" if capabilities["fxaa"] else "off"
                if resolved["post_aa"] != expected_post_aa:
                    raise ValueError("Enhanced fallback did not resolve observed post-AA capability")
                if resolved["samples"] not in capabilities["sample_counts"]:
                    raise ValueError("Enhanced fallback selected unsupported MSAA")
    except subprocess.TimeoutExpired as error:
        print(f"Native window regression timed out after {error.timeout} seconds")
        print(f"stdout:\n{error.stdout or ''}\nstderr:\n{error.stderr or ''}")
        return 1
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Native window regression failed: {error}")
        print(f"stdout:\n{stdout}\nstderr:\n{stderr}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
