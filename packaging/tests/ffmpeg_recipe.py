#!/usr/bin/env python3
"""Offline recipe checks; never configure/build or run downloaded code.

python3 packaging/tests/ffmpeg_recipe.py --source-archive work/tools/ffmpeg-source/ffmpeg-8.1.3.tar.xz
"""
import argparse
import hashlib
import importlib.util
import io
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
spec = importlib.util.spec_from_file_location("build_ffmpeg", Path(__file__).resolve().parents[1] / "build_ffmpeg.py")
recipe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recipe)
ARCHIVE = None


def archive(path, entries):
    with tarfile.open(path, "w:xz") as output:
        for name, kind in entries:
            member = tarfile.TarInfo(name)
            member.type = kind
            if kind == tarfile.REGTYPE:
                member.size = 6
                output.addfile(member, io.BytesIO(b"source"))
            else:
                member.linkname = "outside"
                output.addfile(member)


def install_tree(root, lock):
    (root / "lib/pkgconfig").mkdir(parents=True)
    (root / "include").mkdir()
    for library in lock["libraries"]:
        base = "lib" + library["name"]
        full = base + ".so." + library["version"]
        (root / "lib" / full).write_bytes(b"test ELF input")
        for alias in (base + ".so", library["soname"]):
            (root / "lib" / alias).symlink_to(full)
        (root / "lib/pkgconfig" / (base + ".pc")).write_text("Version: " + library["version"] + "\n")


class RecipeSafety(unittest.TestCase):
    def test_process_group_exit_race_still_reaps_direct_child(self):
        for timeout in (False, True):
            with self.subTest(timeout=timeout):
                child = mock.Mock(pid=123, returncode=0)
                child.communicate.return_value = (b"", None)
                child.poll.return_value = None
                if timeout:
                    child.wait.side_effect = [recipe.subprocess.TimeoutExpired(["not-executed"], 10), None]
                with mock.patch.object(recipe.subprocess, "Popen", return_value=child), \
                        mock.patch.object(recipe.os, "killpg", side_effect=ProcessLookupError):
                    recipe.command(["not-executed"], Path("."))
                self.assertEqual(child.wait.call_args_list, [mock.call(timeout=10), mock.call()])

    def test_install_excludes_upstream_example_sources(self):
        lock = recipe.load_lock()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            install_tree(root, lock)
            (root / "share/ffmpeg/examples").mkdir(parents=True)
            (root / "share/ffmpeg/examples/decode_video.c").write_text("example source")
            with self.assertRaises(ValueError):
                recipe.check_install(root, lock)

    def test_source_extraction(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.tar.xz"
            archive(source, [("ffmpeg-test/configure", tarfile.REGTYPE)])
            extracted = recipe.extract_source(source, root / "unpacked", "ffmpeg-test")
            self.assertEqual((extracted / "configure").read_bytes(), b"source")

    def test_rejects_source_links_devices_escapes_and_duplicates(self):
        cases = [[("ffmpeg-test/../escape", tarfile.REGTYPE)],
                 [("/ffmpeg-test/file", tarfile.REGTYPE)],
                 [("different-root/file", tarfile.REGTYPE)],
                 [("ffmpeg-test/file", tarfile.SYMTYPE)],
                 [("ffmpeg-test/file", tarfile.LNKTYPE)],
                 [("ffmpeg-test/file", tarfile.CHRTYPE)],
                 [("ffmpeg-test/file", tarfile.FIFOTYPE)],
                 [("ffmpeg-test/file", tarfile.REGTYPE)] * 2]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for index, entries in enumerate(cases):
                with self.subTest(entries=entries):
                    source = root / f"bad-{index}.tar.xz"
                    archive(source, entries)
                    with self.assertRaises(ValueError):
                        recipe.extract_source(source, root / f"unpacked-{index}", "ffmpeg-test")
                    self.assertFalse((root / f"unpacked-{index}").exists())

    def test_cache_tampering_does_not_download_or_execute(self):
        lock = recipe.load_lock()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "ffmpeg-source").mkdir()
            (root / "ffmpeg-source" / lock["archive"]).write_bytes(b"tampered")
            with mock.patch.object(recipe, "command", side_effect=AssertionError("executed")), self.assertRaises(ValueError):
                recipe.acquire_source(root)

    def test_system_targets_and_symlinks_rejected(self):
        for path in ("/", "/usr/local/test", "/etc/test", "/proc/test", "/opt/test"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                recipe.local_directory(Path(path))
        with tempfile.TemporaryDirectory() as temporary:
            link = Path(temporary) / "link"
            link.symlink_to("/usr")
            with self.assertRaises(ValueError):
                recipe.local_directory(link / "local/test")

    def test_exact_libraries_versions_and_aliases(self):
        lock = recipe.load_lock()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            install_tree(root, lock)
            records = recipe.check_install(root, lock)
            self.assertEqual([row["soname"] for row in records], [row["soname"] for row in lock["libraries"]])
            extra = root / "lib/libavfilter.so.11"
            extra.write_bytes(b"unexpected")
            with self.assertRaises(ValueError):
                recipe.check_install(root, lock)
            extra.unlink()
            alias = root / "lib/libavcodec.so.62"
            alias.unlink()
            alias.symlink_to("/usr/lib/libavcodec.so.62")
            with self.assertRaises(ValueError):
                recipe.check_install(root, lock)

    def test_configuration_rejects_gpl_or_missing_library(self):
        values = {"GPL": 0, "NONFREE": 0, "VERSION3": 0, "NETWORK": 0,
                  "AVCODEC": 1, "AVFORMAT": 1, "AVUTIL": 1, "SWSCALE": 1,
                  "AVDEVICE": 0, "AVFILTER": 0, "SWRESAMPLE": 0}
        text = "".join(f"#define CONFIG_{name} {value}\n" for name, value in values.items())
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = root / "config.h"
            config.write_text(text)
            recipe.check_configuration(root)
            for flag in ("GPL", "NONFREE", "NETWORK", "AVFILTER", "VERSION3"):
                config.write_text(text.replace(f"CONFIG_{flag} 0", f"CONFIG_{flag} 1"))
                with self.subTest(flag=flag), self.assertRaises(ValueError):
                    recipe.check_configuration(root)

    def test_release_profile_is_finite_lgpl_baseline(self):
        lock = recipe.load_lock()
        flags = lock["configure_args"]
        for required in ("--disable-autodetect", "--disable-gpl", "--disable-nonfree", "--disable-network",
                         "--disable-everything", "--enable-x86asm", "--cpu=generic", "--enable-decoder=wmv3"):
            self.assertIn(required, flags)
        self.assertFalse(any("native" in flag or "--enable-parser" in flag for flag in flags))
        self.assertEqual([row["soname"] for row in lock["libraries"]],
                         ["libavcodec.so.62", "libavformat.so.62", "libavutil.so.60", "libswscale.so.9"])

    def test_authenticated_archive_license_hashes(self):
        if ARCHIVE is None:
            self.skipTest("Pass --source-archive to check byte-exact official source notices offline")
        lock = recipe.load_lock()
        self.assertFalse(ARCHIVE.is_symlink())
        self.assertEqual(recipe._sha256(ARCHIVE), lock["source_sha256"])
        with tempfile.TemporaryDirectory() as temporary:
            source = recipe.extract_source(ARCHIVE, Path(temporary), lock["top_directory"])
            notices = recipe.license_contents(source, lock)
            for row in lock["licenses"]:
                self.assertEqual(hashlib.sha256(notices[Path(row["path"]).name]).hexdigest(), row["sha256"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-archive", type=Path)
    options, remaining = parser.parse_known_args()
    ARCHIVE = options.source_archive
    unittest.main(argv=[sys.argv[0], *remaining])
