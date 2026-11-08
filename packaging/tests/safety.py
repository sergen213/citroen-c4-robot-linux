#!/usr/bin/env python3
"""Offline stdlib checks: python3 packaging/tests/safety.py (requires cmake and openssl)."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

spec = importlib.util.spec_from_file_location("package_native", Path(__file__).resolve().parents[1] / "package_native.py")
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)
from appimage import source_bundle
ELF_HEADER = b"\x7fELF\x02\x01" + b"\x00" * 12 + b"\x3e\x00"
RUNTIME_MANIFEST = "share/citroen-c4-robot/runtime-manifest.json"


def command(*args):
    try:
        return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as error:
        error.add_note("stdout:\n" + (error.stdout or b"").decode("utf-8", errors="replace")
                       + "\nstderr:\n" + (error.stderr or b"").decode("utf-8", errors="replace"))
        raise


def install_tree(root, with_runtime_manifest=False):
    paths = packager.ALLOWED_PATHS | ({RUNTIME_MANIFEST} if with_runtime_manifest else set())
    for name in sorted(paths):
        destination = root / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        if name == RUNTIME_MANIFEST:
            destination.write_text('{"components":[]}\n')
        else:
            destination.write_bytes(ELF_HEADER + b"fixture" if name in packager.BINARY_PATHS else b"project fixture\n")


class PackageSafety(unittest.TestCase):
    def test_versions(self):
        for value in ("0.1.0", "1.2.3", "1.2.3-alpha.1", "1.2.3-0", "1.2.3-01a",
                      "999999999.999999999.999999999-18446744073709551615", "1.2.3-" + "a" * 122):
            self.assertEqual(packager.validate_version(value), value)
        for value in ("v1.2.3", "1.2", "01.2.3", "1.2.3+build", "1.2.3-01", "1.2.3-", "1.2.3\n", "1.2.3-a..b",
                      "1000000000.1.0", "1.2.3-18446744073709551616", "1.2.3-" + "a" * 123):
            with self.subTest(value=value), self.assertRaises(ValueError):
                packager.validate_version(value)

    def test_allowlist_missing_links_special_and_size(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            install_tree(stage)
            self.assertEqual({item["path"] for item in packager.collect_files(stage)}, packager.ALLOWED_PATHS)
            previous_limit = packager.MAX_TOTAL
            packager.MAX_TOTAL = 1
            try:
                with self.assertRaises(ValueError):
                    packager.collect_files(stage)
            finally:
                packager.MAX_TOTAL = previous_limit
            unknown = stage / "share/citroen-c4-robot/original.bin"
            unknown.write_bytes(b"not redistributable")
            with self.assertRaises(ValueError):
                packager.collect_files(stage)
            unknown.unlink()
            binary = stage / "bin/c4-updater"
            binary.unlink()
            with self.assertRaises(ValueError):
                packager.collect_files(stage)
            binary.symlink_to("c4-launcher")
            with self.assertRaises(ValueError):
                packager.collect_files(stage)
            binary.unlink()
            os.mkfifo(binary)
            with self.assertRaises(ValueError):
                packager.collect_files(stage)
            binary.unlink()
            with binary.open("wb") as stream:
                stream.truncate(packager.MAX_FILE + 1)
            with self.assertRaises(ValueError):
                packager.collect_files(stage)
            binary.write_bytes(b"wrong architecture")
            with self.assertRaises(ValueError):
                packager.collect_files(stage)

    def test_exact_runtime_and_authored_shader_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            install_tree(stage)
            catalog = stage / "share/citroen-c4-robot/tools/original-assets.json"
            catalog_bytes = catalog.read_bytes()
            catalog.unlink()
            with self.assertRaises(ValueError):
                packager.collect_files(stage)
            catalog.write_bytes(catalog_bytes)
            with self.assertRaises(ValueError):
                packager.collect_files(stage, ["lib/liboriginal.dll.so.1"])
            runtime = stage / RUNTIME_MANIFEST
            runtime.parent.mkdir(parents=True, exist_ok=True)
            runtime.write_text("{}\n", encoding="utf-8")
            records = packager.collect_files(stage, [RUNTIME_MANIFEST])
            self.assertEqual({row["path"] for row in records},
                             packager.ALLOWED_PATHS | {RUNTIME_MANIFEST})
            names = sorted(packager.AUTHORED_SHADER_PATHS)
            for index, name in enumerate(names):
                target = stage / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b"\x03\x02\x23\x07")
                if index + 1 < len(names):
                    with self.assertRaises(ValueError):
                        packager.collect_files(stage, [RUNTIME_MANIFEST])
            with self.assertRaises(ValueError):
                packager.collect_files(stage, [RUNTIME_MANIFEST])
            loader_name = "lib/libvulkan.so.1"
            loader = stage / loader_name
            loader.parent.mkdir()
            loader.write_bytes(ELF_HEADER + b"fixture")
            runtime_paths = [RUNTIME_MANIFEST, loader_name]
            self.assertEqual({row["path"] for row in packager.collect_files(stage, runtime_paths)},
                             packager.ALLOWED_PATHS | packager.AUTHORED_SHADER_PATHS | set(runtime_paths))
            fullscreen = stage / "share/citroen-c4-robot/shaders/vulkan/fullscreen.vert.spv"
            fullscreen.unlink()
            with self.assertRaises(ValueError):
                packager.collect_files(stage, runtime_paths)

    # Signing/USTAR fixture deliberately substitutes deployment; runtime policy
    # and real dependency deployment are exercised separately by release smoke.
    @mock.patch.object(packager, "stage_runtime", return_value=[RUNTIME_MANIFEST])
    @mock.patch.dict(os.environ, {"PATH": os.environ.get("PATH") or os.defpath}, clear=True)
    def test_reproducible_signed_cmake_install(self, _deploy):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source_lock = root / "runtime-sources.lock.json"
            source_lock.write_text('{"format":1,"sources":[],"notices":[]}\n')
            source_readme = root / "source-README.txt"
            source_readme.write_text("Offline source packaging fixture\n")
            source_inputs = mock.patch.multiple(source_bundle, LOCK=source_lock, README=source_readme)
            source_inputs.start()
            self.addCleanup(source_inputs.stop)
            source.mkdir()
            install_tree(source, with_runtime_manifest=True)
            instructions = ["cmake_minimum_required(VERSION 3.20)", "project(PackageFixture LANGUAGES NONE)"]
            fixture_paths = packager.ALLOWED_PATHS | {RUNTIME_MANIFEST}
            for name in sorted(fixture_paths):
                instructions.append(f'install(FILES "{name}" DESTINATION "{Path(name).parent.as_posix()}")')
            (source / "CMakeLists.txt").write_text("\n".join(instructions) + "\n", encoding="utf-8")
            build = root / "build"
            command("cmake", "-S", str(source), "-B", str(build))
            key = root / "private.pem"
            command("openssl", "genpkey", "-algorithm", "ED25519", "-out", str(key))
            first, second = root / "first", root / "second"
            old_destdir = os.environ.get("DESTDIR")
            os.environ["DESTDIR"] = str(root / "must-not-be-used")
            try:
                packager.package(build, first, "1.2.3-alpha.1", key, 1234567890)
                packager.package(build, second, "1.2.3-alpha.1", key, 1234567890)
            finally:
                if old_destdir is None:
                    del os.environ["DESTDIR"]
                else:
                    os.environ["DESTDIR"] = old_destdir
            for name in (packager.PACKAGE_NAME, "manifest.json", "manifest.sig",
                         "runtime-corresponding-source.tar.gz",
                         "runtime-corresponding-source.tar.gz.sig"):
                self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())
            raw = (first / "manifest.json").read_bytes()
            manifest = json.loads(raw)
            self.assertEqual(manifest["version"], "1.2.3-alpha.1")
            self.assertEqual(manifest["package"]["sha256"], hashlib.sha256((first / packager.PACKAGE_NAME).read_bytes()).hexdigest())
            self.assertEqual((first / "manifest.sig").stat().st_size, 64)
            self.assertEqual((first / packager.PACKAGE_NAME).read_bytes()[4:8], bytes(4))
            with tarfile.open(first / packager.PACKAGE_NAME) as archive:
                members = archive.getmembers()
                self.assertEqual([item.name for item in members], sorted(fixture_paths))
                for member in members:
                    self.assertTrue(member.isfile())
                    self.assertEqual((member.uid, member.gid, member.mtime), (0, 0, 1234567890))
                    self.assertEqual(member.mode, 0o755 if member.name in packager.BINARY_PATHS else 0o644)
                    record = next(item for item in manifest["files"] if item["path"] == member.name)
                    content = archive.extractfile(member).read()
                    self.assertEqual((record["size"], record["sha256"]), (len(content), hashlib.sha256(content).hexdigest()))
            pub = root / "public.pem"
            command("openssl", "pkey", "-in", str(key), "-pubout", "-out", str(pub))
            command("openssl", "pkeyutl", "-verify", "-rawin", "-pubin", "-inkey", str(pub),
                    "-in", str(first / "manifest.json"), "-sigfile", str(first / "manifest.sig"))
            command("openssl", "pkeyutl", "-verify", "-rawin", "-pubin", "-inkey", str(pub),
                    "-in", str(first / "runtime-corresponding-source.tar.gz"),
                    "-sigfile", str(first / "runtime-corresponding-source.tar.gz.sig"))
            with tarfile.open(first / "runtime-corresponding-source.tar.gz") as archive:
                proof = json.loads(archive.extractfile(
                    source_bundle.ROOT + "/dependencies/provenance.json").read())
                self.assertEqual(proof["packages"], [])
            with self.assertRaises(FileExistsError):
                packager.package(build, first, "1.2.3", key, 0)
            rsa = root / "rsa.pem"
            command("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(rsa))
            with self.assertRaises(ValueError):
                packager.package(build, root / "bad-key", "1.2.3", rsa, 0)
            self.assertFalse((root / "bad-key").exists())
            previous_limit = packager.MAX_PACKAGE
            packager.MAX_PACKAGE = 1
            try:
                with self.assertRaises(ValueError):
                    packager.package(build, root / "oversize", "1.2.3", key, 0)
            finally:
                packager.MAX_PACKAGE = previous_limit
            self.assertFalse((root / "oversize").exists())
            unsigned = subprocess.run(["python3", str(Path(packager.__file__)), "--build-dir", str(build),
                                       "--output", str(root / "unsigned"), "--version", "1.2.3"],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(unsigned.returncode, 0)
            self.assertFalse((root / "unsigned").exists())


if __name__ == "__main__":
    unittest.main()
