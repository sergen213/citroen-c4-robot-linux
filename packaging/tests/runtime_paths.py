#!/usr/bin/env python3
"""Run with python3 packaging/tests/runtime_paths.py; no third-party test runner."""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import re
import shutil
import sys
from tempfile import TemporaryDirectory
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import runtime_bundle
import dependency_sources
from runtime_bundle import (EXTENSION_MODULES, LIBRARY_BASES, PLUGIN_PATHS,
                            PYTHON_MINORS, allowed_runtime_path)


def dbus_dependency_selection():
    with TemporaryDirectory() as temporary:
        root = Path(temporary)
        owned = root / "owned/libdbus-1.so.3"
        host = root / "host/libdbus-1.so.3"
        unrelated = root / "host/libexpat.so.1"
        for path in (owned, host, unrelated):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"selection fixture")
        resolved = {"libdbus-1.so.3": host, "libexpat.so.1": unrelated}
        cache = {"libdbus-1.so.3": host}
        # ldd of both distro consumers resolves the host; exact owned SONAME wins.
        for consumer in ("libQt6DBus.so.6", "libpulse.so.0"):
            assert runtime_bundle._dependency_target("libdbus-1.so.3", resolved, cache, owned) == owned, consumer
        assert runtime_bundle._dependency_target("libdbus-1.so.3", resolved, cache, None) == host
        assert runtime_bundle._dependency_target("libdbus-1.so.3", {}, cache, owned) == owned
        assert runtime_bundle._dependency_target("libexpat.so.1", resolved, cache, owned) == unrelated
        assert runtime_bundle._dependency_target("libunknown.so.1", {}, cache, owned) is None
        owned_bom = {"components": [{
            "path": "lib/libdbus-1.so.3", "sha256": "a" * 64,
            "provenance": {"type": "upstream-source-build", "package": "D-Bus",
                           "license_expression": "GPL-2.0-or-later", "source_required": True}}]}
        assert dependency_sources.required_packages(owned_bom) == []


def dbus_prefix_boundaries(original):
    """Mutate real built ELF/source/config proof, never compile or execute libdbus."""
    import build_dbus

    original = original.resolve(strict=True)
    original_metadata = runtime_bundle.validate_dbus_prefix(original)
    lock = build_dbus.load_lock()
    cache = {}
    for row in runtime_bundle._run(["ldconfig", "-p"]).splitlines():
        match = re.match(r"\s*(\S+)\s+\(([^)]+)\)\s+=>\s+(\S+)", row)
        if match and "x86-64" in match[2]:
            cache.setdefault(match[1], Path(match[3]))
    owned = original / original_metadata["built_libraries"][0]["path"]
    for consumer_name in ("libQt6DBus.so.6", "libpulse.so.0"):
        # The trusted host cache names SONAME aliases; inspect their actual ELF target.
        consumer = cache[consumer_name].resolve(strict=True)
        assert "libdbus-1.so.3" in runtime_bundle._elf(consumer)[0]
        resolved = {}
        for row in runtime_bundle._run(["ldd", str(consumer)]).splitlines():
            match = re.match(r"\s*(\S+)\s+=>\s+(/\S+)", row)
            if match:
                resolved[match[1]] = Path(match[2])
        assert "libdbus-1.so.3" in resolved
        assert runtime_bundle._dependency_target("libdbus-1.so.3", resolved, cache, owned) == owned
        assert runtime_bundle._dependency_target("libdbus-1.so.3", resolved, cache, None) == resolved["libdbus-1.so.3"]
    metadata_path = "share/citroen-c4-robot/dbus-source.json"
    with TemporaryDirectory() as temporary:
        root = Path(temporary)

        def clone(index):
            prefix = root / str(index)
            shutil.copytree(original, prefix, symlinks=True)
            metadata = json.loads((prefix / metadata_path).read_text().replace(str(original), str(prefix)))
            for item in metadata["configuration"]:
                path = prefix / item["path"]
                path.write_bytes(path.read_bytes().replace(str(original).encode(), str(prefix).encode()))
                item["sha256"] = runtime_bundle._digest(path)
            (prefix / metadata_path).write_text(json.dumps(metadata), encoding="utf-8")
            runtime_bundle.validate_dbus_prefix(prefix)
            return prefix, metadata

        def reject(index, mutate):
            prefix, metadata = clone(index)
            mutate(prefix, metadata)
            (prefix / metadata_path).write_text(json.dumps(metadata), encoding="utf-8")
            try:
                runtime_bundle.validate_dbus_prefix(prefix)
            except (ValueError, FileNotFoundError):
                return
            raise AssertionError(f"owned D-Bus trust boundary accepted mutation: {index}")

        for key in ("prefix", "lock_sha256", "recipe_sha256"):
            reject(key, lambda prefix, metadata, key=key: metadata.update({key: "changed"}))
        reject("proof", lambda prefix, metadata: metadata.update(proof={}))
        reject("options", lambda prefix, metadata: metadata["meson_args"].append("-Dsystemd=enabled"))
        reject("needed", lambda prefix, metadata: metadata.update(needed_libraries=["libunknown.so.1"]))
        reject("addresses", lambda prefix, metadata: metadata.update(default_bus_addresses={}))
        reject("library-record", lambda prefix, metadata: metadata["built_libraries"].append(
            dict(metadata["built_libraries"][0])))
        reject("configuration-inventory", lambda prefix, metadata: metadata["configuration"].pop())

        def source_changed(prefix, metadata):
            archive = prefix / lock["archive"]
            shutil.copyfile(original_metadata["source_cache_path"], archive)
            metadata["source_cache_path"] = str(archive)
            archive.write_bytes(b"changed corresponding source")

        reject("source", source_changed)
        reject("notice", lambda prefix, metadata:
               (prefix / "share/licenses/D-Bus" / lock["licenses"][0]["path"]).write_bytes(b"changed notice"))
        reject("configuration-hash", lambda prefix, metadata:
               (prefix / metadata["configuration"][0]["path"]).write_bytes(b"changed configuration"))
        for filename in ("dbus.lock.json", "build_dbus.py", *(
                item["path"] for item in lock["provenance"]["materials"])):
            reject("proof-" + filename, lambda prefix, metadata, filename=filename:
                   (prefix / "share/licenses/D-Bus/source-proof" / filename).write_bytes(b"changed proof"))

        def config_policy_changed(prefix, metadata):
            item = next(row for row in metadata["configuration"] if row["path"].endswith("/config.h"))
            path = prefix / item["path"]
            text = path.read_text()
            address = lock["default_bus_addresses"]["system"]
            assert address in text
            path.write_text(text.replace(address, "unix:path=/tmp/untrusted-bus"), encoding="utf-8")
            item["sha256"] = runtime_bundle._digest(path)

        reject("configuration-policy-with-refreshed-hash", config_policy_changed)

        def introspection_changed(prefix, metadata, key, value):
            item = next(row for row in metadata["configuration"]
                        if row["path"].endswith("/intro-buildoptions.json"))
            path = prefix / item["path"]
            rows = json.loads(path.read_text())
            row = next(row for row in rows if row["name"] == key)
            row["value"] = value
            path.write_text(json.dumps(rows), encoding="utf-8")
            item["sha256"] = runtime_bundle._digest(path)

        reject("actual-systemd-option", lambda prefix, metadata:
               introspection_changed(prefix, metadata, "systemd", "enabled"))
        reject("actual-prefix-option", lambda prefix, metadata:
               introspection_changed(prefix, metadata, "prefix", "/tmp/untrusted-prefix"))

        def escaped_configuration(prefix, metadata):
            item = metadata["configuration"][0]
            path = prefix / item["path"]
            path.unlink()
            path.symlink_to(original / item["path"])

        reject("configuration-symlink", escaped_configuration)

        def binary_changed(prefix, metadata, old, new, update_hash):
            record = metadata["built_libraries"][0]
            path = prefix / record["path"]
            data = path.read_bytes()
            assert old in data and len(old) == len(new)
            path.write_bytes(data.replace(old, new))
            if update_hash:
                record["sha256"] = runtime_bundle._digest(path)

        reject("binary-hash", lambda prefix, metadata:
               binary_changed(prefix, metadata, b"dbus_bus_get_private", b"dbus_bus_get_privatX", False))
        for label, old, new in (
                ("required-symbol", b"dbus_bus_get_private", b"dbus_bus_get_privatX"),
                ("symbol-version", b"LIBDBUS_1_3", b"LIBDBUS_1_4"),
                ("soname", b"libdbus-1.so.3\0", b"libdbus-1.so.4\0"),
                ("needed-binary", b"libc.so.6\0", b"libx.so.6\0")):
            reject(label, lambda prefix, metadata, old=old, new=new:
                   binary_changed(prefix, metadata, old, new, True))
        address = lock["default_bus_addresses"]["system"].encode()
        reject("binary-address", lambda prefix, metadata:
               binary_changed(prefix, metadata, address, b"X" + address[1:], True))

        def escaped_alias(prefix, metadata):
            link = prefix / "lib/libdbus-1.so.3"
            link.unlink()
            link.symlink_to(original / original_metadata["built_libraries"][0]["path"])

        reject("alias-escape", escaped_alias)
        for prefix in (Path(""), Path("relative")):
            try:
                runtime_bundle.validate_dbus_prefix(prefix)
            except ValueError:
                pass
            else:
                raise AssertionError("non-absolute/empty D-Bus prefix accepted")


def bsd_component_notices():
    """Only complete pinned primary notices resolve the two raw BSD declarations."""
    ogg = b"""Copyright (c) 2002, Xiph.org Foundation

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of the Xiph.org Foundation nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""
    bzip2 = b"""
--------------------------------------------------------------------------

This program, "bzip2", the associated library "libbzip2", and all
documentation, are copyright (C) 1996-2019 Julian R Seward.  All
rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. The origin of this software must not be misrepresented; you must\x20
   not claim that you wrote the original software.  If you use this\x20
   software in a product, an acknowledgment in the product\x20
   documentation would be appreciated but is not required.

3. Altered source versions must be plainly marked as such, and must
   not be misrepresented as being the original software.

4. The name of the author may not be used to endorse or promote\x20
   products derived from this software without specific prior written\x20
   permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Julian Seward, jseward@acm.org
bzip2/libbzip2 version 1.0.8 of 13 July 2019

--------------------------------------------------------------------------
"""
    cases = (
        ("libogg", "libogg.so.0.8.6", ogg, "BSD-3-Clause",
         "d2ab5758336489da61c12cc5bb757da5339c4ae9001f9bb0562b4370249af814",
         "https://raw.githubusercontent.com/xiph/ogg/v1.3.6/COPYING", "COPYING"),
        ("bzip2", "libbz2.so.1.0.8", bzip2, "bzip2-1.0.6",
         "c6dbbf828498be844a89eaa3b84adbab3199e342eb5cb2ed2f0d4ba7ec0f38a3",
         "https://sourceware.org/git/?p=bzip2.git;a=blob_plain;f=LICENSE;hb=bzip2-1.0.8", "LICENSE"),
    )
    for package, library, data, expected, checksum, origin, filename in cases:
        assert hashlib.sha256(data).hexdigest() == checksum
        records = [{"source": f"/usr/share/licenses/{package}/{filename}", "sha256": checksum}]
        records_before = copy.deepcopy(records)
        evidence = {}
        terms = runtime_bundle._component_terms(
            Path("/usr/lib") / library, package, "BSD", data.decode(), records, evidence)
        assert terms == expected
        assert runtime_bundle._source_required(terms) is False
        assert records == records_before
        assert evidence == {
            "notices": records,
            "primary_notice": {**records[0], "origin": origin, "license_expression": expected},
        }
        # Authenticated BSD notice evidence must not rewrite an unrelated declared route.
        untouched = {}
        unaltered = runtime_bundle._component_terms(
            Path(library), package, "LGPL-2.1-or-later", data.decode(), records, untouched)
        assert unaltered == "LGPL-2.1-or-later"
        assert untouched == {"notices": records}
        assert runtime_bundle._source_required("LGPL-2.1-or-later") is True
        rejected = (
            (package, library, ()),
            (package, library, [{"source": records[0]["source"],
                                 "sha256": hashlib.sha256(data + b"changed").hexdigest()}]),
            (package, library, [{"source": "/usr/share/licenses/spdx/BSD-3-Clause.txt",
                                 "sha256": hashlib.sha256(b"Redistribution and use").hexdigest()}]),
            ("unrelated-package", library, records),
            ("bzip2" if package == "libogg" else "libogg", library, records),
            (package, "libunrelated.so.1", records),
            (package, library + ".debug", records),
            (package, library.split(".so.")[0] + ".so", records),
            (package, library, [{"source": records[0]["source"], "sha256": cases[1 if package == "libogg" else 0][4]}]),
        )
        for source_package, name, notices in rejected:
            evidence = {}
            try:
                terms = runtime_bundle._component_terms(
                    Path(name), source_package, "BSD", data.decode(), notices, evidence)
                runtime_bundle._source_required(terms)
            except ValueError:
                assert "primary_notice" not in evidence
            else:
                raise AssertionError(f"unbound BSD component accepted: {source_package}/{name}: {notices}")
    try:
        runtime_bundle._source_required("BSD")
    except ValueError:
        pass
    else:
        raise AssertionError("generic BSD declaration accepted")


def versioned_component_notices():
    """Operator-verified whole-notice receipts only resolve their exact release scope."""
    icu_raw = "(LicenseRef-Unicode-3.0) AND (BSD-2-Clause) AND (BSD-3-Clause) AND (NAIST-2003)"
    icu_terms = "Unicode-3.0 AND BSD-2-Clause AND BSD-3-Clause AND NAIST-2003"
    cases = (
        ("icu", "78.3-1.1", ("libicudata.so.78.3", "libicuuc.so.78.3", "libicui18n.so.78.3"),
         icu_raw, icu_terms, "/usr/share/licenses/icu/LICENSE",
         "e55522d81edc687a341a4411e0776e54ca654e90147f354a90458aaced4116af",
         "https://raw.githubusercontent.com/unicode-org/icu/release-78.3/LICENSE"),
        ("libxext", "1.3.7-1.1", ("libXext.so.6.4.0",),
         "LicenseRef-libxext", "LicenseRef-libXext-1.3.7", "/usr/share/licenses/libxext/COPYING",
         "fd62910be4b13829d94e76c1447cf840953f0e225c4dc6c79349c84dd0557f22",
         "https://gitlab.freedesktop.org/xorg/lib/libxext/-/raw/libXext-1.3.7/COPYING"),
    )
    for index, (package, version, libraries, raw, expected, filename, checksum, origin) in enumerate(cases):
        # Hashes bind authentic installed/upstream bytes; aggregation computes them,
        # while these classifier tests deliberately do not duplicate the large notices.
        records = [{"source": filename, "sha256": checksum}]
        records_before = copy.deepcopy(records)
        for library in libraries:
            evidence = {}
            terms = runtime_bundle._component_terms(
                Path("/usr/lib") / library, package, raw, "", records, evidence,
                source_version=version)
            assert terms == expected
            assert runtime_bundle._source_required(terms) is False
            assert records == records_before
            assert evidence == {
                "notices": records,
                "primary_notice": {
                    **records[0], "origin": origin, "license_expression": expected,
                    "source_package": package, "source_version": version, "library": library,
                },
            }
            assert runtime_bundle._source_required(f"({terms}) AND GPL-2.0-only") is True
            assert runtime_bundle._source_required(f"GPL-2.0-only OR ({terms})") is False
            canonical = f"({terms}) AND GPL-2.0-only"
            untouched = {}
            assert runtime_bundle._component_terms(
                Path(library), package, canonical, "", records, untouched,
                source_version=version) == canonical
            assert untouched == {"notices": records}
            for declaration in (raw + " AND GPL-2.0-only", f"({raw}) OR GPL-2.0-only"):
                untouched = {}
                result = runtime_bundle._component_terms(
                    Path(library), package, declaration, "", records, untouched,
                    source_version=version)
                assert result == declaration
                assert untouched == {"notices": records}
                try:
                    runtime_bundle._source_required(result)
                except ValueError:
                    pass
                else:
                    raise AssertionError("extra raw opaque terms bypassed the strict parser")
        library = libraries[0]
        missing_version = {}
        try:
            runtime_bundle._component_terms(Path(library), package, raw, "", records, missing_version)
        except ValueError as error:
            assert "source version" in str(error)
            assert "primary_notice" not in missing_version
        else:
            raise AssertionError("omitted source version accepted")
        rejected = (
            (package, version, library, (), "pinned complete"),
            (package, version, library, [{"source": filename, "sha256": "0" * 64}], "pinned complete"),
            (package, version, library, [{"source": filename, "sha256": cases[1 - index][6]}], "pinned complete"),
            (package, None, library, records, "source version"),
            (package, "", library, records, "source version"),
            (package, version + ".changed", library, records, "source version"),
            (package, "79.1-1.1" if package == "icu" else "1.3.8-1.1", library, records, "source version"),
            ("unrelated-package", version, library, records, None),
            (cases[1 - index][0], version, library, records, None),
            (package, version, "libunrelated.so.1", records, None),
            (package, version, "libicutu.so.78.3" if package == "icu" else "libXext.so.6.5.0", records, None),
            (package, version, library + ".debug", records, None),
            (package, version, library.split(".so.")[0] + ".so", records, None),
            (package, version, "libicudata.so.79.1" if package == "icu" else "libXext.so.7.4.0", records, None),
        )
        for source_package, source_version, name, notices, error_fragment in rejected:
            evidence = {}
            try:
                terms = runtime_bundle._component_terms(
                    Path(name), source_package, raw, "", notices, evidence,
                    source_version=source_version)
                assert error_fragment is None, "bound contract mismatch did not fail explicitly"
                assert terms == raw
                runtime_bundle._source_required(terms)
            except ValueError as error:
                if error_fragment:
                    assert error_fragment in str(error)
                assert "primary_notice" not in evidence
            else:
                raise AssertionError(f"unbound versioned notice accepted: {source_package}/{name}")
        assert records == records_before
    assert icu_terms.split(" AND ") == ["Unicode-3.0", "BSD-2-Clause", "BSD-3-Clause", "NAIST-2003"]
    for raw in (icu_raw, "LicenseRef-Unicode-3.0", "LicenseRef-libxext"):
        assert runtime_bundle._license_expression(raw) == raw
        try:
            runtime_bundle._source_required(raw)
        except ValueError:
            pass
        else:
            raise AssertionError(f"raw opaque declaration accepted globally: {raw}")


def kerberos_component_notices():
    """The complete NOTICE and all three authentic supplemental files are mandatory."""
    version = "1.22.2-1.1"
    expected = "LicenseRef-Kerberos-1.22.2-LibraryNotices"
    commit = "8570e77819563e036027e1da789d08ec9333ed4d"
    origin = f"https://raw.githubusercontent.com/krb5/krb5/{commit}/"
    materials = (
        ("NOTICE", "d37fcb886de2ad1c602f61c99c6893bcbd9a1949f54d9a6bb40d191204eb605e",
         "complete unchanged release notice; conditional grants retained conservatively"),
        ("src/include/k5-queue.h", "6074ee76bdde96dfcf2a57134676baf7ae37fac7644ff349d3996b748a82d0bb",
         "actual UCB queue-header attribution incorporated by library support hashtab"),
        ("src/include/copyright.h", "435ef19645770fbb3e9cdd2bead820307768ce1981e5d164622f6faca5333f2d",
         "complete legacy MIT permission and retained conditions"),
        ("src/lib/gssapi/LICENSE", "c4ab82b506f53b97e7bf7ee2b71152da792e0d5578da4089ed17b4dd8798aedf",
         "complete Sun GSSAPI permission and provenance"),
    )
    records = [{"source": "/usr/share/licenses/krb5/LICENSE", "sha256": materials[0][1]}]
    records += [{"source": f"dependency-sources/notices/krb5-1.22.2/{path}", "sha256": checksum}
                for path, checksum, _ in materials[1:]]
    original = copy.deepcopy(records)
    libraries = ("libgssapi_krb5.so.2.2", "libkrb5.so.3.3", "libk5crypto.so.3.1", "libkrb5support.so.0.1")
    for library in libraries:
        evidence = {}
        terms = runtime_bundle._component_terms(
            Path(library), "krb5", "custom", "", records, evidence, source_version=version)
        assert terms == expected
        assert runtime_bundle._source_required(terms) is False
        mandatory = [
            {**record, "origin": origin + path, "scope": scope, "license_expression": expected,
             "source_package": "krb5", "source_version": version, "library": library}
            for record, (path, _, scope) in zip(records, materials)
        ]
        assert evidence == {"notices": records, "primary_notice": mandatory[0],
                            "primary_notices": mandatory}
        assert records == original
        canonical = f"({terms}) AND GPL-2.0-only"
        assert runtime_bundle._source_required(canonical) is True
        assert runtime_bundle._source_required(f"GPL-2.0-only OR ({terms})") is False
        untouched = {}
        assert runtime_bundle._component_terms(
            Path(library), "krb5", canonical, "", records, untouched,
            source_version=version) == canonical
        assert untouched == {"notices": records}
        for index in range(len(records)):
            for replacement in (None, {"source": records[index]["source"], "sha256": "0" * 64},
                                {**records[index], "sha256": materials[(index + 1) % len(materials)][1]}):
                incomplete = copy.deepcopy(records)
                if replacement is None:
                    incomplete.pop(index)
                else:
                    incomplete[index] = replacement
                rejected = {}
                try:
                    runtime_bundle._component_terms(
                        Path(library), "krb5", "custom", "", incomplete, rejected, source_version=version)
                except ValueError:
                    assert "primary_notice" not in rejected
                    assert "primary_notices" not in rejected
                else:
                    raise AssertionError(f"incomplete Kerberos material accepted: {library}/{index}")
    rejected_scopes = [
        ("krb5", candidate, libraries[0], "custom")
        for candidate in (None, "", "1.22.2-1", "1.22.3-1.1")
    ] + [
        ("krb5", version, candidate, "custom")
        for candidate in ("libkrb5.so.3", "libkrb5.so.3.4", "libkadm5clnt_mit.so.12.0",
                          "libkadm5srv_mit.so.12.0", "libgssrpc.so.4.2", "libkdb5.so.10.0",
                          "kadmin", "libunrelated.so.1", libraries[0] + ".debug")
    ] + [
        ("unrelated-package", version, libraries[0], "custom"),
        ("e2fsprogs", version, "libcom_err.so.2.1", "custom"),
        ("krb5", version, libraries[0], "custom AND GPL-2.0-only"),
        ("krb5", version, libraries[0], "custom OR GPL-2.0-only"),
    ]
    for package, source_version, library, declaration in rejected_scopes:
        evidence = {}
        try:
            terms = runtime_bundle._component_terms(
                Path(library), package, declaration, "", records, evidence, source_version=source_version)
            assert terms == declaration
            runtime_bundle._source_required(terms)
        except ValueError:
            assert "primary_notice" not in evidence
            assert "primary_notices" not in evidence
        else:
            raise AssertionError(f"unbound Kerberos scope accepted: {package}/{source_version}/{library}")
    assert records == original
    try:
        runtime_bundle._source_required("custom")
    except ValueError:
        pass
    else:
        raise AssertionError("raw custom accepted globally")


def libarchive_component_notices():
    """Exact notices require the authenticated recipe and selected binary member."""
    version = "3.8.10-1.1"
    library = "libarchive.so.13.8.10"
    source = Path("/usr/lib") / library
    raw = "(BSD-2-Clause) AND (BSD-4-Clause-UC) AND (Apache-2.0 OR CC0-1.0 OR OpenSSL)"
    expected = "LicenseRef-libarchive-3.8.10-LibraryNotices-CC0"
    recipe = "f97910d09d390608ef7177d48f39fdb1e0f340b265c00f557d1d9cb076c8019d"
    elf = "08960c60b44bb280e84eb35c54350829cd69bd27190b844da6a54ffa227583f0"
    mtree = "750920fa31a04b7c24fbbe845a500075f879faacb0b42e5f37153b593ca68447"
    commit = "0c48412be2d94afe1859a807ea7896a18d8900bd"
    materials = (
        ("COPYING", "30e556b3959e3985d66efefec5eaac51d4995053caa1d3cffe6eb916f146f229"),
        ("cc0-1.0.txt", "a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499"),
        ("library-notices.txt", "34c7c75c7410b42a9e556b77ae6a23729d23031301b0b1354c4aee65d6562ee2"),
    )
    # These are the authentic aggregation receipts, not substitute notice templates.
    records = [{"source": f"dependency-sources/notices/libarchive-3.8.10/{name}",
                "sha256": checksum} for name, checksum in materials]
    provenance = {
        "manager": "pacman", "package": "libarchive", "version": version,
        "source_package": "libarchive", "source_version": version,
        "pkgbuild_sha256": recipe, "original_sha256": elf,
        "binary_package": {
            "authentication": "gpgv detached signature with installed pacman keyring",
            "mtree_sha256": mtree,
        },
    }
    members = {"usr/lib/" + library: elf}
    originals = copy.deepcopy((records, provenance, members))
    binding = runtime_bundle._notice_source_binding(source, provenance, members)
    assert binding == {
        "pkgbuild_sha256": recipe,
        "binary_member": {"path": "usr/lib/" + library, "sha256": elf, "mtree_sha256": mtree},
    }
    evidence = {}
    terms = runtime_bundle._component_terms(
        source, "libarchive", raw, "", records, evidence,
        source_version=version, source_binding=binding)
    assert terms == expected
    assert runtime_bundle._source_required(terms) is False
    assert evidence["notices"] == records
    primary = evidence["primary_notices"]
    assert len(primary) == 3
    assert evidence["primary_notice"] == primary[0]
    for notice, record in zip(primary, records):
        assert notice["source"] == record["source"]
        assert notice["sha256"] == record["sha256"]
        assert notice["scope"]
        assert notice["origin"]
        assert notice["license_expression"] == expected
        assert notice["source_package"] == "libarchive"
        assert notice["source_version"] == version
        assert notice["library"] == library
    assert evidence["notice_source_binding"] == binding
    notice_source = evidence["notice_source"]
    assert notice_source["commit"] == commit
    assert notice_source["archive_url"] == f"https://codeload.github.com/libarchive/libarchive/tar.gz/{commit}"
    assert notice_source["archive_sha256"] == "391daa6bbb2d0d8315a8dea5dc741f071e759cfeca2fe0bb90999b42979743f1"
    assert notice_source["archive_size"] == 6829745
    assert notice_source["pkgbuild_sha256"] == recipe
    assert notice_source["library_sha256"] == elf
    assert notice_source["collector"] == "libarchive-3.8.10-library-comment-corpus-v1"
    assert notice_source["cc0_files"] == [
        "libarchive/archive_blake2.h", "libarchive/archive_blake2_impl.h",
        "libarchive/archive_blake2s_ref.c", "libarchive/archive_blake2sp_ref.c",
    ]
    assert notice_source["qualification"]

    def rejected(notices=records, package="libarchive", release=version,
                 name=library, declaration=raw, proof=binding):
        failed = {}
        try:
            result = runtime_bundle._component_terms(
                Path("/usr/lib") / name, package, declaration, "", notices, failed,
                source_version=release, source_binding=proof)
            assert result == declaration, "unbound declaration was silently rewritten"
            runtime_bundle._source_required(result)
        except ValueError:
            assert "primary_notice" not in failed
            assert "primary_notices" not in failed
            assert "notice_source" not in failed
            assert "notice_source_binding" not in failed
        else:
            raise AssertionError(f"unbound libarchive notice accepted: {package}/{release}/{name}")

    for scope in ({}, {"source_version": version}, {"source_binding": binding}):
        failed = {}
        try:
            runtime_bundle._component_terms(
                source, "libarchive", raw, "", records, failed, **scope)
        except ValueError:
            assert "primary_notice" not in failed
            assert "primary_notices" not in failed
        else:
            raise AssertionError("omitted libarchive version/binding accepted")
    rejected(notices=[])
    for index in range(len(records)):
        for checksum in (None, "0" * 64, materials[(index + 1) % len(materials)][1]):
            incomplete = copy.deepcopy(records)
            if checksum is None:
                incomplete.pop(index)
            else:
                incomplete[index]["sha256"] = checksum
            rejected(notices=incomplete)
    for release in (None, "", "3.8.10-1", "3.8.9-1.1"):
        rejected(release=release)
    for package in (None, "", "unrelated-package", "libarchive-tools"):
        rejected(package=package)
    for name in ("", "libarchive.so", "libarchive.so.13", "libarchive.so.13.8.9",
                 "libarchive.so.13.8.11", library + ".debug", "bsdtar", "libunrelated.so.1"):
        rejected(name=name)
    for declaration in (
        "(BSD-2-Clause) OR (BSD-4-Clause-UC) AND (Apache-2.0 OR CC0-1.0 OR OpenSSL)",
        "(BSD-2-Clause AND BSD-4-Clause-UC AND Apache-2.0) OR CC0-1.0 OR OpenSSL",
        "(BSD-2-Clause) AND ((BSD-4-Clause-UC) OR (Apache-2.0 OR CC0-1.0 OR OpenSSL))",
        "(BSD-2-Clause) AND (BSD-4-Clause-UC) AND (Apache-2.0 AND CC0-1.0 OR OpenSSL)",
        raw + " AND GPL-2.0-only", f"({raw}) OR GPL-2.0-only",
    ):
        rejected(declaration=declaration)
    for proof in (None, {}, {"pkgbuild_sha256": recipe},
                  {"binary_member": binding["binary_member"]}):
        rejected(proof=proof)
    for field, values in (
        ("pkgbuild_sha256", (None, "", "0" * 64)),
        ("binary_member", (None, {})),
    ):
        for value in values:
            bad = copy.deepcopy(binding)
            bad[field] = value
            rejected(proof=bad)
    for field, values in (
        ("path", (None, "", "usr/lib/libarchive.so.13", "usr/lib/libarchive.so.13.8.9")),
        ("sha256", (None, "", "0" * 64)),
        ("mtree_sha256", (None, "", "invalid", "A" * 64)),
    ):
        bad = copy.deepcopy(binding)
        del bad["binary_member"][field]
        rejected(proof=bad)
        for value in values:
            bad = copy.deepcopy(binding)
            bad["binary_member"][field] = value
            rejected(proof=bad)

    # Exercise the actual binding helper and its real MTREE member comparison.
    invalid_provenances = []
    for field, value in (
        ("manager", "dpkg"), ("package", "libarchive-tools"), ("version", "3.8.10-1"),
        ("source_version", "3.8.9-1.1"),
        ("pkgbuild_sha256", "0" * 64), ("original_sha256", "0" * 64),
        ("binary_package", {}),
    ):
        for replacement in (None, "", value):
            bad = copy.deepcopy(provenance)
            bad[field] = replacement
            invalid_provenances.append(bad)
        bad = copy.deepcopy(provenance)
        del bad[field]
        invalid_provenances.append(bad)
    for field, values in (
        ("authentication", (None, "", "unverified", "gpgv")),
        ("mtree_sha256", (None, "", "invalid", "A" * 64)),
    ):
        bad = copy.deepcopy(provenance)
        del bad["binary_package"][field]
        invalid_provenances.append(bad)
        for value in values:
            bad = copy.deepcopy(provenance)
            bad["binary_package"][field] = value
            invalid_provenances.append(bad)
    for bad, candidate_members in (
        [(bad, members) for bad in invalid_provenances]
        + [(provenance, {}), (provenance, {"usr/lib/" + library: "0" * 64}),
           (provenance, {"usr/lib/libarchive.so.13": elf})]
    ):
        try:
            runtime_bundle._notice_source_binding(source, bad, candidate_members)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid libarchive source/member authentication accepted")
    assert runtime_bundle._notice_source_binding(
        Path("/usr/lib/libunrelated.so.1"), provenance, members) is None
    for package in (None, "", "unrelated-package"):
        assert runtime_bundle._notice_source_binding(
            source, dict(provenance, source_package=package), members) is None
    unrelated = copy.deepcopy(provenance)
    del unrelated["source_package"]
    assert runtime_bundle._notice_source_binding(source, unrelated, members) is None
    canonical = f"({expected}) AND GPL-2.0-only"
    assert runtime_bundle._source_required(canonical) is True
    untouched = {}
    assert runtime_bundle._component_terms(
        source, "libarchive", canonical, "", records, untouched,
        source_version=version, source_binding=binding) == canonical
    assert untouched == {"notices": records}
    for declaration in ("OpenSSL", raw):
        try:
            runtime_bundle._source_required(declaration)
        except ValueError:
            pass
        else:
            raise AssertionError("raw OpenSSL was globally admitted")
    assert (records, provenance, members) == originals


def libarchive_notice_collector():
    """Compare byte output independently, including lexer exclusions and raw offsets."""
    preamble = (
        b"libarchive 3.8.10 library source notices\n"
        b"Derived from primary upstream commit 0c48412be2d94afe1859a807ea7896a18d8900bd.\n"
        b"Scope: all versioned top-level libarchive/*.c, *.h and *.h.in; all conditional platforms included.\n"
        b"The preamble and labeled separators are derived metadata; source comment blocks are verbatim bytes.\n"
        b"Individual source notices control; unchanged upstream COPYING supplies its stated default where needed.\n"
        b"License choice for the four BLAKE2 reference files only: CC0-1.0; see cc0-1.0.txt.\n\n"
    )
    first = b"/* Copyright (c) 2026 First Holder\r\n * permission is granted.\r\n */"
    second = b"/* Copyright (c) Regents, second genuine block.\n * Clause 3 removed; all rights reserved.\n */"
    line = b"// SPDX: test-license; no whitespace normalization"
    prefix = (
        b'const char *s = "/* copyright fake */ // license fake";\r\n'
        b'const char *escaped = "\\"/* permission is fake */";\n'
        b"int multi = '/* copyright character fake */';\n"
        b"int quote = '\\''; int slash = '/'; int star = '*';\n"
        b"/* implementation-only comment */\n"
        b"// implementation-only line comment\r\n"
        b"const char *utf8 = \"\xc3\xa9\";\n"
    )
    middle = b"\r\nint n = 1;\n"
    tail = b"\r\nint m = 2;\n"
    source = prefix + first + middle + second + tail + line + b"\r\n"
    no_legal = b'const char *s = "/* copyright fake */";\nint c = \'// license fake\';\n/* ordinary */\n'
    header = b"/* public\n * domain; trademark retained */"
    # Insertion order differs from ASCII path order, and .h.in is included.
    sources = {"libarchive/z_fixture.h.in": header,
               "libarchive/b_default.h": no_legal,
               "libarchive/a_fixture.c": source}
    originals = copy.deepcopy(sources)
    starts = (len(prefix), len(prefix + first + middle),
              len(prefix + first + middle + second + tail))
    selected = {
        "libarchive/a_fixture.c": tuple(zip(starts, (first, second, line))),
        "libarchive/b_default.h": (),
        "libarchive/z_fixture.h.in": ((0, header),),
    }
    expected = bytearray(preamble)
    for path in sorted(sources):
        data = sources[path]
        blob = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
        expected.extend(
            (f"===== DERIVED SOURCE IDENTITY: {path} =====\n"
             f"source-sha256: {hashlib.sha256(data).hexdigest()}\n"
             f"source-size: {len(data)}\n"
             f"git-blob-sha1: {blob}\n").encode("ascii"))
        for index, (start, block) in enumerate(selected[path], 1):
            end = start + len(block)
            assert data[start:end] == block
            expected.extend(
                f"----- DERIVED COMMENT BLOCK {index}: source bytes [{start}, {end}) -----\n".encode("ascii"))
            expected.extend(block + b"\n\n")
        if not selected[path]:
            expected.extend(
                b"No qualifying source comment; the unchanged upstream COPYING default applies. No author list is inferred.\n\n")
    assert runtime_bundle._libarchive_corpus(sources) == bytes(expected)
    assert sources == originals

def cached_source_component_notices():
    """Real libasyncns source bytes resolve LGPL only with the complete signed binding."""
    import io
    import tarfile

    original_root = Path(__file__).resolve().parents[2] / "work/tools/dependency-sources"
    inventory = json.loads((original_root / "provenance.json").read_text(encoding="utf-8"))
    record = next(item for item in inventory["packages"] if item["package"] == "libasyncns")
    version = "1:0.8+r3+g68cd5af-3.2"
    library = "libasyncns.so.0.3.1"
    source = Path("/usr/lib") / library
    recipe = "c3406297b1bf539da442e3e35cb43f47652d6adaac487028012fda4f9e48d122"
    elf = "532222b24689f1d81d70748db19cc11f84ba468cb204f4f1db2584357371ac92"
    mtree = "07ce825c50ead527ff2eb74983a9aa83243171281b562dbccf22894d6199dfd9"
    binary_hash = "9a5d3d165207882ffee4bfc885194999b3b3c8e6ba73ceafb4d2a7e01670002f"
    provenance = {
        "manager": "pacman", "package": "libasyncns", "version": version,
        "source_package": "libasyncns", "source_version": version,
        "distribution": record["distribution"], "license_expression": "LGPL",
        "pkgbuild_sha256": recipe, "original_sha256": elf,
        "binary_package": {
            "sha256": binary_hash,
            "authentication": "gpgv detached signature with installed pacman keyring",
            "mtree_sha256": mtree,
        },
    }
    members = {"usr/lib/" + library: elf}
    archive_relative = f"libasyncns/{version}/libasyncns-68cd5aff1467638c086f1bedcc750e34917168e4.tar.gz"
    with tarfile.open(original_root / archive_relative, "r:gz") as archive:
        actual = {name: archive.extractfile(name).read()
                  for name in ("LICENSE", "libasyncns/asyncns.c", "libasyncns/asyncns.h")}
    pins = {
        "LICENSE": "d80c9d084ebfb50ea1ed91bfbc2410d6ce542097a32c43b00781b83adcb8c77f",
        "libasyncns/asyncns.c": "4db61110d36de49df9162499df3b875c2fa6744418bc2f40f79bb7e78a037abb",
        "libasyncns/asyncns.h": "74a2409ab37cddd0fbb8c49230189913b025368f598ec5172ee468d185fe444e",
    }
    for name, data in actual.items():
        assert hashlib.sha256(data).hexdigest() == pins[name]
    for name in ("libasyncns/asyncns.c", "libasyncns/asyncns.h"):
        assert b"Copyright 2005-2008 Lennart Poettering" in actual[name]
        assert b"either version 2.1" in actual[name]
        assert b"(at your option) any later version" in actual[name]
    assert runtime_bundle._standard_license_text("LGPL") is None
    assert runtime_bundle._license_expression("LGPL") == "LGPL"
    try:
        runtime_bundle._source_required("LGPL")
    except ValueError:
        pass
    else:
        raise AssertionError("bare LGPL was globally assigned a version")

    with TemporaryDirectory() as temporary:
        root = Path(temporary)
        for material in record["materials"]:
            target = root / material["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original_root / material["path"], target)
        index = root / "provenance.json"
        original_inventory = {"format": 1, "packages": [record]}
        index.write_text(json.dumps(original_inventory), encoding="utf-8")

        def collect(proof=provenance, signed_members=members, selected=source):
            return runtime_bundle._component_notice_supplements(
                root, selected, proof["source_package"], proof["source_version"],
                provenance=proof, members=signed_members)

        paths = collect()
        assert {path.read_bytes() for path in paths} == set(actual.values())
        assert len(paths) == 3
        assert all(path.is_relative_to(root / "notices") for path in paths)
        notices = [{"source": str(path), "sha256": runtime_bundle._digest(path)} for path in paths]
        binding = runtime_bundle._notice_source_binding(source, provenance, members)
        evidence = {}
        terms = runtime_bundle._component_terms(
            source, "libasyncns", "LGPL", "", notices, evidence,
            source_version=version, source_binding=binding)
        assert terms == "LGPL-2.1-or-later"
        assert runtime_bundle._source_required(terms) is True
        assert len(evidence["primary_notices"]) == 3
        assert evidence["notice_source_binding"] == binding
        assert runtime_bundle._source_required(f"({terms}) AND GPL-2.0-only") is True
        assert runtime_bundle._source_required(f"({terms}) OR MIT") is False

        def reject_terms(candidate=notices, release=version, selected=source, proof=binding):
            try:
                result = runtime_bundle._component_terms(
                    selected, "libasyncns", "LGPL", "", candidate,
                    source_version=release, source_binding=proof)
                runtime_bundle._source_required(result)
            except ValueError:
                return
            raise AssertionError("unbound or incomplete LGPL source grant accepted")

        reject_terms(notices[:1])  # The full LGPL body alone is not a component grant.
        for position in range(3):
            reject_terms(notices[:position] + notices[position + 1:])
            tampered = copy.deepcopy(notices)
            tampered[position]["sha256"] = "0" * 64
            reject_terms(tampered)
        reject_terms(release="1:0.8+r3+g68cd5af-3.1")
        reject_terms(selected=Path("/usr/lib/libasyncns.so.0"))
        reject_terms(proof=None)
        for field, value in (("pkgbuild_sha256", "0" * 64),
                             ("binary_member", {**binding["binary_member"], "mtree_sha256": "0" * 64})):
            reject_terms(proof={**binding, field: value})

        def reject_collect(proof=provenance, signed_members=members):
            try:
                collect(proof, signed_members)
            except ValueError:
                return
            raise AssertionError("untrusted cached source grant accepted")

        for field, value in (("package", "other"), ("version", "wrong"),
                             ("pkgbuild_sha256", "0" * 64), ("original_sha256", "0" * 64),
                             ("distribution", {"id": "other", "version_id": ""})):
            reject_collect({**provenance, field: value})
        for field, value in (("sha256", "0" * 64), ("mtree_sha256", "0" * 64),
                             ("authentication", "unverified")):
            reject_collect({**provenance, "binary_package": {**provenance["binary_package"], field: value}})
        reject_collect(signed_members={})
        reject_collect(signed_members={"usr/lib/" + library: "0" * 64})
        for mutate in (
            lambda value: value["packages"].clear(),
            lambda value: value["packages"].append(copy.deepcopy(record)),
            lambda value: value["packages"][0].update(source_version="wrong"),
            lambda value: value["packages"][0]["authentication"].update(pkgbuild_sha256="0" * 64),
            lambda value: value["packages"][0]["authentication"]["required_materials"].pop(),
            lambda value: value["packages"][0]["authentication"]["binary_archive"].update(sha256="0" * 64),
        ):
            bad = copy.deepcopy(original_inventory)
            mutate(bad)
            index.write_text(json.dumps(bad), encoding="utf-8")
            reject_collect()
        index.write_text(json.dumps(original_inventory), encoding="utf-8")
        for relative in (archive_relative, f"libasyncns/{version}/PKGBUILD",
                         f"libasyncns/{version}/.SRCINFO"):
            file = root / relative
            original = file.read_bytes()
            file.write_bytes(original + b"tampered")
            reject_collect()
            file.write_bytes(original)
        # Cached output never substitutes for revalidating the source and recipe.
        cached_notice = paths[1].read_bytes()
        paths[1].write_bytes(b"tampered grant")
        reject_collect()
        paths[1].write_bytes(cached_notice)

        contract = runtime_bundle._COMPONENT_NOTICES["libasyncns"]
        assert runtime_bundle._source_archive_notices(root / archive_relative, contract) == actual
        # Exercise the member boundary independently of the outer archive checksum.
        for mutation in ("missing", "tampered", "duplicate", "link", "directory", "traversal"):
            malformed = root / "malformed.tar.gz"
            with tarfile.open(malformed, "w:gz") as archive:
                for name, data in actual.items():
                    if mutation == "missing" and name == "libasyncns/asyncns.h":
                        continue
                    item = tarfile.TarInfo(name)
                    item.mode = 0o664
                    item.size = len(data)
                    if name == "libasyncns/asyncns.h":
                        if mutation == "tampered":
                            data = b"x" + data[1:]
                        elif mutation == "link":
                            item.type, item.linkname, item.size = tarfile.SYMTYPE, "asyncns.c", 0
                        elif mutation == "directory":
                            item.type, item.size = tarfile.DIRTYPE, 0
                    archive.addfile(item, io.BytesIO(data) if item.isreg() else None)
                    if mutation == "duplicate" and name == "libasyncns/asyncns.h":
                        archive.addfile(item, io.BytesIO(data))
                if mutation == "traversal":
                    item = tarfile.TarInfo("../escape")
                    item.size = 1
                    archive.addfile(item, io.BytesIO(b"x"))
            altered = copy.deepcopy(contract)
            altered["notice_source"].update(
                archive_sha256=runtime_bundle._digest(malformed), archive_size=malformed.stat().st_size)
            try:
                runtime_bundle._source_archive_notices(malformed, altered)
            except ValueError:
                pass
            else:
                raise AssertionError(f"invalid source notice member accepted: {mutation}")



def authenticated_distro_notice_links():
    """Signed notice links retain bytes only within the authenticated owner boundary."""
    with TemporaryDirectory() as temporary:
        root = Path(temporary)
        data = b"Copyright fixture\r\nPermission granted.\x00\n"
        digest = hashlib.sha256(data).hexdigest()
        notice = root / "usr/share/doc/example/COPYING"
        link = root / "usr/share/licenses/example/COPYING"
        notice.parent.mkdir(parents=True)
        link.parent.mkdir(parents=True)
        notice.write_bytes(data)
        notice.chmod(0o644)
        link.symlink_to("/usr/share/doc/example/COPYING")
        manifest = (
            "#mtree\n/set type=file mode=644\n"
            "./usr/share/licenses/example type=dir mode=755\n"
            "./usr/share/licenses/example/COPYING type=link mode=777 "
            "link=/usr/share/doc/example/COPYING\n"
            f"./usr/share/doc/example/COPYING mode=0644 size={len(data)} sha256digest={digest}\n")
        entries = runtime_bundle._mtree_members(manifest.encode(), notice_entries=True)

        def read():
            return runtime_bundle._read_pacman_notice(link, entries, root=root)

        def reject(path=link, signed=entries):
            try:
                runtime_bundle._read_pacman_notice(path, signed, root=root)
            except (ValueError, FileNotFoundError):
                return
            raise AssertionError(f"unauthenticated distro notice accepted: {path}")

        resolved, retained = read()
        assert resolved == notice and retained == data
        assert resolved.stat().st_mode & 0o7777 == 0o644
        assert runtime_bundle._resolve_pacman_notice(link, entries, root=root) == notice
        notice.write_bytes(b"changed notice")
        reject()
        notice.write_bytes(data)
        notice.chmod(0o600)
        reject()
        notice.chmod(0o644)
        link.unlink()
        link.symlink_to("../../doc/example/COPYING")
        reject()  # Same target and bytes, but not the authenticated link spelling.
        relative = copy.deepcopy(entries)
        relative["usr/share/licenses/example/COPYING"]["link"] = "../../doc/example/COPYING"
        assert runtime_bundle._read_pacman_notice(link, relative, root=root) == (notice, data)
        for target in ("/outside/COPYING", "/usr/share/doc/other-owner/COPYING"):
            file = root / target.lstrip("/")
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(data)
            file.chmod(0o644)
            link.unlink()
            link.symlink_to(target)
            signed = copy.deepcopy(entries)
            signed["usr/share/licenses/example/COPYING"]["link"] = target
            if target.startswith("/outside/"):
                signed[target.lstrip("/")] = dict(entries["usr/share/doc/example/COPYING"])
            reject(signed=signed)
        link.unlink()
        link.symlink_to("/usr/share/doc/example/COPYING")
        reject(root / "usr/share/licenses/example/../example/COPYING")
        staging = root / "staging/COPYING"
        staging.parent.mkdir()
        staging.write_bytes(data)
        reject(staging)
        notice.parent.rename(notice.parent.with_name("real-example"))
        notice.parent.symlink_to("real-example", target_is_directory=True)
        reject()  # An unrecorded parent link cannot redirect an authenticated member.
        parent_signed = copy.deepcopy(entries)
        parent_signed["usr/share/doc/example"] = {
            "type": "link", "mode": "777", "link": "real-example"}
        parent_signed["usr/share/doc/real-example/COPYING"] = parent_signed.pop(
            "usr/share/doc/example/COPYING")
        assert runtime_bundle._read_pacman_notice(link, parent_signed, root=root) == (
            root / "usr/share/doc/real-example/COPYING", data)


def canonical_gpl2_license_text():
    canonical = Path("/usr/share/licenses/spdx/GPL-2.0-only.txt").read_bytes()
    assert hashlib.sha256(canonical).hexdigest() == (
        "aaf135472f81c5b4a0dca9367e5bb5e9750032b5bebe5442b36e4c0a47430df3")
    for identifier in ("GPL2", "GPL-2.0-only"):
        path = runtime_bundle._standard_license_text(identifier)
        assert path is not None, identifier
        assert path.read_bytes() == canonical, identifier
        expression = runtime_bundle._license_expression(identifier)
        assert expression == "GPL-2.0-only", identifier
        assert runtime_bundle._source_required(expression) is True, identifier
    for label in ("LicenseRef-unverified-GPL2", "GPL2-custom"):
        expression = runtime_bundle._license_expression(label)
        assert expression == label
        try:
            runtime_bundle._source_required(expression)
        except ValueError:
            pass
        else:
            raise AssertionError(f"unknown GPL2 custom declaration accepted: {label}")


def empty_stdlib_modules():
    """Owned empty package modules remain importable from the deployed stdlib ZIP."""
    import importlib
    import zipfile

    with TemporaryDirectory() as temporary:
        root = Path(temporary)
        stdlib = root / "stdlib"
        package_name = "empty_stdlib_fixture"
        package = stdlib / package_name
        package.mkdir(parents=True)
        initializer = package / "__init__.py"
        initializer.write_bytes(b"")
        submodule = package / "values.py"
        submodule.write_bytes(b"VALUE = 73\n")
        owned = {initializer, submodule}
        for directory in ("site-packages", "dist-packages", "__pycache__", "test", "tests",
                          "lib-dynload", "idlelib", "tkinter", "ensurepip", "venv", ".hidden"):
            excluded = stdlib / directory
            excluded.mkdir()
            (excluded / "ignored.py").write_bytes(b"")
        for filename in (".hidden.py", "test_ignored.py", "sitecustomize.py", "usercustomize.py"):
            (stdlib / filename).write_bytes(b"")
        (stdlib / "linked").symlink_to(package, target_is_directory=True)

        target = root / "stdlib.zip"
        runtime_bundle._write_stdlib_archive(target, stdlib, owned)
        with zipfile.ZipFile(target) as archive:
            assert archive.namelist() == [f"{package_name}/__init__.py", f"{package_name}/values.py"]
            assert archive.read(f"{package_name}/__init__.py") == b""
            assert archive.read(f"{package_name}/values.py") == submodule.read_bytes()
            for entry in archive.infolist():
                assert entry.external_attr >> 16 == 0o100644
                assert entry.date_time == (1980, 1, 1, 0, 0, 0)
                assert entry.create_system == 3
                assert entry.compress_type == zipfile.ZIP_DEFLATED
        with patch.object(sys, "path", [str(target), *sys.path]):
            try:
                imported_package = importlib.import_module(package_name)
                imported_submodule = importlib.import_module(f"{package_name}.values")
                assert imported_submodule.VALUE == 73
                assert imported_package.__file__ == f"{target}/{package_name}/__init__.py"
                assert imported_submodule.__file__ == f"{target}/{package_name}/values.py"
            finally:
                sys.modules.pop(f"{package_name}.values", None)
                sys.modules.pop(package_name, None)
                sys.path_importer_cache.pop(str(target), None)
                sys.path_importer_cache.pop(f"{target}/{package_name}", None)

        try:
            runtime_bundle._regular(initializer)
        except ValueError:
            pass
        else:
            raise AssertionError("default regular-file guard accepted an empty file")
        runtime_bundle._regular(submodule)
        symlink = stdlib / "symlink.py"
        symlink.symlink_to(submodule)
        for path, limit in ((package, 1024), (symlink, 1024), (root / "missing.py", 1024),
                            (submodule, 1)):
            try:
                runtime_bundle._regular(path, limit, allow_empty=True)
            except (ValueError, FileNotFoundError):
                pass
            else:
                raise AssertionError(f"empty opt-in bypassed the regular-file boundary: {path}")
        symlink.unlink()
        unowned = stdlib / "unowned.py"
        unowned.write_bytes(b"")
        for name, inventory in (("unowned", owned), ("symlink", owned | {symlink})):
            if name == "symlink":
                unowned.unlink()
                symlink.symlink_to(submodule)
            try:
                runtime_bundle._write_stdlib_archive(root / f"{name}.zip", stdlib, inventory)
            except ValueError:
                pass
            else:
                raise AssertionError(f"stdlib ZIP accepted {name} Python source")


def main():
    dbus_dependency_selection()
    empty_stdlib_modules()
    authenticated_distro_notice_links()
    canonical_gpl2_license_text()
    bsd_component_notices()
    versioned_component_notices()
    kerberos_component_notices()
    libarchive_component_notices()
    libarchive_notice_collector()
    cached_source_component_notices()
    accepted = {
        "bin/qt.conf", "tools/innoextract", "runtime/bin/python3",
        "lib/libQt6Concurrent.so.6", "lib/libQt6Concurrent.so.6.12.0",
        "share/licenses/citroen-c4-robot/libqt6concurrent6t64/LICENSE.txt",
        "share/citroen-c4-robot/runtime-manifest.json",
        "share/licenses/citroen-c4-robot/qt6-base/LICENSE.txt",
        "share/licenses/citroen-c4-robot/libqt6core6t64/LICENSE.txt",
        "share/licenses/citroen-c4-robot/appimage-runtime/LICENSE.txt",
    } | set(PLUGIN_PATHS)
    for base in LIBRARY_BASES:
        accepted.update({f"lib/lib{base}.so", f"lib/lib{base}.so.1",
                         f"lib/lib{base}.so.7600.6.0"})
    for minor in PYTHON_MINORS:
        accepted.add(f"runtime/lib/python3{minor}.zip")
        for module in EXTENSION_MODULES:
            accepted.add(f"runtime/lib/python3.{minor}/lib-dynload/{module}.cpython-3{minor}-x86_64-linux-gnu.so")
            accepted.add(f"runtime/lib/python3.{minor}/lib-dynload/{module}.so")
    for name in accepted:
        assert allowed_runtime_path(name), f"required runtime path rejected: {name}"
        for bad in ("/" + name, "./" + name, name + "/", name + "\x00",
                    name.replace("/", "//", 1), name.replace("/", "\\", 1)):
            assert not allowed_runtime_path(bad), f"unsafe path accepted: {bad!r}"
    rejected = [
        "", None, 123, "../private.pem", "lib/../id_rsa", "lib/libunknown.so.1",
        "lib/libQt6Core.so.10000", "lib/libQt6Core.so.1.2.3.4.5",
        "lib/libQt6Core.so.debug", "lib/libQt6Core.so.6/signing-key.pem",
        "lib/libQt6Concurrent.so.debug", "lib/libQt6ConcurrentPrivate.so.6",
        "lib/libQt6Qml.so.6",
        "lib/libc.so.6", "lib/libmvec.so.1", "lib/libEGL.so.1",
        "lib/libvulkan_lvp.so", "lib/libvulkan_radeon.so",
        "share/vulkan/icd.d/private.json",
        "lib/libx264.so.165", "lib/libswresample.so.7", "lib/libOpenCL.so.1",
        "lib/libudev.so.1", "lib/libpython3.10.so.1.0", "lib/libpython3.99.so.1",
        "lib/libpulsecommon-999.999.so", "runtime/lib/python310.zip",
        "runtime/lib/python399.zip", "runtime/lib/python3.14/site-packages/private.py",
        "runtime/lib/python3.14/lib-dynload/_testcapi.cpython-314-x86_64-linux-gnu.so",
        "runtime/lib/python3.14/lib-dynload/zlib.cpython-313-x86_64-linux-gnu.so",
        "runtime/lib/python3.14/lib-dynload/zlib.cpython-314-aarch64-linux-gnu.so",
        "runtime/lib/python3.14/lib-dynload/secrets.so", "runtime/bin/pip",
        "plugins/platforms/libqoffscreen.so", "plugins/imageformats/libqjpeg.so",
        "plugins/tls/libqcertonlybackend.so", "plugins/tls/private.pem",
        "share/licenses/citroen-c4-robot/qt6-base/id_rsa",
        "share/licenses/citroen-c4-robot/qt6-base/password.txt",
        "share/licenses/citroen-c4-robot/qt6-base/NOTICE.txt",
        "share/licenses/citroen-c4-robot/.private/LICENSE.txt",
        "share/licenses/citroen-c4-robot/../LICENSE.txt",
        "share/licenses/citroen-c4-robot/qt6-base/subdir/LICENSE.txt",
        "share/licenses/citroen-c4-robot/pkg..name/LICENSE.txt",
        "share/licenses/citroen-c4-robot/pkg:amd64/LICENSE.txt",
        "share/licenses/citroen-c4-robot/" + "p" * 97 + "/LICENSE.txt",
        "share/citroen-c4-robot/assets/customer.private", "share/secrets/password.txt",
        "tools/innoextract-private", "bin/signing-key.pem", "bin/qt.conf\n",
        "lib/libQt6Cоre.so.6",  # Cyrillic homoglyph must not pass the ASCII gate.
    ]
    for name in rejected:
        assert not allowed_runtime_path(name), f"private/unlisted path accepted: {name!r}"
    assert not runtime_bundle._host_dependency("libvulkan.so.1", bundled_vulkan=True)
    assert runtime_bundle._host_dependency("libvulkan.so.1")
    for name in ("libEGL.so.1", "libdrm.so.2", "libudev.so.1"):
        assert runtime_bundle._host_dependency(name)
        assert runtime_bundle._host_dependency(name, bundled_vulkan=True)
    fullscreen_shader = "share/citroen-c4-robot/shaders/vulkan/fullscreen.vert.spv"
    assert len(runtime_bundle.VULKAN_SHADER_PATHS) == 6
    assert fullscreen_shader in runtime_bundle.VULKAN_SHADER_PATHS
    with TemporaryDirectory() as temporary:
        stage = Path(temporary)
        assert runtime_bundle._vulkan_runtime_required(stage) is False
        unrelated = stage / "private.spv"
        unrelated.write_bytes(b"shader fixture")
        assert runtime_bundle._vulkan_runtime_required(stage) is False
        shaders = [stage / name for name in runtime_bundle.VULKAN_SHADER_PATHS]
        shaders[0].parent.mkdir(parents=True, exist_ok=True)
        shaders[0].write_bytes(b"shader fixture")
        try:
            runtime_bundle._vulkan_runtime_required(stage)
        except ValueError:
            pass
        else:
            raise AssertionError("partial Vulkan shader set accepted")
        for shader in shaders:
            shader.parent.mkdir(parents=True, exist_ok=True)
            shader.write_bytes(b"shader fixture")
        assert runtime_bundle._vulkan_runtime_required(stage) is True
        fullscreen = stage / fullscreen_shader
        fullscreen.unlink()
        try:
            runtime_bundle._vulkan_runtime_required(stage)
        except ValueError:
            pass
        else:
            raise AssertionError("legacy five-shader Vulkan set accepted")
        fullscreen.write_bytes(b"shader fixture")
        shaders[0].unlink()
        shaders[0].symlink_to(unrelated)
        try:
            runtime_bundle._vulkan_runtime_required(stage)
        except ValueError:
            pass
        else:
            raise AssertionError("symlink Vulkan shader accepted")
    notes = ("Properties: x86 ISA needed: x86-64-baseline, x86-64-v2, x86-64-v3, "
             "x86 feature used: XMM, x86 ISA used: x86-64-v4\n")
    with patch.object(runtime_bundle, "_run", return_value=notes):
        assert runtime_bundle._gnu_x86_isa_needed(Path("unused")) == [
            "x86-64-baseline", "x86-64-v2", "x86-64-v3"]
    with patch.object(runtime_bundle, "_run", return_value="x86 ISA used: x86-64-v4\n"):
        assert runtime_bundle._gnu_x86_isa_needed(Path("unused")) is None
    assert runtime_bundle._source_required("MIT OR GPL-2.0-only") is False
    assert runtime_bundle._source_required("LGPL-2.1-or-later AND MIT") is True
    assert runtime_bundle._source_required("GPL-3.0-or-later WITH GCC-exception-3.1") is False
    assert runtime_bundle._source_required("LicenseRef-MIT-SIPB") is False
    for expression in ("MIT-open-group", "NAIST-2003", "SMLNJ",
                       "BSD-3-Clause WITH PCRE2-exception"):
        assert runtime_bundle._source_required(expression) is False
        assert runtime_bundle._source_required(f"({expression}) AND GPL-2.0-only") is True
        assert runtime_bundle._source_required(f"GPL-2.0-only OR ({expression})") is False
        assert runtime_bundle._license_expression(expression) == expression
    assert runtime_bundle._license_expression("LGPL-2.1+ or GPL-2+") == "LGPL-2.1-or-later OR GPL-2.0-or-later"
    assert runtime_bundle._component_terms(
        Path("libmount.so.1"), "util-linux", "GPL-2.0-only AND LGPL-2.1-or-later",
        "GNU Lesser General Public License") == "LGPL-2.1-or-later"
    assert runtime_bundle._component_terms(
        Path("libuuid.so.1"), "util-linux", "GPL-2.0-only AND BSD-3-Clause",
        "Redistribution and use") == "BSD-3-Clause"
    assert runtime_bundle._component_terms(
        Path("liblzma.so.5"), "xz", "GPL AND LGPL AND custom", "liblzma is 0BSD") == "0BSD"
    members = runtime_bundle._mtree_members(
        ("#mtree\n./usr/lib/libstdc\\053\\053.so.6 type=file sha256digest=" + "b" * 64 + "\n").encode())
    member_provenance = {"original_sha256": "b" * 64,
                         "binary_package": {"mtree_sha256": "d" * 64}}
    member = runtime_bundle._bind_pacman_member(
        Path("/usr/lib/libstdc++.so.6"), member_provenance, members)
    assert member == {"path": "usr/lib/libstdc++.so.6", "sha256": "b" * 64, "mtree_sha256": "d" * 64}
    try:
        runtime_bundle._bind_pacman_member(
            Path("/usr/lib/libstdc++.so.6"), dict(member_provenance, original_sha256="c" * 64), members)
    except ValueError:
        pass
    else:
        raise AssertionError("changed ELF assigned the authenticated package source identity")
    for expression in (
            "LicenseRef-unknown", "MIT-open-group-variant", "MIT-Open-Group",
            "LicenseRef-MIT-open-group", "MIT-open-group OR LicenseRef-unknown",
            "LicenseRef-unknown OR MIT-open-group", "NAIST-2003-variant",
            "SMLNJ-variant", "LicenseRef-NAIST-2003", "LicenseRef-SMLNJ",
            "BSD-3-Clause WITH PCRE2-exception-variant",
            "BSD-2-Clause WITH PCRE2-exception", "MIT WITH PCRE2-exception",
            "GPL-2.0-only WITH PCRE2-exception"):
        try:
            runtime_bundle._source_required(expression)
        except ValueError:
            pass
        else:
            raise AssertionError(f"unknown source terms accepted: {expression}")
    provenance = {"type": "distro", "manager": "pacman", "package": "example",
                  "version": "1-1", "source_package": "example", "source_version": "1-1",
                  "distribution": {"id": "cachyos", "version_id": ""},
                  "license_expression": "LGPL-2.1-or-later", "source_required": True,
                  "pkgbuild_sha256": "a" * 64, "original_sha256": "b" * 64}
    bom = {"components": [{"path": "lib/libexample.so.1", "sha256": "c" * 64,
                           "provenance": provenance}]}
    assert dependency_sources.required_packages(bom) == [provenance]
    duplicated = copy.deepcopy(bom)
    duplicated["components"].append(copy.deepcopy(bom["components"][0]))
    assert dependency_sources.required_packages(duplicated) == [provenance]
    non_elf = copy.deepcopy(bom)
    non_elf["components"][0]["path"] = "share/licenses/citroen-c4-robot/example/LICENSE.txt"
    assert dependency_sources.required_packages(non_elf) == []
    missing = copy.deepcopy(bom)
    del missing["components"][0]["provenance"]["source_required"]
    try:
        dependency_sources.required_packages(missing)
    except ValueError:
        pass
    else:
        raise AssertionError("distro ELF with missing source obligation accepted")
    with TemporaryDirectory() as temporary:
        tools = Path(temporary)
        try:
            dependency_sources.acquire(bom, tools)
        except ValueError:
            pass
        else:
            raise AssertionError("missing exact pacman source inventory accepted")
        root = tools / "dependency-sources"
        root.mkdir(exist_ok=True)
        source = root / "example.tar.gz"
        source.write_bytes(b"source fixture")
        recipe = root / "PKGBUILD"
        recipe.write_bytes(b"recipe fixture")
        sourceinfo = root / ".SRCINFO"
        sourceinfo.write_text(
            "pkgbase = example\n\tsource = https://example.org/example.tar.gz\n\tsha256sums = "
            + hashlib.sha256(source.read_bytes()).hexdigest() + "\n", encoding="utf-8")
        record = {key: value for key, value in provenance.items()
                  if key not in {"type", "source_required", "pkgbuild_sha256"}}
        record.update(materials=[
            {"path": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
             "size": path.stat().st_size, "url": "https://example.org/" + path.name,
             "role": role} for path, role in ((source, "source"), (recipe, "recipe"), (sourceinfo, "recipe"))],
            authentication={"pkgbuild_sha256": hashlib.sha256(recipe.read_bytes()).hexdigest()},
            instructions="Use the retained recipe.")
        record["authentication"]["required_materials"] = [
            {field: item[field] for field in ("path", "sha256", "size", "role")}
            for item in record["materials"]]
        record["authentication"]["recipe_sources"] = [
            {"declared_source": "https://example.org/example.tar.gz", "material_path": source.name,
             **{field: record["materials"][0][field] for field in ("sha256", "size", "role")}}]
        inventory = {"format": 1, "packages": [record]}
        index = root / "provenance.json"
        index.write_text(json.dumps(inventory), encoding="utf-8")
        mismatched = copy.deepcopy(bom)
        mismatched["components"][0]["provenance"]["source_version"] = "2-1"
        for rejected_bom in (bom, mismatched):
            try:
                dependency_sources.acquire(rejected_bom, tools)
            except ValueError:
                pass
            else:
                raise AssertionError("source identity or authenticated recipe mismatch accepted")
        bound = copy.deepcopy(bom)
        bound["components"][0]["provenance"]["pkgbuild_sha256"] = record["authentication"]["pkgbuild_sha256"]
        dependency_sources.acquire(bound, tools)
        incomplete = copy.deepcopy(inventory)
        incomplete["packages"][0]["materials"].pop()
        index.write_text(json.dumps(incomplete), encoding="utf-8")
        try:
            dependency_sources.acquire(bound, tools)
        except ValueError:
            pass
        else:
            raise AssertionError("missing complete recipe material accepted")
        index.write_text(json.dumps(inventory), encoding="utf-8")
        source.write_bytes(b"changed source")
        try:
            dependency_sources.acquire(bound, tools)
        except ValueError:
            pass
        else:
            raise AssertionError("changed cached source bytes accepted")
    print("runtime path policy passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dbus-prefix", type=Path,
                        help="Exercise real owned libdbus source/config/ABI proof without executing it")
    options = parser.parse_args()
    if options.dbus_prefix is not None:
        dbus_prefix_boundaries(options.dbus_prefix)
    main()
