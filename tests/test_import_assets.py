"""Run with python -m unittest discover -s tests -p test_import_assets.py."""
import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import import_assets
from import_assets import commit_new_directory, import_installer, parse_listing, validate_tree, verify_payload


class ImportSafetyTests(unittest.TestCase):
    def test_rejects_path_escape_and_duplicate_listing(self):
        for path in ("../outside", "/outside", "app/../outside", "C:/outside", "app\\outside"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                parse_listing(f"1 MD5 0 {path}\n")
        with self.assertRaises(ValueError):
            parse_listing("1 MD5 0 app/a\n1 MD5 0 app/a\n")

    def test_existing_install_and_original_preserved_without_running_extractor(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            installer = base / "original.exe"
            installer.write_bytes(b"untouched")
            output = base / "installed"
            output.mkdir()
            (output / "user-data").write_bytes(b"preserved")
            with self.assertRaises(FileExistsError):
                import_installer(installer, output, "/does/not/exist")
            self.assertEqual(installer.read_bytes(), b"untouched")
            self.assertEqual((output / "user-data").read_bytes(), b"preserved")

    def test_report_cannot_create_or_change_asset_destination(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            installer = base / "original.exe"
            installer.write_bytes(b"untouched")
            output = base / "new-output"
            with self.assertRaises(ValueError):
                import_installer(installer, output, "/does/not/exist", output / "report.json")
            self.assertFalse(output.exists())
            self.assertEqual(installer.read_bytes(), b"untouched")

    def test_symlink_cannot_read_or_change_outside_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            outside = base / "outside"
            outside.write_bytes(b"preserved")
            stage = base / "stage"
            stage.mkdir()
            (stage / "link").symlink_to(outside)
            with self.assertRaises(ValueError):
                validate_tree(stage)
            self.assertEqual(outside.read_bytes(), b"preserved")
            with self.assertRaises(ValueError):
                validate_tree(stage / "link")

    def test_completeness_detects_corruption_missing_and_extra_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp)
            (stage / "a").write_bytes(b"original")
            checksum = hashlib.md5(b"original", usedforsecurity=False).hexdigest()
            expected = parse_listing(f"8 MD5 {checksum} a\n")
            records = verify_payload(stage, expected)
            self.assertEqual(records[0]["sha256"], hashlib.sha256(b"original").hexdigest())
            (stage / "a").write_bytes(b"tampered")
            with self.assertRaises(ValueError):
                verify_payload(stage, expected)
            (stage / "a").unlink()
            with self.assertRaises(ValueError):
                verify_payload(stage, expected)
            (stage / "a").write_bytes(b"original")
            (stage / "extra").write_bytes(b"extra")
            with self.assertRaises(ValueError):
                verify_payload(stage, expected)

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux no-replace commit")
    def test_atomic_commit_preserves_even_empty_existing_destination(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            stage = base / "stage"
            stage.mkdir()
            (stage / "a").write_bytes(b"ready")
            target = base / "target"
            target.mkdir()
            with self.assertRaises(FileExistsError):
                commit_new_directory(stage, target)
            self.assertTrue(stage.is_dir())
            self.assertEqual(list(target.iterdir()), [])
            target.rmdir()
            commit_new_directory(stage, target)
            self.assertFalse(stage.exists())
            self.assertEqual((target / "a").read_bytes(), b"ready")

    @unittest.skipUnless(sys.platform.startswith("linux") and hasattr(os, "pidfd_open"),
                         "Linux native extraction and pidfd lifecycle observation")
    def test_sigterm_kills_and_reaps_running_extractor(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("Native C compiler required for extractor lifecycle fixture")
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            source = base / "extractor.c"
            extractor = base / "extractor"
            checksum = hashlib.md5(b"ready", usedforsecurity=False).hexdigest()
            source.write_text(r'''
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
    int extract = 0;
    const char *stage = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--data-version")) { puts("6.0.0"); return 0; }
        if (!strcmp(argv[i], "--list")) { puts("5 MD5 CHECKSUM payload"); return 0; }
        if (!strcmp(argv[i], "--extract")) extract = 1;
        if (!strcmp(argv[i], "--output-dir") && i + 1 < argc) stage = argv[++i];
    }
    if (!extract) return 0;
    if (!stage || chdir(stage)) return 2;
    FILE *partial = fopen("payload", "w");
    if (!partial) return 3;
    fputs("partial", partial);
    fclose(partial);
    /* Also cover bounded escalation for an uncooperative process tree. */
    if (getenv("C4_TEST_DESCENDANT")) {
        signal(SIGTERM, SIG_IGN);
        signal(SIGINT, SIG_IGN);
    }
    alarm(15);
    pid_t descendant = 0;
    if (getenv("C4_TEST_DESCENDANT")) {
        descendant = fork();
        if (descendant < 0) return 4;
        if (!descendant) { alarm(15); for (;;) pause(); }
    }
    FILE *ready = fopen(getenv("C4_TEST_PID_FILE"), "w");
    if (!ready) return 5;
    fprintf(ready, "%ld %ld\n", (long)getpid(), (long)descendant);
    fclose(ready);
    for (;;) pause();
}
'''.replace("CHECKSUM", checksum))
            subprocess.run([compiler, str(source), "-o", str(extractor)],
                           check=True, capture_output=True, timeout=15)
            tools = Path(__file__).resolve().parents[1] / "tools"
            for with_descendant in (False, True):
                with self.subTest(descendant=with_descendant):
                    case = base / str(with_descendant)
                    case.mkdir()
                    installer = case / "original.exe"
                    original = b"MZ locally owned Inno Setup Setup Data (6.0.0)"
                    installer.write_bytes(original)
                    current = case / "current"
                    current.mkdir()
                    (current / "user-data").write_bytes(b"preserved")
                    output = case / "new-output"
                    report = case / "report.json"
                    pid_file = case / "extractor.pid"
                    environment = os.environ.copy()
                    environment["C4_TEST_PID_FILE"] = str(pid_file)
                    environment.pop("C4_TEST_DESCENDANT", None)
                    if with_descendant:
                        environment["C4_TEST_DESCENDANT"] = "1"
                    process = subprocess.Popen(
                        [sys.executable, str(tools / "import_assets.py"),
                         "--installer", str(installer), "--output", str(output),
                         "--innoextract", str(extractor), "--report", str(report)],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment)
                    pids = []
                    pidfds = []
                    try:
                        deadline = time.monotonic() + 5
                        while time.monotonic() < deadline and process.poll() is None:
                            if pid_file.exists():
                                fields = pid_file.read_text().split()
                                if len(fields) == 2:
                                    pids = [int(value) for value in fields if int(value)]
                                    break
                            time.sleep(0.01)
                        self.assertTrue(pids, "Native extractor did not reach partial extraction")
                        for pid in pids:
                            pidfds.append(os.pidfd_open(pid))
                        stages = list(case.glob(".import-new-output-*"))
                        self.assertEqual(len(stages), 1)
                        self.assertEqual((stages[0] / "payload").read_bytes(), b"partial")
                        process.send_signal(signal.SIGTERM)
                        stdout, stderr = process.communicate(timeout=5)
                        self.assertEqual(process.returncode, 130, (stdout, stderr))
                        self.assertIn(b"Import interrupted", stderr)
                        for pidfd in pidfds:
                            self.assertEqual(select.select([pidfd], [], [], 1)[0], [pidfd],
                                             "Extractor or descendant is still running")
                        # The importer owns and must reap its direct extractor.
                        # A grandchild can be an exited zombie pending PID 1;
                        # pidfd readiness above observes exit, not PID ownership.
                        info = Path(f"/proc/self/fdinfo/{pidfds[0]}").read_text()
                        self.assertIn("Pid:\t-1\n", info, "Direct extractor was not reaped")
                        self.assertFalse(output.exists())
                        self.assertEqual(list(case.glob(".import-new-output-*")), [])
                        self.assertEqual(installer.read_bytes(), original)
                        self.assertEqual((current / "user-data").read_bytes(), b"preserved")
                        evidence = json.loads(report.read_text())
                        self.assertEqual(evidence["status"], "failed")
                        self.assertEqual(evidence["error"], "SIGTERM")
                        self.assertEqual(evidence["installer_sha256_before"],
                                         evidence["installer_sha256_after"])
                    finally:
                        for pidfd in pidfds:
                            try:
                                signal.pidfd_send_signal(pidfd, signal.SIGKILL)
                            except ProcessLookupError:
                                pass
                        if process.poll() is None:
                            process.kill()
                        process.communicate(timeout=5)
                        for pidfd in pidfds:
                            os.close(pidfd)


@unittest.skipUnless(sys.platform.startswith("linux"), "Linux descriptor walk and no-replace commit")
class DirectoryImportSafetyTests(unittest.TestCase):
    PAYLOAD = {"app/C4_Robot.exe": b"MZ fixture executable",
               "app/Data/payload.bin": b"fixture original asset"}

    def setUp(self):
        self.records = [{"path": path, "size": len(data),
                         "sha256": hashlib.sha256(data).hexdigest()}
                        for path, data in sorted(self.PAYLOAD.items())]
        catalog = mock.patch.object(import_assets, "original_asset_records",
                                    return_value=self.records)
        catalog.start()
        self.addCleanup(catalog.stop)

    def make_source(self, base, wrapped=False):
        source = base / "source"
        source.mkdir()
        for relative, data in self.PAYLOAD.items():
            path = source / (relative if wrapped else relative.removeprefix("app/"))
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (source / "Saves").mkdir()
        (source / "Saves/user.sav").write_bytes(b"keep my progress")
        (source / ".DS_Store").write_bytes(b"keep OS metadata")
        (source / "asset-manifest.json").write_bytes(b"not even a JSON manifest")
        current = base / "current"
        current.mkdir()
        (current / "user-data").write_bytes(b"previous working installation")
        return source

    def file_contents(self, source):
        return {path.relative_to(source).as_posix(): path.read_bytes()
                for path in source.rglob("*") if path.is_file()}

    def assert_no_publication(self, base):
        self.assertFalse(os.path.lexists(base / "new-output"))
        self.assertEqual(list(base.glob(".import-*")), [])
        self.assertEqual((base / "current/user-data").read_bytes(),
                         b"previous working installation")

    def test_raw_and_wrapped_directory_copy_only_catalog_and_preserve_user_files(self):
        for wrapped in (False, True):
            with self.subTest(wrapped=wrapped), tempfile.TemporaryDirectory() as tmp:
                base = Path(tmp)
                source = self.make_source(base, wrapped)
                before = self.file_contents(source)
                output = base / "new-output"
                report = base / "report.json"
                manifest = import_assets.import_directory(source, output, report)
                self.assertEqual(manifest, {
                    "schema_version": 2,
                    "source": {"kind": "directory", "path": str(source.absolute())},
                    "files": self.records})
                self.assertEqual(json.loads((output / "asset-manifest.json").read_text()), manifest)
                self.assertEqual(self.file_contents(output), {
                    **self.PAYLOAD, "asset-manifest.json": (output / "asset-manifest.json").read_bytes()})
                for relative, data in self.PAYLOAD.items():
                    copied = output / relative
                    original = source / (relative if wrapped else relative.removeprefix("app/"))
                    self.assertEqual(copied.read_bytes(), data)
                    self.assertNotEqual(copied.stat().st_ino, original.stat().st_ino)
                    self.assertEqual(copied.stat().st_nlink, 1)
                self.assertEqual(self.file_contents(source), before)
                self.assertEqual((base / "current/user-data").read_bytes(),
                                 b"previous working installation")
                self.assertEqual(list(base.glob(".import-*")), [])
                self.assertEqual(json.loads(report.read_text())["status"], "success")

    def test_missing_and_corrupt_assets_cannot_be_blessed_by_source_manifest(self):
        for wrapped in (False, True):
            for damage in ("missing", "same-size-corruption", "wrong-size"):
                with self.subTest(wrapped=wrapped, damage=damage), tempfile.TemporaryDirectory() as tmp:
                    base = Path(tmp)
                    source = self.make_source(base, wrapped)
                    damaged = source / ("app/Data/payload.bin" if wrapped else "Data/payload.bin")
                    if damage == "missing":
                        damaged.unlink()
                        forged_records = self.records[:1]
                    else:
                        data = (b"x" * len(self.PAYLOAD["app/Data/payload.bin"])
                                if damage == "same-size-corruption" else b"short")
                        damaged.write_bytes(data)
                        forged_records = [self.records[0], {
                            "path": "app/Data/payload.bin", "size": len(data),
                            "sha256": hashlib.sha256(data).hexdigest()}]
                    # Source metadata agrees with the damage; only the independent catalog is trusted.
                    (source / "asset-manifest.json").write_text(json.dumps({
                        "schema_version": 1, "installer_sha256": "0" * 64,
                        "files": forged_records}))
                    before = self.file_contents(source)
                    report = base / "report.json"
                    with self.assertRaises(ValueError):
                        import_assets.import_directory(source, base / "new-output", report)
                    self.assert_no_publication(base)
                    self.assertEqual(self.file_contents(source), before)
                    self.assertEqual(json.loads(report.read_text())["status"], "failed")

    def test_existing_destination_is_never_replaced(self):
        for existing in ("empty-directory", "working-directory", "file", "symlink"):
            with self.subTest(existing=existing), tempfile.TemporaryDirectory() as tmp:
                base = Path(tmp)
                source = self.make_source(base)
                before = self.file_contents(source)
                output = base / "new-output"
                if existing == "file":
                    output.write_bytes(b"existing file")
                elif existing == "symlink":
                    output.symlink_to(base / "current", target_is_directory=True)
                else:
                    output.mkdir()
                    if existing == "working-directory":
                        (output / "user-data").write_bytes(b"existing destination")
                with self.assertRaises(FileExistsError):
                    import_assets.import_directory(source, output)
                if existing == "file":
                    self.assertEqual(output.read_bytes(), b"existing file")
                elif existing == "symlink":
                    self.assertTrue(output.is_symlink())
                    self.assertEqual(output.readlink(), base / "current")
                elif existing == "empty-directory":
                    self.assertEqual(list(output.iterdir()), [])
                else:
                    self.assertEqual((output / "user-data").read_bytes(), b"existing destination")
                self.assertEqual(self.file_contents(source), before)
                self.assertEqual(list(base.glob(".import-*")), [])
                self.assertEqual((base / "current/user-data").read_bytes(),
                                 b"previous working installation")

    def test_links_hardlinks_and_special_files_even_in_excluded_extras_are_rejected(self):
        for wrapped in (False, True):
            for unsafe in ("file-symlink", "directory-symlink", "hardlink", "fifo", "socket"):
                with self.subTest(wrapped=wrapped, unsafe=unsafe), tempfile.TemporaryDirectory() as tmp:
                    base = Path(tmp)
                    source = self.make_source(base, wrapped)
                    outside = base / "outside"
                    outside.write_bytes(b"outside must stay unchanged")
                    extra = source / "Saves/unsafe"
                    listener = None
                    try:
                        if unsafe == "file-symlink":
                            extra.symlink_to(outside)
                        elif unsafe == "directory-symlink":
                            extra.symlink_to(base / "current", target_is_directory=True)
                        elif unsafe == "hardlink":
                            os.link(outside, extra)
                        elif unsafe == "fifo":
                            os.mkfifo(extra)
                        else:
                            listener = socket.socket(socket.AF_UNIX)
                            listener.bind(str(extra))
                        report = base / "uncreated-report-parent/report.json"
                        with self.assertRaises(ValueError):
                            import_assets.import_directory(source, base / "new-output", report)
                        self.assert_no_publication(base)
                        self.assertFalse(report.parent.exists())
                        self.assertTrue(os.path.lexists(extra))
                        self.assertEqual(outside.read_bytes(), b"outside must stay unchanged")
                        self.assertEqual((source / "Saves/user.sav").read_bytes(), b"keep my progress")
                        for relative, data in self.PAYLOAD.items():
                            original = source / (relative if wrapped else relative.removeprefix("app/"))
                            self.assertEqual(original.read_bytes(), data)
                    finally:
                        if listener is not None:
                            listener.close()

    def test_symlinked_source_root_and_ancestor_are_rejected_before_writes(self):
        for ancestor in (False, True):
            with self.subTest(ancestor=ancestor), tempfile.TemporaryDirectory() as tmp:
                base = Path(tmp)
                source = self.make_source(base)
                before = self.file_contents(source)
                alias = base / "alias"
                alias.symlink_to(base if ancestor else source, target_is_directory=True)
                selected = alias / "source" if ancestor else alias
                report = base / "uncreated-report-parent/report.json"
                with self.assertRaises(ValueError):
                    import_assets.import_directory(selected, base / "new-output", report)
                self.assert_no_publication(base)
                self.assertFalse(report.parent.exists())
                self.assertEqual(self.file_contents(source), before)
                self.assertTrue(alias.is_symlink())

    def test_source_output_report_overlaps_are_rejected_before_any_writes(self):
        cases = ("output-is-source", "output-in-source", "output-contains-source",
                 "report-is-source", "report-is-original", "report-in-source",
                 "report-is-output", "report-in-output", "report-contains-source",
                 "report-contains-output", "output-alias-in-source", "report-alias-in-source")
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as tmp:
                base = Path(tmp)
                source = self.make_source(base)
                before = self.file_contents(source)
                output = base / "new-output"
                report = base / "uncreated-report-parent/report.json"
                if case == "output-is-source":
                    output = source
                elif case == "output-in-source":
                    output = source / "uncreated-parent/output"
                elif case == "output-contains-source":
                    output = base
                elif case == "report-is-source":
                    report = source
                elif case == "report-is-original":
                    report = source / "C4_Robot.exe"
                elif case == "report-in-source":
                    report = source / "uncreated-parent/report.json"
                elif case == "report-is-output":
                    report = output
                elif case == "report-in-output":
                    report = output / "uncreated-parent/report.json"
                elif case == "report-contains-source":
                    report = base
                elif case == "report-contains-output":
                    output = base / "uncreated-parent/output"
                    report = output.parent
                else:
                    alias = base / "alias"
                    alias.symlink_to(source, target_is_directory=True)
                    if case == "output-alias-in-source":
                        output = alias / "uncreated-parent/output"
                    else:
                        report = alias / "uncreated-parent/report.json"
                entries_before = set(base.rglob("*"))
                with self.assertRaises(ValueError):
                    import_assets.import_directory(source, output, report)
                self.assertEqual(set(base.rglob("*")), entries_before)
                self.assertEqual(self.file_contents(source), before)
                self.assertEqual((base / "current/user-data").read_bytes(),
                                 b"previous working installation")

    def test_unrelated_symlinked_output_or_report_parent_is_rejected_without_writes(self):
        for target in ("output", "report"):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as tmp:
                base = Path(tmp)
                source = self.make_source(base)
                source_before = self.file_contents(source)
                outside = base / "outside"
                outside.mkdir()
                (outside / "user-data").write_bytes(b"unrelated user files")
                outside_before = self.file_contents(outside)
                alias = base / "alias"
                alias.symlink_to(outside, target_is_directory=True)
                output = (alias / "uncreated-parent/new-output"
                          if target == "output" else base / "new-output")
                report = alias / "uncreated-parent/report.json" if target == "report" else None
                with self.assertRaises(OSError):
                    import_assets.import_directory(source, output, report)
                self.assert_no_publication(base)
                self.assertFalse(os.path.lexists(output))
                self.assertEqual(self.file_contents(source), source_before)
                self.assertEqual(self.file_contents(outside), outside_before)
                self.assertEqual(set(outside.iterdir()), {outside / "user-data"})
                self.assertTrue(alias.is_symlink())

    def test_output_parent_swap_during_copy_cannot_publish_or_redirect_stage_cleanup(self):
        for replacement in ("new-directory", "source-symlink", "outside-symlink"):
            with self.subTest(replacement=replacement), tempfile.TemporaryDirectory() as tmp:
                base = Path(tmp)
                source = self.make_source(base)
                source_before = self.file_contents(source)
                outside = base / "outside"
                outside.mkdir()
                (outside / "user-data").write_bytes(b"unrelated user files")
                outside_before = self.file_contents(outside)
                parent = base / "destination-parent"
                parent.mkdir()
                (parent / "user-data").write_bytes(b"preserve destination parent's files")
                parent_before = self.file_contents(parent)
                moved = base / "moved-destination-parent"
                output = parent / "new-output"
                real_read = os.read
                swapped = False

                def swap_parent_on_read(descriptor, size):
                    nonlocal swapped
                    if not swapped:
                        swapped = True
                        parent.rename(moved)
                        if replacement == "new-directory":
                            parent.mkdir()
                        else:
                            parent.symlink_to(source if replacement == "source-symlink" else outside,
                                              target_is_directory=True)
                    return real_read(descriptor, size)

                expected_error = ValueError if replacement == "new-directory" else OSError
                with mock.patch.object(import_assets.os, "read", side_effect=swap_parent_on_read):
                    with self.assertRaises(expected_error):
                        import_assets.import_directory(source, output)
                self.assertTrue(swapped)
                self.assertFalse(os.path.lexists(output))
                self.assertEqual(self.file_contents(moved), parent_before)
                self.assertEqual(set(moved.iterdir()), {moved / "user-data"})
                self.assertEqual(self.file_contents(source), source_before)
                self.assertEqual(self.file_contents(outside), outside_before)
                self.assert_no_publication(base)
                if replacement == "new-directory":
                    self.assertEqual(list(parent.iterdir()), [])
                else:
                    self.assertTrue(parent.is_symlink())

    def test_interrupted_descriptor_copy_removes_private_stage_not_source_or_working_sibling(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            source = self.make_source(base)
            before = self.file_contents(source)
            real_read = os.read
            reads = 0
            stage = None

            def cancel_after_partial_read(descriptor, size):
                nonlocal reads, stage
                reads += 1
                if reads == 1:
                    return real_read(descriptor, min(size, 3))
                stages = list(base.glob(".import-*"))
                self.assertEqual(len(stages), 1)
                stage = stages[0]
                self.assertTrue((stage / "app/C4_Robot.exe").is_file())
                raise KeyboardInterrupt()

            report = base / "report.json"
            with mock.patch.object(import_assets.os, "read", side_effect=cancel_after_partial_read):
                with self.assertRaises(KeyboardInterrupt):
                    import_assets.import_directory(source, base / "new-output", report)
            self.assertEqual(reads, 2)
            self.assertIsNotNone(stage)
            self.assertFalse(stage.exists())
            self.assert_no_publication(base)
            self.assertEqual(self.file_contents(source), before)
            self.assertEqual(json.loads(report.read_text())["status"], "failed")


if __name__ == "__main__":
    unittest.main()
