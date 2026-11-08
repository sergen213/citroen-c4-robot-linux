#!/usr/bin/env python3
"""Offline D-Bus recipe contracts; never compile or execute upstream source."""
import argparse
import copy
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import re
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_dbus as recipe

ARCHIVE = None


def configuration(root, lock):
    (root / "meson-info").mkdir(parents=True)
    options = [{"name": key, "value": value} for key, value in lock["meson_options"].items()]
    (root / "meson-info/intro-buildoptions.json").write_text(json.dumps(options))
    (root / "config.h").write_text("".join(
        f"#define {key}\n" if value is True else
        f"#define {key} {json.dumps(value) if isinstance(value, str) else value}\n"
        for key, value in lock["config_defines"].items()))
    return {"meson_options": lock["meson_options"], "default_bus_addresses": lock["default_bus_addresses"]}


class DbusRecipeSafety(unittest.TestCase):
    def test_corresponding_recipe_rejects_changed_imported_helpers(self):
        with mock.patch.object(recipe, "_sha256", return_value="0" * 64), self.assertRaises(ValueError):
            recipe.load_lock()

    def test_missing_prefix_is_not_created_by_validation(self):
        with tempfile.TemporaryDirectory() as temporary:
            absent = Path(temporary) / "missing"
            with self.assertRaises((ValueError, FileNotFoundError)):
                recipe.check_install(absent, recipe.load_lock())
            self.assertFalse(absent.exists())
    def test_configuration_matches_authentic_meson_boolean_emission(self):
        # Upstream config.set(name, boolean) emits presence-only defines, while
        # DBUS_ENABLE_CHECKS uses checks ? 1 : false and must retain integer 1.
        lock = recipe.load_lock()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            metadata = configuration(root, lock)
            text = (root / "config.h").read_text()
            for key in ("DBUS_UNIX", "HAVE_UNIX_FD_PASSING", "DBUS_ENABLE_X11_AUTOLAUNCH"):
                text = re.sub(rf"^#define {key}(?: 1)?$", f"#define {key}", text, flags=re.M)
            (root / "config.h").write_text(text)
            recipe.check_configuration(root, metadata)
            self.assertIn("#define DBUS_ENABLE_CHECKS 1\n", text)
            for key in ("DBUS_UNIX", "HAVE_UNIX_FD_PASSING", "DBUS_ENABLE_X11_AUTOLAUNCH"):
                for bad in ("#undef " + key, "#define " + key + " 0", "#define " + key + " 2"):
                    (root / "config.h").write_text(text.replace("#define " + key + "\n", bad + "\n"))
                    with self.subTest(key=key, bad=bad), self.assertRaises(ValueError):
                        recipe.check_configuration(root, metadata)
            for value in ("", " 1"):
                (root / "config.h").write_text(text + "#define HAVE_SYSTEMD" + value + "\n")
                with self.subTest(systemd=value), self.assertRaises(ValueError):
                    recipe.check_configuration(root, metadata)


    def test_configuration_rejects_changed_bus_api_or_dependency_policy(self):
        lock = recipe.load_lock()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            metadata = configuration(root, lock)
            recipe.check_configuration(root, metadata)
            original = (root / "config.h").read_text()
            for key in ("HAVE_UNIX_FD_PASSING", "DBUS_UNIX", "DBUS_ENABLE_CHECKS"):
                (root / "config.h").write_text(re.sub(rf"^#define {key}(?: 1)?$", f"#define {key} 0", original, flags=re.M))
                with self.subTest(key=key), self.assertRaises(ValueError):
                    recipe.check_configuration(root, metadata)
            (root / "config.h").write_text(original)
            options = json.loads((root / "meson-info/intro-buildoptions.json").read_text())
            for key, bad in (("message_bus", True), ("default_library", "static"),
                             ("systemd", "enabled"), ("c_args", ["-march=native"])):
                changed = copy.deepcopy(options)
                next(row for row in changed if row["name"] == key)["value"] = bad
                (root / "meson-info/intro-buildoptions.json").write_text(json.dumps(changed))
                with self.subTest(key=key), self.assertRaises(ValueError):
                    recipe.check_configuration(root, metadata)

    def test_source_tampering_does_not_execute_or_download(self):
        lock = recipe.load_lock()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "dbus-source").mkdir()
            (root / "dbus-source" / lock["archive"]).write_bytes(b"tampered")
            with mock.patch.object(recipe, "command", side_effect=AssertionError("executed")), \
                    self.assertRaises(ValueError):
                recipe.acquire_source(root)

    def test_archive_rejects_links_devices_escapes_and_duplicates_before_writes(self):
        for name, kind, duplicate in (("../escape", tarfile.REGTYPE, False),
                                     ("dbus-1.16.2/link", tarfile.SYMTYPE, False),
                                     ("dbus-1.16.2/link", tarfile.LNKTYPE, False),
                                     ("dbus-1.16.2/dev", tarfile.CHRTYPE, False),
                                     ("dbus-1.16.2/file", tarfile.REGTYPE, True)):
            with self.subTest(name=name, kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                archive = root / "bad.tar.xz"
                with tarfile.open(archive, "w:xz") as output:
                    member = tarfile.TarInfo(name)
                    member.type = kind
                    member.size = 1 if kind == tarfile.REGTYPE else 0
                    member.linkname = "outside"
                    output.addfile(member, io.BytesIO(b"x") if member.size else None)
                    if duplicate:
                        output.addfile(member, io.BytesIO(b"x"))
                with self.assertRaises(ValueError):
                    recipe.extract_source(archive, root / "unpacked", "dbus-1.16.2")
                self.assertFalse((root / "unpacked").exists())

    def test_lock_preserves_shared_public_api_and_baseline(self):
        lock = recipe.load_lock()
        self.assertEqual(lock["libraries"], [{"name": "dbus-1", "soname": "libdbus-1.so.3", "version": "3.38.3"}])
        self.assertIn("dbus_threads_init_default", lock["required_symbols"])
        self.assertIn("dbus_connection_can_send_type", lock["required_symbols"])
        self.assertEqual(lock["needed_libraries"], ["libc.so.6"])
        self.assertEqual(lock["meson_options"]["c_args"], ["-march=x86-64", "-mtune=generic"])
        self.assertFalse(lock["meson_options"]["message_bus"])
        self.assertFalse(lock["meson_options"]["tools"])

    def test_install_rejects_wrong_abi_alias_or_daemon(self):
        lock = recipe.load_lock()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "lib/pkgconfig").mkdir(parents=True)
            (root / "include/dbus-1.0/dbus").mkdir(parents=True)
            for name in lock["public_headers"]:
                (root / "include/dbus-1.0/dbus" / name).write_bytes(b"public header")
            arch = root / "lib/dbus-1.0/include/dbus/dbus-arch-deps.h"
            arch.parent.mkdir(parents=True)
            arch.write_bytes(b"architecture header")
            full = "libdbus-1.so." + lock["libraries"][0]["version"]
            (root / "lib" / full).write_bytes(b"test ELF fixture unix:path=/run/dbus/system_bus_socket autolaunch:")
            for alias in ("libdbus-1.so", "libdbus-1.so.3"):
                (root / "lib" / alias).symlink_to(full)
            (root / "lib/pkgconfig/dbus-1.pc").write_text("Version: 1.16.2\n")
            dynamic = b"(SONAME) [libdbus-1.so.3]\n(NEEDED) [libc.so.6]\n"
            symbols = "".join("000000 T " + name + "@@LIBDBUS_1_3\n" for name in lock["required_symbols"]).encode()
            header = b"Class: ELF64\nMachine: Advanced Micro Devices X86-64\n"
            with mock.patch.object(recipe, "command", side_effect=[dynamic, symbols, header]):
                self.assertEqual(recipe.check_install(root, lock)[0]["soname"], "libdbus-1.so.3")
            for bad_dynamic, bad_symbols in ((dynamic.replace(b"so.3", b"so.4"), symbols),
                                             (dynamic + b"(NEEDED) [libsystemd.so.0]\n", symbols),
                                             (dynamic, symbols.replace(b"dbus_threads_init_default", b"removed_thread_API"))):
                with mock.patch.object(recipe, "command", side_effect=[bad_dynamic, bad_symbols, header]), self.assertRaises(ValueError):
                    recipe.check_install(root, lock)
            alias = root / "lib/libdbus-1.so.3"
            alias.unlink()
            alias.symlink_to("/usr/lib/libdbus-1.so.3")
            with self.assertRaises(ValueError):
                recipe.check_install(root, lock)
            alias.unlink()
            alias.symlink_to(full)
            (root / "bin").mkdir()
            (root / "bin/dbus-daemon").write_bytes(b"unexpected")
            with self.assertRaises(ValueError):
                recipe.check_install(root, lock)

    def test_official_source_notice_identity(self):
        if ARCHIVE is None:
            self.skipTest("Pass --source-archive for authentic complete source notice check")
        lock = recipe.load_lock()
        self.assertEqual(recipe._sha256(ARCHIVE), lock["source_sha256"])
        with tempfile.TemporaryDirectory() as temporary:
            source = recipe.extract_source(ARCHIVE, Path(temporary), lock["top_directory"])
            notices = recipe.license_contents(source, lock)
            for row in lock["licenses"]:
                self.assertEqual(hashlib.sha256(notices[row["path"]]).hexdigest(), row["sha256"])
            notice = source / "COPYING"
            notice.write_bytes(b"changed")
            with self.assertRaises(ValueError):
                recipe.license_contents(source, lock)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-archive", type=Path)
    args, remaining = parser.parse_known_args()
    ARCHIVE = args.source_archive
    unittest.main(argv=[sys.argv[0], *remaining])
