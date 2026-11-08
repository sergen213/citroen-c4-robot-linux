#!/usr/bin/env python3
"""Offline corresponding-source contracts; run directly with Python."""
import copy
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tarfile
import tempfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from appimage import acquire, source_bundle


def rejected(output, epoch, tools, exceptions=(ValueError,), *, runtime_manifest=None) -> None:
    try:
        source_bundle.create(output, epoch, tools, runtime_manifest=runtime_manifest)
    except exceptions:
        pass
    else:
        raise AssertionError(f"Invalid bundle request was accepted: {output.name}")
    assert not os.path.lexists(output), "Failed request left an output"
    assert not list(output.parent.rglob(".source-bundle*")), "Temporary bundle leaked"


def ffmpeg_contracts(root: Path, tools: Path, epoch: int) -> None:
    import build_ffmpeg
    import runtime_bundle

    materials = root / "ffmpeg-materials"
    materials.mkdir()
    source = materials / "ffmpeg-fixture.tar.xz"
    source.write_bytes(b"byte-exact official FFmpeg source fixture")
    signature = materials / (source.name + ".asc")
    signature.write_bytes(b"official signature fixture")
    key = materials / "ffmpeg-devel.asc"
    key.write_bytes(b"official key fixture")
    recipe = materials / "build_ffmpeg.py"
    recipe.write_bytes(b"# byte-exact authored recipe fixture\n")
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    lock = materials / "ffmpeg.lock.json"
    specification = {
        "archive": source.name, "source_sha256": digest(source),
        "provenance": {"signature_sha256": digest(signature), "key_sha256": digest(key)},
    }
    lock.write_text(json.dumps(specification) + "\n")
    prefixes = [root / "ffmpeg-first-prefix", root / "different-staging" / "ffmpeg-prefix"]
    configuration = {
        "config.h": b"/* fixture configuration */\n",
        "config_components.h": b"#define CONFIG_WMV3_DECODER 1\n",
        "ffbuild/config.mak": b"SRC_PATH=.\nCC=/usr/bin/gcc\n",
        "ffbuild/config.sh": b"#!/bin/sh\nsource_path='.'\n",
    }
    published = {
        **configuration, "ffbuild/config.mak": b"SRC_PATH=.\nCC=tool/gcc\n",
    }
    for prefix in prefixes:
        for name, data in configuration.items():
            destination = prefix / source_bundle.CONFIGURATION_ROOT / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
    original = {
        "format": 1, "component": "FFmpeg", "version": "fixture",
        "source_sha256": digest(source), "lock_sha256": digest(lock),
        "proof": specification["provenance"],
        "built_libraries": [{"path": "lib/libavcodec.so.62.28.103",
                             "sha256": "a" * 64}],
        "configuration": [
            {"path": source_bundle.CONFIGURATION_ROOT + name,
             "sha256": hashlib.sha256(data).hexdigest()}
            for name, data in configuration.items()
        ],
    }
    outputs = []
    observed = []

    def validated(prefix):
        observed.append(prefix)
        return {
            **copy.deepcopy(original), "prefix": str(prefix),
            "source_cache_path": str(prefix / "cache" / source.name),
            "configure_args": [
                "--prefix=" + str(prefix), "--libdir=" + str(prefix / "lib"),
                "--shlibdir=" + str(prefix / "lib"),
                "--pkgconfigdir=" + str(prefix / "lib/pkgconfig"),
                "--cc=/usr/bin/gcc", "--enable-shared", "--disable-gpl",
            ],
        }

    with patch.object(source_bundle, "FFMPEG_LOCK", lock), \
            patch.object(source_bundle, "FFMPEG_RECIPE", recipe), \
            patch.object(build_ffmpeg, "acquire_source", return_value=source) as acquire_source, \
            patch.object(runtime_bundle, "validate_ffmpeg_prefix", side_effect=validated) as validate:
        for index, prefix in enumerate(prefixes):
            with patch.dict(os.environ, {"C4_FFMPEG_PREFIX": str(prefix)}):
                output = root / f"ffmpeg-{index}.tar.gz"
                source_bundle.create(output, epoch, tools)
                outputs.append(output.read_bytes())
        assert observed == prefixes
        assert outputs[0] == outputs[1], "Provenance-only path changes altered bundle"
        canonical = validated(prefixes[0])
        canonical["prefix"] = "ffmpeg-prefix"
        canonical["source_cache_path"] = "archive/" + source.name
        canonical["configure_args"] = [
            "--prefix=ffmpeg-prefix", "--libdir=ffmpeg-prefix/lib",
            "--shlibdir=ffmpeg-prefix/lib", "--pkgconfigdir=ffmpeg-prefix/lib/pkgconfig",
            "--cc=tool/gcc", "--enable-shared", "--disable-gpl",
        ]
        for record in canonical["configuration"]:
            name = record["path"].removeprefix(source_bundle.CONFIGURATION_ROOT)
            record["original_sha256"] = record["sha256"]
            record["published_sha256"] = hashlib.sha256(published[name]).hexdigest()
            record["published_path"] = "configuration/" + name
        expected = {
            "archive/" + source.name: source.read_bytes(),
            "archive/" + signature.name: signature.read_bytes(),
            "archive/" + key.name: key.read_bytes(),
            "build_ffmpeg.py": recipe.read_bytes(),
            "ffmpeg.lock.json": lock.read_bytes(),
            "ffmpeg-source.json": (json.dumps(canonical, indent=2, sort_keys=True) + "\n").encode(),
            **{"configuration/" + name: data for name, data in published.items()},
        }
        with tarfile.open(fileobj=io.BytesIO(outputs[0]), mode="r:gz") as archive:
            actual = {entry.name.removeprefix(source_bundle.ROOT + "/ffmpeg/"):
                      archive.extractfile(entry).read() for entry in archive.getmembers()
                      if entry.name.startswith(source_bundle.ROOT + "/ffmpeg/")}
            assert actual == expected
        with patch.dict(os.environ, {"C4_FFMPEG_PREFIX": str(prefixes[0])}):
            validate.side_effect = ValueError("Invalid actual built prefix")
            rejected(root / "ffmpeg-invalid-prefix.tar.gz", epoch, tools)
            validate.side_effect = validated
            for path in (source, signature, key):
                original_bytes = path.read_bytes()
                path.write_bytes(b"tampered input")
                rejected(root / ("ffmpeg-bad-" + path.name + ".tar.gz"), epoch, tools)
                path.write_bytes(original_bytes)
            proof_path = prefixes[0] / source_bundle.CONFIGURATION_ROOT / "config.h"
            proof_path.write_bytes(b"tampered configure proof")
            rejected(root / "ffmpeg-bad-config.tar.gz", epoch, tools)
            proof_path.write_bytes(configuration["config.h"])
            unsafe_proof = b"/* source */\n#define UNKNOWN \"/private/staging/source\"\n"
            proof_path.write_bytes(unsafe_proof)
            unexpected = validated(prefixes[0])
            unexpected["configuration"][0]["sha256"] = hashlib.sha256(unsafe_proof).hexdigest()
            validate.side_effect = None
            validate.return_value = unexpected
            rejected(root / "ffmpeg-unknown-proof-path.tar.gz", epoch, tools)
            proof_path.write_bytes(configuration["config.h"])
            unsafe = validated(prefixes[0])
            unsafe["unexpected_path"] = "/private/staging/source"
            validate.side_effect = None
            validate.return_value = unsafe
            rejected(root / "ffmpeg-unknown-path.tar.gz", epoch, tools)
            unsafe.pop("unexpected_path")
            unsafe["configure_args"].append("--extra-ldflags=-L/private/library")
            rejected(root / "ffmpeg-unknown-configure-path.tar.gz", epoch, tools)
            unsafe_roots = validated(prefixes[0])
            unsafe_roots["provenance_roots"] = {
                "source": "/fixture-source", "build": "/fixture-source",
                "prefix": str(prefixes[0]), "unknown": "/private/unexplained",
            }
            validate.return_value = unsafe_roots
            rejected(root / "ffmpeg-unknown-root.tar.gz", epoch, tools)
            known = validated(prefixes[0])
            known["provenance_roots"] = {
                "source": "/fixture-work/ffmpeg-fixture",
                "build": "/fixture-work/ffmpeg-fixture", "work": "/fixture-work",
                "prefix": str(prefixes[0]),
            }
            tool_paths = {"cc": "/usr/bin/gcc", "ld": "/usr/bin/gcc",
                          "ar": "/usr/bin/ar", "ranlib": "/usr/bin/ranlib",
                          "strip": "/usr/bin/strip", "nm": "/usr/bin/nm",
                          "x86asmexe": "/usr/bin/nasm"}
            known["configure_args"] = [
                argument for argument in known["configure_args"]
                if not argument.startswith("--cc=")
            ] + ["--" + option + "=" + value for option, value in tool_paths.items()]
            known["tool_paths"] = tool_paths
            validate.return_value = known
            current_output = root / "ffmpeg-current-schema.tar.gz"
            source_bundle.create(current_output, epoch, tools)
            with tarfile.open(current_output) as archive:
                current = json.loads(archive.extractfile(
                    source_bundle.ROOT + "/ffmpeg/ffmpeg-source.json").read())
            assert current["tool_paths"] == {
                name: "tool/" + Path(value).name for name, value in tool_paths.items()
            }
            assert current["provenance_roots"] == {
                name: "ffmpeg-" + name for name in known["provenance_roots"]
            }
            known["tool_paths"]["cc"] = "/unexpected/compiler"
            rejected(root / "ffmpeg-mismatched-tools.tar.gz", epoch, tools)
        ffmpeg_bom = {"components": [{"path": "lib/libavcodec.so.62", "sha256": "b" * 64,
                                     "provenance": {
                                         "type": "upstream-source-build", "package": "FFmpeg",
                                         **{key: original[key] for key in
                                            ("version", "source_sha256", "lock_sha256", "proof")}}}]}
        validate.side_effect = validated
        with patch.dict(os.environ, {"C4_FFMPEG_PREFIX": str(prefixes[0])}):
            source_bundle.create(root / "ffmpeg-deployed.tar.gz", epoch, tools,
                                 runtime_manifest=ffmpeg_bom)
            ffmpeg_bom["components"][0]["provenance"]["source_sha256"] = "c" * 64
            try:
                source_bundle.create(root / "ffmpeg-deployed-mismatch.tar.gz", epoch, tools,
                                     runtime_manifest=ffmpeg_bom)
            except ValueError:
                pass
            else:
                raise AssertionError("Mismatched deployed FFmpeg source was accepted")
        with patch.dict(os.environ, {}, clear=True):
            try:
                source_bundle.create(root / "ffmpeg-deployed-missing.tar.gz", epoch, tools,
                                     runtime_manifest=ffmpeg_bom)
            except ValueError:
                pass
            else:
                raise AssertionError("Deployed FFmpeg without source was accepted")
        assert acquire_source.call_args.args == (tools,)

def dbus_contracts(root: Path, tools: Path, epoch: int) -> None:
    import build_dbus
    import runtime_bundle

    materials = root / "dbus-materials"
    materials.mkdir()
    source = materials / "dbus-fixture.tar.xz"
    source.write_bytes(b"authentic source fixture bytes")
    recipe = materials / "build_dbus.py"
    recipe.write_bytes(b"# exact owned build recipe fixture\n")
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    helper = materials / "build_ffmpeg.py"
    helper.write_bytes(b"# exact imported build helper fixture\n")
    lock = materials / "dbus.lock.json"
    lock.write_text(json.dumps({"archive": source.name, "source_sha256": digest(source),
                               "default_paths": ["/run", "/run/dbus/system_bus_socket"],
                               "meson_options": {"runtime_dir": "/run"},
                               "recipe_helpers": [{"path": helper.name, "sha256": digest(helper)}]}))
    prefix = materials / "prefix"
    configuration = prefix / "share/licenses/D-Bus/source-proof/config.h"
    configuration.parent.mkdir(parents=True)
    configuration.write_bytes(b'#define DBUS_SYSTEM_BUS_DEFAULT_ADDRESS "unix:path=/run/dbus/system_bus_socket"\n')
    actual = {"format": 1, "component": "D-Bus", "version": "1.16.2",
              "prefix": str(prefix), "source_cache_path": str(source),
              "source_sha256": digest(source), "lock_sha256": digest(lock),
              "recipe_sha256": digest(recipe), "proof": {"type": "fixture-authentication"},
              "configuration": [{"path": configuration.relative_to(prefix).as_posix(),
                                 "sha256": digest(configuration)}]}
    actual["built_libraries"] = [{"path": "lib/libdbus-1.so.3.38.3", "sha256": "a" * 64}]
    actual["tool_paths"] = {"cc": "/private/toolchain/cc"}
    actual["system_paths"] = ["/usr/bin", "/bin"]
    actual["build_environment"] = {"CC": "/private/toolchain/cc", "PATH": "/private/toolchain:/usr/bin:/bin"}
    bom = {"components": [{"path": "lib/libdbus-1.so.3", "sha256": "b" * 64,
                           "provenance": {"type": "upstream-source-build", "package": "D-Bus",
                                          "license_expression": "GPL-2.0-or-later",
                                          "original_sha256": "a" * 64,
                                          "configuration": copy.deepcopy(actual["configuration"]),
                                          "built_libraries": copy.deepcopy(actual["built_libraries"]),
                                          **{key: actual[key] for key in (
                                              "version", "source_sha256", "lock_sha256", "recipe_sha256", "proof")}}}]}
    with patch.object(source_bundle, "DBUS_LOCK", lock), \
            patch.object(source_bundle, "DBUS_RECIPE", recipe), \
            patch.object(build_dbus, "source_materials",
                         return_value=[("archive/" + source.name, source, digest(source))]), \
            patch.object(runtime_bundle, "validate_dbus_prefix", return_value=copy.deepcopy(actual)) as validate, \
            patch.dict(os.environ, {"C4_DBUS_PREFIX": str(prefix)}):
        output = root / "dbus-source.tar.gz"
        source_bundle.create(output, epoch, tools, runtime_manifest=bom)
        with tarfile.open(output) as archive:
            canonical = json.loads(archive.extractfile(
                source_bundle.ROOT + "/dbus/dbus-source.json").read())
            assert canonical["prefix"] == "dbus-prefix"
            assert canonical["recipe_sha256"] == digest(recipe)
            assert canonical["tool_paths"] == {"cc": "tool/cc"}
            assert canonical["build_environment"]["PATH"] == "tool-directory/cc:/usr/bin:/bin"
            assert archive.extractfile(source_bundle.ROOT + "/dbus/archive/" + source.name).read() == source.read_bytes()
            assert archive.extractfile(source_bundle.ROOT + "/dbus/helpers/" + helper.name).read() == helper.read_bytes()
            published = canonical["configuration"][0]
            assert archive.extractfile(source_bundle.ROOT + "/dbus/" + published["published_path"]).read() == configuration.read_bytes()
        # Meson's genuine runtime_dir description contains a bracketed default,
        # not a source location ending in ']'; keep the option and prose intact.
        options = configuration.parent / "meson-info/intro-buildoptions.json"
        options.parent.mkdir()
        runtime_option = {
            "name": "runtime_dir", "value": "/run", "section": "user",
            "machine": "any", "type": "string",
            "description": "Directory for transient runtime state [default: LOCALSTATEDIR/run or /run]",
        }
        option_records = [runtime_option, {
            "name": "prefix", "value": str(prefix), "type": "string",
            "description": "Installation prefix",
        }]
        options.write_text(json.dumps(option_records) + "\n")
        portable = copy.deepcopy(actual)
        portable["configuration"].append({
            "path": options.relative_to(prefix).as_posix(), "sha256": digest(options),
        })
        portable["meson_args"] = ["--prefix=" + str(prefix), "-Druntime_dir=/run"]
        portable["provenance_roots"] = {"prefix": str(prefix)}
        install = "/usr/lib/gcc/x86_64-pc-linux-gnu/16/"
        portable["tool_paths"]["gcc"] = "/usr/bin/gcc"
        portable["system_paths"].extend(["/usr/bin/ld", install.rstrip("/")])
        log = configuration.parent / "meson-logs/meson-log.txt"
        log.parent.mkdir()
        compiler_report = (
            "/usr/bin/ld -m elf_x86_64 -dynamic-linker /lib64/ld-linux-x86-64.so.2\n"
            "/usr/bin/ld: cannot find -lexecinfo: No such file or directory\n"
            "Command line: `/usr/bin/gcc testfile.c --print-search-dirs` -> 0\n"
            "stdout:\n"
            f"install: {install}\n"
            f"programs: ={install}:{install}../../../../x86_64-pc-linux-gnu/bin/\n"
            f"libraries: ={install}../../../../x86_64-pc-linux-gnu/lib/../lib/:/usr/lib/\n"
            "    System bus PID file             : /run/dbus/pid\n"
            "<GnuCCompiler: v16.2.1 `/usr/bin/gcc`>>>\n"
        )
        log_text = (f"Build Options: -Dprefix={prefix} -Druntime_dir=/run\n"
                    f"Build directory: [{prefix}]\n" + compiler_report)
        log.write_text(log_text)
        portable["configuration"].append({
            "path": log.relative_to(prefix).as_posix(), "sha256": digest(log),
        })
        validate.return_value = portable
        bracket_bom = copy.deepcopy(bom)
        bracket_bom["components"][0]["provenance"]["configuration"] = copy.deepcopy(
            portable["configuration"])
        bracket_output = root / "dbus-bracketed-runtime-default.tar.gz"
        source_bundle.create(bracket_output, epoch, tools, runtime_manifest=bracket_bom)
        with tarfile.open(bracket_output) as archive:
            published_options = json.loads(archive.extractfile(
                source_bundle.ROOT + "/dbus/configuration/meson-info/intro-buildoptions.json").read())
            assert published_options == [runtime_option, {
                "name": "prefix", "value": "dbus-prefix", "type": "string",
                "description": "Installation prefix",
            }]
            assert archive.extractfile(
                source_bundle.ROOT + "/dbus/configuration/meson-logs/meson-log.txt").read() == (
                    b"Build Options: -Dprefix=dbus-prefix -Druntime_dir=/run\n"
                    b"Build directory: [dbus-prefix]\n"
                    + compiler_report.replace("/usr/bin/gcc", "tool/gcc").encode())
            published_metadata = json.loads(archive.extractfile(
                source_bundle.ROOT + "/dbus/dbus-source.json").read())
            assert published_metadata["meson_args"] == [
                "--prefix=dbus-prefix", "-Druntime_dir=/run",
            ]
            assert published_metadata["recipe_sha256"] == digest(recipe)
            assert published_metadata["configuration"][1]["original_sha256"] == digest(options)
        for index, description in enumerate((
                "[/private/unexplained]", "[default: /run/../private]",
                "compiler: [-I/home/private/include]", "[/run]/private/host",
        )):
            invalid_options = copy.deepcopy(option_records)
            invalid_options[0]["description"] = description
            options.write_text(json.dumps(invalid_options) + "\n")
            unsafe = copy.deepcopy(portable)
            unsafe["configuration"][1]["sha256"] = digest(options)
            validate.return_value = unsafe
            rejected(root / f"dbus-bracketed-unsafe-{index}.tar.gz", epoch, tools)
        options.write_text(json.dumps(option_records) + "\n")
        for index, invalid_log in enumerate((
                log_text.replace("/lib64/ld-linux-x86-64.so.2", "/private/loader"),
                log_text.replace(":/usr/lib/", ":/private/unexplained/"),
                log_text.replace(install + "../../../../x86_64-pc-linux-gnu/bin/",
                                 install + "../../../../home/private/"),
                log_text.replace("/run/dbus/pid", "/run/dbus/../private"),
                log_text + "untyped runtime path: /run/dbus/pid\n",
                log_text + f"source: [{prefix}/../private]\n",
                log_text + "compiler: `/private/toolchain/gcc`\n",
        )):
            log.write_text(invalid_log)
            unsafe = copy.deepcopy(portable)
            unsafe["configuration"][2]["sha256"] = digest(log)
            validate.return_value = unsafe
            rejected(root / f"dbus-typed-log-unsafe-{index}.tar.gz", epoch, tools)
        log.write_text(log_text)
        for index, unknown_path in enumerate((
                "/run]", "/usr/lib:private", "/run/../private",
                "/private/toolchain", "/private/toolchain:private", "/private/toolchain/gcc",
        )):
            unsafe = copy.deepcopy(portable)
            unsafe["unexpected_path"] = unknown_path
            validate.return_value = unsafe
            rejected(root / f"dbus-literal-unsafe-{index}.tar.gz", epoch, tools)
        unsafe = copy.deepcopy(portable)
        unsafe["configuration"][1]["path"] = (
            configuration.parent.relative_to(prefix).as_posix() + "/meson-info/../config.h")
        unsafe["configuration"][1]["sha256"] = digest(configuration)
        validate.return_value = unsafe
        rejected(root / "dbus-bracketed-proof-traversal.tar.gz", epoch, tools)
        options.unlink()
        options.symlink_to(configuration)
        unsafe = copy.deepcopy(portable)
        unsafe["configuration"][1]["sha256"] = digest(configuration)
        validate.return_value = unsafe
        rejected(root / "dbus-bracketed-proof-symlink.tar.gz", epoch, tools)
        options.unlink()
        options.write_text(json.dumps(option_records) + "\n")
        # JSON source operands are literal paths, not bracketed prose defaults.
        targets = options.parent / "intro-targets.json"
        targets.write_text(json.dumps([{
            "name": "dbus-1", "type": "shared library",
            "target_sources": [{"sources": ["/usr/lib[private-build-root]"]}],
        }]) + "\n")
        unsafe = copy.deepcopy(portable)
        unsafe["configuration"].append({
            "path": targets.relative_to(prefix).as_posix(), "sha256": digest(targets),
        })
        invalid_proof_bom = copy.deepcopy(bracket_bom)
        invalid_proof_bom["components"][0]["provenance"]["configuration"] = copy.deepcopy(
            unsafe["configuration"])
        validate.return_value = unsafe
        rejected(root / "dbus-json-literal-source-path.tar.gz", epoch, tools,
                 runtime_manifest=invalid_proof_bom)
        # Real Meson target proof uses attached -I build-object/source paths.
        work = str(materials / ".dbus-build-fixture")
        build = work + "/build"
        source_root = work + "/source/dbus-fixture"
        target_records = [{
            "name": "dbus-1", "type": "shared library",
            "defined_in": source_root + "/dbus/meson.build",
            "filename": [build + "/dbus/libdbus-1.so.3.38.3"],
            "target_sources": [{
                "compiler": ["/usr/bin/gcc"],
                "parameters": ["-I" + build + "/dbus/libdbus-1.so.3.38.3.p",
                               "-I" + source_root + "/dbus", "-L" + build + "/dbus"],
                "sources": [source_root + "/dbus/dbus-address.c"],
            }],
        }]
        target_text = json.dumps(target_records, indent=3) + "\n"
        targets.write_text(target_text)
        target_actual = copy.deepcopy(portable)
        target_actual["provenance_roots"].update({
            "build": build, "source": source_root, "work": work,
        })
        target_actual["configuration"].append({
            "path": targets.relative_to(prefix).as_posix(), "sha256": digest(targets),
        })
        target_bom = copy.deepcopy(bracket_bom)
        target_bom["components"][0]["provenance"]["configuration"] = copy.deepcopy(
            target_actual["configuration"])
        validate.return_value = target_actual
        target_output = root / "dbus-json-attached-declared-roots.tar.gz"
        source_bundle.create(target_output, epoch, tools, runtime_manifest=target_bom)
        with tarfile.open(target_output) as archive:
            target_bytes = archive.extractfile(
                source_bundle.ROOT + "/dbus/configuration/meson-info/intro-targets.json").read()
            assert target_bytes.decode() == target_text.replace(
                source_root, "dbus-source").replace(build, "dbus-build").replace(
                    "/usr/bin/gcc", "tool/gcc")
            published_target = json.loads(target_bytes)[0]["target_sources"][0]
            assert published_target["parameters"] == [
                "-Idbus-build/dbus/libdbus-1.so.3.38.3.p",
                "-Idbus-source/dbus", "-Ldbus-build/dbus",
            ]
            target_metadata = json.loads(archive.extractfile(
                source_bundle.ROOT + "/dbus/dbus-source.json").read())
            target_proof = target_metadata["configuration"][-1]
            assert target_proof["original_sha256"] == digest(targets)
            assert target_proof["published_sha256"] == hashlib.sha256(target_bytes).hexdigest()
        for index, operand in enumerate((
                "-I" + work + "-private/build/dbus/libdbus-1.so.3.38.3.p",
                "-I/private/undeclared/build/dbus/libdbus-1.so.3.38.3.p",
                "-I" + build + "/../private",
                "-L" + work + "-private/build/dbus",
                "-I/usr/lib[private-build-root]",
                "-L/usr/lib:private-build-root",
                "-I/private/outer" + build + "/dbus",
        )):
            invalid_targets = copy.deepcopy(target_records)
            invalid_targets[0]["target_sources"][0]["parameters"][0] = operand
            targets.write_text(json.dumps(invalid_targets, indent=3) + "\n")
            unsafe = copy.deepcopy(target_actual)
            unsafe["configuration"][-1]["sha256"] = digest(targets)
            invalid_target_bom = copy.deepcopy(target_bom)
            invalid_target_bom["components"][0]["provenance"]["configuration"] = copy.deepcopy(
                unsafe["configuration"])
            validate.return_value = unsafe
            rejected(root / f"dbus-json-attached-unsafe-{index}.tar.gz", epoch, tools,
                     runtime_manifest=invalid_target_bom)
        for index, path in enumerate((
                "/private/unknown",
                "/private/toolchain-extra:/usr/bin:/bin",
                "/private/prefix/private/toolchain:/usr/bin:/bin",
                "/private/toolchain:/private/unknown:/bin",
                "/private/toolchain:/usr/bin:/private/unknown",
        )):
            unsafe = copy.deepcopy(actual)
            unsafe["build_environment"]["PATH"] = path
            validate.return_value = unsafe
            rejected(root / ("dbus-private-path-" + str(index) + ".tar.gz"), epoch, tools)
        validate.return_value = copy.deepcopy(actual)
        for key in ("source_sha256", "recipe_sha256", "lock_sha256", "original_sha256"):
            invalid = copy.deepcopy(bom)
            invalid["components"][0]["provenance"][key] = "c" * 64
            try:
                source_bundle.create(root / ("dbus-mismatch-" + key + ".tar.gz"), epoch, tools,
                                     runtime_manifest=invalid)
            except ValueError:
                pass
            else:
                raise AssertionError("Mismatched owned D-Bus source coverage was accepted")
        helper_bytes = helper.read_bytes()
        helper.write_bytes(b"tampered imported build helper")
        rejected(root / "dbus-tampered-helper.tar.gz", epoch, tools)
        helper.write_bytes(helper_bytes)
        configuration.write_bytes(b"tampered build configuration")
        rejected(root / "dbus-tampered-config.tar.gz", epoch, tools)
        configuration.write_bytes(b'#define UNKNOWN_PRIVATE_ROOT "/private/buildroot"\n')
        unsafe = copy.deepcopy(actual)
        unsafe["configuration"][0]["sha256"] = digest(configuration)
        validate.return_value = unsafe
        rejected(root / "dbus-private-config.tar.gz", epoch, tools)
    with patch.dict(os.environ, {}, clear=True):
        try:
            source_bundle.create(root / "dbus-missing-prefix.tar.gz", epoch, tools, runtime_manifest=bom)
        except ValueError:
            pass
        else:
            raise AssertionError("Owned D-Bus without its actual source prefix was accepted")


def dependency_contracts(root: Path, tools: Path, epoch: int) -> None:
    import dependency_sources

    cache = tools / "dependency-sources"
    cache.mkdir()
    materials = []
    for name, role, data in (("source.tar.xz", "source", b"source archive fixture"),
                             ("PKGBUILD", "recipe", b"exact distro build recipe fixture"),
                             ("distro.patch", "patch", b"required distro patch fixture")):
        path = cache / name
        path.write_bytes(data)
        materials.append({"path": name, "role": role, "size": len(data),
                          "sha256": hashlib.sha256(data).hexdigest(),
                          "url": "https://invalid.example/" + name})
    sourceinfo = (f"pkgbase = fixture\n\tpkgver = 1\n\tpkgrel = 1\n"
                  f"\tsource = https://invalid.example/source.tar.xz\n"
                  f"\tsource = distro.patch\n\tsha256sums = {materials[0]['sha256']}\n"
                  f"\tsha256sums = {materials[2]['sha256']}\n").encode()
    (cache / ".SRCINFO").write_bytes(sourceinfo)
    materials.append({"path": ".SRCINFO", "role": "recipe", "size": len(sourceinfo),
                      "sha256": hashlib.sha256(sourceinfo).hexdigest(),
                      "url": "https://invalid.example/.SRCINFO"})
    provenance = {
        "type": "distro", "manager": "pacman", "package": "fixture-lib",
        "version": "1-1", "source_package": "fixture", "source_version": "1-1",
        "distribution": {"id": "fixture", "version_id": "1"},
        "source_required": True, "license_expression": "LGPL-2.1-or-later",
        "pkgbuild_sha256": materials[1]["sha256"], "original_sha256": "a" * 64,
    }
    bom = {"components": [{"path": "lib/libfixture.so.1", "sha256": "b" * 64,
                           "size": 12, "provenance": provenance},
                          {"path": "lib/libpermissive.so.1", "sha256": "d" * 64,
                           "size": 10, "provenance": {
                               **provenance, "package": "unrelated-permissive",
                               "source_required": False, "license_expression": "MIT"}}]}
    record = {**provenance, "materials": materials,
              "authentication": {"pkgbuild_sha256": materials[1]["sha256"],
                                 "required_materials": [
                                     {key: item[key] for key in ("path", "sha256", "size", "role")}
                                     for item in materials],
                                 "recipe_sources": [
                                     {"declared_source": materials[index]["url"] if index == 0 else "distro.patch",
                                      "material_path": materials[index]["path"],
                                      **{key: materials[index][key] for key in ("sha256", "size", "role")}}
                                     for index in (0, 2)],
                                 "extra_required_patches": ["distro.patch"]},
              "instructions": "Use the supplied PKGBUILD in an isolated distro build environment."}
    inventory = cache / "provenance.json"
    inventory.write_text(json.dumps({"format": 1, "packages": [record]}))
    with patch.object(dependency_sources, "acquire", return_value=None):
        output = root / "dependency-source.tar.gz"
        source_bundle.create(output, epoch, tools, runtime_manifest=bom)
        with tarfile.open(output) as archive:
            proof = json.loads(archive.extractfile(
                source_bundle.ROOT + "/dependencies/provenance.json").read())
            covered = proof["packages"][0]["covered_components"]
            assert covered == [{"path": "lib/libfixture.so.1", "sha256": "b" * 64,
                                "original_sha256": "a" * 64}]
            for material in proof["packages"][0]["materials"]:
                assert archive.extractfile(source_bundle.ROOT + "/" + material["published_path"]).read() == (
                    cache / material["path"]).read_bytes()
        native_only = root / "dependency-native-only.tar.gz"
        source_bundle.create(native_only, epoch, tools, runtime_manifest=bom,
                             include_appimage_runtime=False)
        with tarfile.open(native_only) as archive:
            assert not any(member.name.startswith(source_bundle.ROOT + "/archives/")
                           or member.name.endswith("/runtime-sources.lock.json")
                           for member in archive.getmembers())
            assert archive.getmember(source_bundle.ROOT + "/dependencies/provenance.json").isfile()
        for index, mutate in enumerate((
                lambda item: item.update(version="different-version"),
                lambda item: item.update(source_package="wrong-source"),
                lambda item: item.update(materials=[]),
                lambda item: item["authentication"].update(pkgbuild_sha256="c" * 64),
                lambda item: item["materials"][0].update(path="../source.tar.xz"),
                lambda item: item["materials"][0].update(sha256="c" * 64),
                lambda item: item["materials"].pop(2),
                lambda item: item["authentication"]["recipe_sources"].pop(),
        )):
            invalid = copy.deepcopy(record)
            mutate(invalid)
            inventory.write_text(json.dumps({"format": 1, "packages": [invalid]}))
            destination = root / f"dependency-reject-{index}.tar.gz"
            try:
                source_bundle.create(destination, epoch, tools, runtime_manifest=bom)
            except ValueError:
                pass
            else:
                raise AssertionError("Missing/mismatched corresponding source was accepted")
            assert not destination.exists()
        inventory.write_text(json.dumps({"format": 1, "packages": []}))
        try:
            source_bundle.create(root / "dependency-missing.tar.gz", epoch, tools, runtime_manifest=bom)
        except ValueError:
            pass
        else:
            raise AssertionError("Absent corresponding source was accepted")
        for provenance_change in ({"source_required": False}, {"license_expression": None},
                                  {"pkgbuild_sha256": "c" * 64},
                                  {"original_sha256": None}):
            invalid_bom = copy.deepcopy(bom)
            invalid_bom["components"][0]["provenance"].update(provenance_change)
            inventory.write_text(json.dumps({"format": 1, "packages": [record]}))
            try:
                source_bundle.create(root / "dependency-invalid-bom.tar.gz",
                                     epoch, tools, runtime_manifest=invalid_bom)
            except ValueError:
                pass
            else:
                raise AssertionError("Uncertain or mismatched deployed source identity was accepted")


def main() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        materials = root / "materials"
        materials.mkdir()
        tools = root / "tools"
        cache = tools / "runtime-sources"
        cache.mkdir(parents=True)
        archives = {}
        sources = []
        for name in ("alpha", "beta"):
            archive_name = name + ".tar.gz"
            payload = (name + " source\n").encode()
            raw_tar = io.BytesIO()
            with tarfile.open(fileobj=raw_tar, mode="w") as archive:
                entry = tarfile.TarInfo(name + "/source.c")
                entry.size = len(payload)
                entry.mtime = 42
                archive.addfile(entry, io.BytesIO(payload))
            original = gzip.compress(raw_tar.getvalue(), mtime=42)
            archives[archive_name] = original
            (cache / archive_name).write_bytes(original)
            sources.append({
                "name": archive_name,
                "url": "https://invalid.example/" + archive_name,
                "sha256": hashlib.sha256(original).hexdigest(),
                "top_directory": name,
                "provenance": "Offline test fixture",
            })
        notice = materials / "fixture-NOTICE.txt"
        notice_bytes = b"Fixture redistribution notice\n"
        notice.write_bytes(notice_bytes)
        readme = materials / "runtime-SOURCE-README.txt"
        readme_bytes = b"Corresponding-source fixture instructions\n"
        readme.write_bytes(readme_bytes)
        lock = materials / "runtime-sources.lock.json"
        manifest = {
            "format": 1,
            "sources": sources,
            "notices": [{"path": notice.name,
                         "sha256": hashlib.sha256(notice_bytes).hexdigest()}],
        }
        lock.write_text(json.dumps(manifest, indent=2) + "\n")
        lock_bytes = lock.read_bytes()
        sentinel = b"PRIVATE ORIGINAL ASSETS MUST NOT SHIP"
        (tools / "original-assets").mkdir()
        (tools / "original-assets" / "sentinel.bin").write_bytes(sentinel)
        (materials / "unrelated.bin").write_bytes(sentinel)
        epoch = 1234567890
        with patch.object(source_bundle, "LOCK", lock), \
                patch.dict(os.environ, {}, clear=True), \
                patch.object(source_bundle, "README", readme), \
                patch.object(acquire.urllib.request, "urlopen",
                             side_effect=AssertionError("Unexpected network access")) as download:
            first = root / "first.tar.gz"
            assert source_bundle.create(first, epoch, tools) == first
            for path in [*(cache / name for name in archives), notice]:
                path.chmod(0o600)
                os.utime(path, (epoch + 100, epoch + 100))
            second = root / "second.tar.gz"
            assert source_bundle.create(second, epoch, tools) == second
            packed = first.read_bytes()
            assert packed == second.read_bytes(), "Input metadata changed output bytes"
            assert packed[:3] == b"\x1f\x8b\x08"
            assert not packed[3] & 0x08, "Gzip header contains a filename"
            assert int.from_bytes(packed[4:8], "little") == epoch
            prefix = "runtime-corresponding-source/"
            expected = {
                **{prefix + "archives/" + name: data for name, data in archives.items()},
                prefix + "notices/" + notice.name: notice_bytes,
                prefix + "runtime-sources.lock.json": lock_bytes,
                prefix + "README.txt": readme_bytes,
            }
            with tarfile.open(fileobj=io.BytesIO(packed), mode="r:gz") as archive:
                members = archive.getmembers()
                assert len(members) == len(expected)
                assert {entry.name for entry in members} == set(expected)
                for entry in members:
                    assert entry.isfile()
                    assert entry.mode == 0o644
                    assert entry.uid == entry.gid == 0
                    assert entry.uname == entry.gname == ""
                    assert entry.mtime == epoch
                    extracted = archive.extractfile(entry)
                    assert extracted is not None
                    assert extracted.read() == expected[entry.name]
            assert sentinel not in gzip.decompress(packed)
            for output in (first, root / "dangling.tar.gz"):
                if output != first:
                    output.symlink_to(root / "missing-target")
                try:
                    source_bundle.create(output, epoch, tools)
                except FileExistsError:
                    pass
                else:
                    raise AssertionError("Existing output was overwritten")
                assert not list(root.rglob(".source-bundle*"))
            assert first.read_bytes() == packed
            dangling = root / "dangling.tar.gz"
            assert dangling.is_symlink()
            assert os.readlink(dangling) == str(root / "missing-target")
            for index, bad_epoch in enumerate((True, False, -1, 4294967296, 1.5, "42", None)):
                rejected(root / f"epoch-{index}.tar.gz", bad_epoch, tools,
                         (ValueError, TypeError))
            archive_path = cache / sources[0]["name"]
            archive_path.write_bytes(b"tampered archive")
            rejected(root / "bad-archive.tar.gz", epoch, tools)
            archive_path.write_bytes(archives[archive_path.name])
            notice.write_bytes(b"tampered notice")
            rejected(root / "bad-notice.tar.gz", epoch, tools)
            notice.write_bytes(notice_bytes)
            for field, bad_names in (
                ("source", ("../escape.tar.gz", "/escape.tar.gz", "nested/archive.tar.gz")),
                ("notice", ("../escape.txt", "/escape.txt")),
            ):
                for index, bad_name in enumerate(bad_names):
                    unsafe = copy.deepcopy(manifest)
                    if field == "source":
                        unsafe["sources"][0]["name"] = bad_name
                    else:
                        unsafe["notices"][0]["path"] = bad_name
                    lock.write_text(json.dumps(unsafe))
                    rejected(root / f"unsafe-{field}-{index}.tar.gz", epoch, tools)
            lock.write_bytes(lock_bytes)
            dependency_contracts(root, tools, epoch)
            ffmpeg_contracts(root, tools, epoch)
            dbus_contracts(root, tools, epoch)
            download.assert_not_called()
        print("Corresponding-source bundle contracts passed")


if __name__ == "__main__":
    main()
