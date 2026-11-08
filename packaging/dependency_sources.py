#!/usr/bin/env python3
"""Acquire only exact source identities required by the deployed runtime BOM."""
from __future__ import annotations

import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import stat
import subprocess
import tempfile
from urllib.parse import urlsplit, urlunsplit

from appimage.acquire import _download, _sha256

IDENTITY = ("manager", "package", "version", "source_package", "source_version",
            "distribution", "license_expression")
APT_OPTIONS = ["-o", "APT::Get::AllowUnauthenticated=false", "-o",
               "Acquire::AllowInsecureRepositories=false", "-o",
               "Acquire::AllowDowngradeToInsecureRepositories=false"]
ROLES = {"source", "recipe", "patch", "buildinfo", "signature", "key",
         "configuration", "notice"}


def required_packages(bom: dict) -> list[dict]:
    """Select actual distro ELF components, never packagewide notice declarations."""
    from runtime_bundle import _source_required

    if not isinstance(bom, dict) or not isinstance(bom.get("components"), list):
        raise ValueError("Runtime source acquisition requires the actual runtime BOM")
    selected = {}
    for component in bom["components"]:
        path = component.get("path", "")
        provenance = component.get("provenance", {})
        if provenance.get("type") != "distro" or not (
                path.startswith(("lib/", "plugins/", "runtime/bin/"))
                or path.startswith("runtime/lib/") and path.endswith(".so")):
            continue
        if not isinstance(provenance.get("source_required"), bool):
            raise ValueError(f"Missing component source obligation: {path}")
        if _source_required(provenance.get("license_expression")) != provenance["source_required"]:
            raise ValueError(f"Component source obligation disagrees with terms: {path}")
        if not provenance["source_required"]:
            continue
        for digest in (provenance.get("original_sha256"), component.get("sha256")):
            if not isinstance(digest, str) or not re.fullmatch(r"[a-f0-9]{64}", digest):
                raise ValueError(f"Missing original/deployed ELF hash for source coverage: {path}")
        for field in IDENTITY:
            value = provenance.get(field)
            if field == "distribution":
                if (not isinstance(value, dict) or not isinstance(value.get("id"), str)
                        or not value["id"] or not isinstance(value.get("version_id"), str)):
                    raise ValueError(f"Missing source distribution identity: {path}")
            elif not isinstance(value, str) or not value:
                raise ValueError(f"Missing exact source identity {field}: {path}")
        key = tuple(provenance[field] for field in ("manager", "package", "version"))
        if key in selected and any(selected[key][field] != provenance[field] for field in IDENTITY):
            raise ValueError(f"Conflicting component source identity: {key}")
        selected[key] = provenance
    return [selected[key] for key in sorted(selected)]


def _safe_file(root: Path, relative: str) -> Path:
    if (not isinstance(relative, str) or not relative or "\\" in relative
            or not relative.isascii() or any(ord(char) < 32 for char in relative)
            or PurePosixPath(relative).is_absolute()
            or any(part in {"", ".", ".."} for part in relative.split("/"))):
        raise ValueError(f"Unsafe dependency source cache path: {relative!r}")
    path = root / relative
    try:
        for parent in path.parents:
            if parent == root or root in parent.parents:
                if not stat.S_ISDIR(parent.lstat().st_mode):
                    raise ValueError(f"Dependency source cache parent is not a real directory: {parent}")
        if not stat.S_ISREG(path.lstat().st_mode):
            raise ValueError(f"Dependency source material is not a regular file: {path}")
    except OSError as error:
        raise ValueError(f"Missing dependency source material: {path}") from error
    return path


def _validate(root: Path, record: dict, expected: dict) -> None:
    if any(record.get(field) != expected[field] for field in IDENTITY):
        raise ValueError(f"Dependency source identity mismatch: {expected['package']}")
    materials = record.get("materials")
    if not isinstance(materials, list) or not materials:
        raise ValueError("Dependency sources have no retained materials")
    seen, roles = set(), set()
    for material in materials:
        path = _safe_file(root, material.get("path"))
        if material["path"] in seen or material.get("role") not in ROLES:
            raise ValueError("Duplicate or unknown dependency source material")
        seen.add(material["path"])
        roles.add(material["role"])
        url = urlsplit(material.get("url", ""))
        if url.scheme != "https" or not url.netloc or url.username or url.password:
            raise ValueError("Dependency source materials require HTTPS origins")
        digest = material.get("sha256")
        size = material.get("size")
        if (not isinstance(digest, str) or not re.fullmatch(r"[a-f0-9]{64}", digest)
                or type(size) is not int or size <= 0 or path.stat().st_size != size
                or _sha256(path) != digest):
            raise ValueError(f"Dependency source cache checksum/size mismatch: {path}")
    if not {"source", "recipe"} <= roles:
        raise ValueError("Complete dependency sources require source bytes and a recipe")
    authentication = record.get("authentication")
    if not isinstance(authentication, dict) or not authentication:
        raise ValueError("Missing dependency source authentication")
    manifest = [{field: material[field] for field in ("path", "sha256", "size", "role")}
                for material in materials]
    locked = authentication.get("required_materials")
    if (not isinstance(locked, list) or sorted(locked, key=lambda item: item["path"])
            != sorted(manifest, key=lambda item: item["path"])):
        raise ValueError("Dependency source inventory omits required recipe material")
    if expected["manager"] == "pacman":
        digest = expected.get("pkgbuild_sha256")
        recipes = [item for item in materials if item["role"] == "recipe"
                   and PurePosixPath(item["path"]).name == "PKGBUILD"]
        if (not digest or authentication.get("pkgbuild_sha256") != digest
                or not any(item["sha256"] == digest for item in recipes)):
            raise ValueError("Pacman source recipe is not bound to authenticated binary BUILDINFO")
        sourceinfo = [item for item in materials if item["role"] == "recipe"
                      and PurePosixPath(item["path"]).name == ".SRCINFO"]
        if len(sourceinfo) != 1:
            raise ValueError("Pacman recipe requires its complete retained source declarations")
        text = _safe_file(root, sourceinfo[0]["path"]).read_text(encoding="utf-8")
        declared = re.findall(r"(?m)^\s*source(?:_[A-Za-z0-9_]+)?\s*=\s*(.+)$", text)
        sources = authentication.get("recipe_sources")
        if (not isinstance(sources, list) or not sources
                or sorted(item.get("sourceinfo_source", item.get("declared_source", "")) for item in sources)
                != sorted(declared)):
            raise ValueError("Pacman source inventory does not cover every recipe declaration")
        by_path = {item["path"]: item for item in materials}
        for item in sources:
            material = by_path.get(item.get("material_path"))
            if material is None or any(material.get(field) != item.get(field)
                                       for field in ("sha256", "size", "role")):
                raise ValueError("Pacman recipe source/patch binding mismatch")
        _recipe_checksums(root, text, sources)
        for item in authentication.get("extra_required_patches", []):
            path = item if isinstance(item, str) else item.get("material_path", item.get("path"))
            if path not in by_path or by_path[path]["role"] != "patch":
                raise ValueError("Pacman prepare patch is absent from complete materials")
    elif expected["manager"] == "dpkg":
        recipes = [item for item in materials if item["role"] == "recipe"
                   and item["path"].endswith(".dsc")]
        if (authentication.get("method") != "apt-authenticated-source-index"
                or not any(item["sha256"] == authentication.get("descriptor_sha256")
                           for item in recipes)
                or not any(item["sha256"] == authentication.get("source_index_sha256")
                           for item in materials if item["role"] == "configuration")):
            raise ValueError("Debian source descriptor has no authenticated source-index binding")
        indices = [item for item in materials if item["role"] == "configuration"
                   and item["sha256"] == authentication["source_index_sha256"]]
        if len(indices) != 1 or len(recipes) != 1:
            raise ValueError("Ambiguous Debian source descriptor/index proof")
        stanzas = [item for item in _stanzas(_safe_file(root, indices[0]["path"]).read_text(encoding="utf-8"))
                   if item.get("Package") == expected["source_package"]
                   and item.get("Version") == expected["source_version"]]
        if len(stanzas) != 1:
            raise ValueError("Authenticated apt source index omits exact source identity")
        checksums = _checksums(stanzas[0], "Checksums-Sha256")
        archives = {PurePosixPath(item["path"]).name: (item["sha256"], item["size"])
                    for item in materials if item["role"] in {"source", "recipe", "patch"}}
        if checksums != archives:
            raise ValueError("Debian source inventory omits authenticated source/recipe/patch bytes")
    else:
        raise ValueError("Unsupported dependency source package manager")
    if not isinstance(record.get("instructions"), str) or not record["instructions"].strip():
        raise ValueError("Dependency sources require build/install instructions")


def _recipe_checksums(root: Path, sourceinfo: str, sources: list[dict]) -> None:
    """Check all declared strong hashes; VCS pins hash makepkg's uncompressed tar."""
    fields = {}
    for match in re.finditer(r"(?m)^\s*(source|sha256sums|sha512sums|b2sums)(_[A-Za-z0-9_]+)?\s*=\s*(.+)$",
                             sourceinfo):
        fields.setdefault((match[1], match[2] or ""), []).append(match[3])
    pending = list(sources)
    for (field, architecture), declared in fields.items():
        if field != "source":
            continue
        for index, declaration in enumerate(declared):
            matches = [item for item in pending
                       if item.get("sourceinfo_source", item.get("declared_source")) == declaration]
            if not matches:
                raise ValueError("Missing declared recipe source checksum binding")
            item = matches[0]
            pending.remove(item)
            checksum_found = False
            for checksum_field, algorithm in (("sha256sums", "sha256"), ("sha512sums", "sha512"),
                                               ("b2sums", "blake2b")):
                pins = fields.get((checksum_field, architecture))
                if pins is None:
                    continue
                if len(pins) != len(declared):
                    raise ValueError("Recipe source/checksum array length mismatch")
                pin = pins[index]
                if pin == "SKIP":
                    continue
                checksum_found = True
                path = _safe_file(root, item["material_path"])
                vcs = bool(item.get("git_commit") or re.search(r"git\+|\.git(?:[#?]|$)", declaration))
                with (gzip.open(path, "rb") if vcs else path.open("rb")) as stream:
                    actual = hashlib.file_digest(stream, algorithm).hexdigest()
                if actual != pin:
                    raise ValueError(f"Recipe source checksum mismatch: {declaration}")
            if not checksum_found and item["role"] == "source":
                commit = item.get("git_commit", "")
                canonical = item.get("canonical_archive_sha256", "")
                if (not re.fullmatch(r"[a-f0-9]{40}", commit)
                        or not re.fullmatch(r"[a-f0-9]{64}", canonical)):
                    raise ValueError("Unchecksummed source lacks an exact VCS commit/archive binding")
                if "#commit=" in declaration and declaration.split("#commit=", 1)[1].split("?")[0] != commit:
                    raise ValueError("VCS source commit disagrees with the authenticated recipe")
                with gzip.open(_safe_file(root, item["material_path"]), "rb") as stream:
                    if hashlib.file_digest(stream, "sha256").hexdigest() != canonical:
                        raise ValueError("VCS source archive differs from its exact canonical commit snapshot")


def _run(command: list[str], *, cwd: Path | None = None) -> str:
    environment = dict(os.environ, LC_ALL="C")
    return subprocess.run(command, cwd=cwd, env=environment, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True).stdout


def _stanzas(text: str) -> list[dict[str, str]]:
    result = []
    for block in re.split(r"\n\s*\n", text):
        fields, field = {}, None
        for line in block.splitlines():
            if line.startswith((" ", "\t")) and field:
                fields[field] += "\n" + line.strip()
            elif ": " in line or line.endswith(":"):
                field, _, value = line.partition(":")
                if field in fields:
                    raise ValueError("Duplicate Debian control field")
                fields[field] = value.strip()
        if fields:
            result.append(fields)
    return result


def _checksums(stanza: dict, field: str) -> dict[str, tuple[str, int]]:
    result = {}
    for line in stanza.get(field, "").splitlines():
        if not line.strip():
            continue
        parts = line.split()
        if (len(parts) != 3 or not re.fullmatch(r"[a-f0-9]{64}", parts[0])
                or not parts[1].isdigit() or int(parts[1]) <= 0
                or parts[2] in result):
            raise ValueError("Invalid authenticated source checksum descriptor")
        result[parts[2]] = (parts[0], int(parts[1]))
    if not result:
        raise ValueError("Missing SHA-256 source checksum descriptor")
    return result


def _https(url: str) -> str:
    parsed = urlsplit(url)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc or parsed.username or parsed.password:
        raise ValueError("APT source origin is not an HTTP(S) repository")
    return urlunsplit(parsed._replace(scheme="https"))



def _directory(root: Path, directory: Path) -> None:
    relative = directory.relative_to(root)
    for path in (root, *(root / parent for parent in reversed(relative.parents)), directory):
        path.mkdir(exist_ok=True)
        if not stat.S_ISDIR(path.lstat().st_mode):
            raise ValueError(f"Dependency source cache parent is not a real directory: {path}")

def _store(root: Path, relative: str, data: bytes, url: str, role: str) -> dict:
    destination = root / relative
    _directory(root, destination.parent)
    digest = hashlib.sha256(data).hexdigest()
    if destination.exists() or destination.is_symlink():
        path = _safe_file(root, relative)
        if path.stat().st_size != len(data) or _sha256(path) != digest:
            raise ValueError(f"Dependency source cache collision: {relative}")
    else:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=".material-") as stream:
            stream.write(data)
            stream.flush()
            os.link(stream.name, destination)
    return {"path": relative, "sha256": digest, "size": len(data), "url": url, "role": role}


def _ubuntu(root: Path, expected: dict) -> dict:
    if expected["distribution"]["id"] != "ubuntu":
        raise ValueError("Automatic apt source acquisition supports Ubuntu archive identities only")
    package, version = expected["source_package"], expected["source_version"]
    if not re.fullmatch(r"[a-z0-9][a-z0-9+.-]*", package) or not re.fullmatch(r"[A-Za-z0-9.+:~_-]+", version):
        raise ValueError("Unsafe apt source package/version")
    selection = f"{package}={version}"
    command = ["apt-get", *APT_OPTIONS, "source", "--print-uris", "--download-only", "--only-source", selection]
    uri_output = _run(command)
    uris = {}
    for line in uri_output.splitlines():
        if line.startswith("'"):
            parts = shlex.split(line)
            if len(parts) != 4 or parts[1] in uris or "/" in parts[1] or not parts[2].isdigit():
                raise ValueError("Invalid apt source download descriptor")
            uris[parts[1]] = (_https(parts[0]), int(parts[2]))
    if not uris:
        raise ValueError(f"Exact apt source revision unavailable: {selection}")
    targets = _stanzas(_run(["apt-get", *APT_OPTIONS, "indextargets"]))
    keyring = Path("/usr/share/keyrings/ubuntu-archive-keyring.gpg")
    if not keyring.is_file() or keyring.is_symlink():
        raise ValueError("Authentic Ubuntu archive keyring is unavailable")
    selected = None
    for target in targets:
        if target.get("Created-By") != "Sources" or target.get("Trusted") != "yes":
            continue
        index = Path(target["Filename"])
        sources = _run(["/usr/lib/apt/apt-helper", "cat-file", str(index)])
        matches = [item for item in _stanzas(sources)
                   if item.get("Package") == package and item.get("Version") == version]
        if not matches:
            continue
        if len(matches) != 1:
            raise ValueError("Ambiguous exact apt source index stanza")
        checksums = _checksums(matches[0], "Checksums-Sha256")
        if set(checksums) != set(uris):
            continue
        metakey = target.get("MetaKey", "")
        if not metakey.endswith("/source/Sources"):
            raise ValueError("Unexpected apt source index identity")
        suffix = "_" + metakey.replace("/", "_")
        filename = str(index)
        for compression in (".lz4", ".xz", ".gz", ".bz2"):
            if filename.endswith(compression):
                filename = filename[:-len(compression)]
                break
        if not filename.endswith(suffix):
            raise ValueError("Cannot bind apt index to its signed release")
        release = Path(filename[:-len(suffix)] + "_InRelease")
        status = _run(["gpgv", "--status-fd", "1", "--keyring", str(keyring), str(release)])
        fingerprints = re.findall(r"^\[GNUPG:\] VALIDSIG ([A-Fa-f0-9]+) ", status, re.M)
        if not fingerprints:
            raise ValueError("APT source release has no valid Ubuntu archive signature")
        release_text = release.read_text(encoding="utf-8")
        signed = release_text.split("\n\n", 1)[1].split("-----BEGIN PGP SIGNATURE-----", 1)[0]
        signed = re.sub(r"(?m)^- ", "", signed)
        release_fields = _stanzas(signed)[0]
        release_hashes = _checksums(release_fields, "SHA256")
        source_bytes = sources.encode("utf-8")
        if release_hashes.get(metakey) != (hashlib.sha256(source_bytes).hexdigest(), len(source_bytes)):
            raise ValueError("APT Sources index does not match the signed release")
        if release_fields.get("Origin") != "Ubuntu":
            raise ValueError("Source index is not from the Ubuntu archive")
        selected = (target, matches[0], checksums, source_bytes, release, fingerprints)
        break
    if selected is None:
        raise ValueError(f"No authenticated source index covers {selection}; enable deb-src and update apt")
    target, stanza, checksums, sources, release, fingerprints = selected
    label = package + "-" + version.replace(":", "_")
    materials = []
    for name, (digest, size) in sorted(checksums.items()):
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+~_-]*", name):
            raise ValueError("Unsafe apt source archive filename")
        url, uri_size = uris[name]
        if uri_size != size:
            raise ValueError("APT download descriptor size differs from authenticated index")
        directory = root / "apt" / label
        _directory(root, directory)
        # APT supplies the exact download plan; authenticated Sources pins HTTPS bytes.
        path = _download(directory, name, url, digest)
        if path.stat().st_size != size:
            raise ValueError(f"APT source differs from authenticated index: {name}")
        materials.append({"path": path.relative_to(root).as_posix(), "sha256": digest,
                          "size": size, "url": url,
                          "role": "recipe" if name.endswith(".dsc") else "source"})
    descriptors = [item for item in materials if item["path"].endswith(".dsc")]
    if len(descriptors) != 1:
        raise ValueError("Exact apt source must have one retained .dsc descriptor")
    descriptor = _safe_file(root, descriptors[0]["path"]).read_text(encoding="utf-8")
    if descriptor.startswith("-----BEGIN PGP SIGNED MESSAGE-----"):
        descriptor = descriptor.split("\n\n", 1)[1].split("-----BEGIN PGP SIGNATURE-----", 1)[0]
        descriptor = re.sub(r"(?m)^- ", "", descriptor)
    fields = _stanzas(descriptor)[0]
    if fields.get("Source") != package or fields.get("Version") != version:
        raise ValueError("APT source descriptor identity mismatch")
    archives = {name: value for name, value in checksums.items() if not name.endswith(".dsc")}
    if _checksums(fields, "Checksums-Sha256") != archives:
        raise ValueError("APT source descriptor archive checksums do not match authenticated index")
    proof = "apt/proof/" + hashlib.sha256(sources).hexdigest()
    index_url = _https(target["URI"])
    index_material = _store(root, proof + "/Sources", sources, index_url, "configuration")
    base = index_url.rsplit("/" + target["MetaKey"], 1)[0]
    materials.extend([index_material,
                      _store(root, proof + "/InRelease", release.read_bytes(), base + "/InRelease", "signature"),
                      _store(root, proof + "/ubuntu-archive-keyring.gpg", keyring.read_bytes(),
                             "https://archive.ubuntu.com/ubuntu/", "key")])
    return {**{field: expected[field] for field in IDENTITY}, "materials": materials,
            "authentication": {"method": "apt-authenticated-source-index",
                               "required_materials": [
                                   {field: item[field] for field in ("path", "sha256", "size", "role")}
                                   for item in materials],
                               "descriptor_sha256": descriptors[0]["sha256"],
                               "source_index_sha256": index_material["sha256"],
                               "release_sha256": materials[-2]["sha256"],
                               "keyring_sha256": materials[-1]["sha256"],
                               "keyring_origin": "Installed Ubuntu archive trust anchor; key material URL names its repository origin",
                               "valid_signatures": fingerprints,
                               "source_index_metakey": target["MetaKey"],
                               "source_index_stanza": stanza,
                               "command": command},
            "instructions": ("Verify InRelease using the retained Ubuntu archive keyring; verify its SHA256 "
                             "Sources entry, then the exact package/version Sources stanza and .dsc archive "
                             "checksums. Use dpkg-source -x on the retained .dsc; install its declared "
                             "Build-Depends and run dpkg-buildpackage -b -uc -us. The Debian source archives "
                             "include the complete packaging recipe, patches, configuration and notices.")}


def acquire(bom: dict, tools_dir: Path) -> None:
    """Ensure exact BOM-selected source records exist; never enumerate installed packages."""
    required = required_packages(bom)
    if not required:
        return
    tools_dir = Path(tools_dir)
    tools_dir.mkdir(parents=True, exist_ok=True)
    if not stat.S_ISDIR(tools_dir.lstat().st_mode):
        raise ValueError("Tool cache root must be a real directory")
    root = tools_dir.resolve() / "dependency-sources"
    root.mkdir(exist_ok=True)
    if not stat.S_ISDIR(root.lstat().st_mode):
        raise ValueError("Dependency source cache must be a real directory")
    inventory_path = root / "provenance.json"
    if inventory_path.exists() or inventory_path.is_symlink():
        inventory_path = _safe_file(root, "provenance.json")
        inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
        if inventory.get("format") != 1 or not isinstance(inventory.get("packages"), list):
            raise ValueError("Invalid dependency source inventory")
    else:
        inventory = {"format": 1, "packages": []}
    changed = False
    for expected in required:
        matches = [item for item in inventory["packages"] if all(
            item.get(field) == expected[field] for field in ("manager", "package", "version"))]
        if len(matches) > 1:
            raise ValueError("Duplicate dependency source package identity")
        if matches:
            _validate(root, matches[0], expected)
            continue
        if expected["manager"] != "dpkg":
            raise ValueError(f"Missing preacquired exact pacman sources: {expected['package']} {expected['version']}")
        record = _ubuntu(root, expected)
        _validate(root, record, expected)
        inventory["packages"].append(record)
        changed = True
    if changed:
        data = (json.dumps(inventory, indent=2, sort_keys=True) + "\n").encode("utf-8")
        with tempfile.NamedTemporaryFile(dir=root, prefix=".provenance-", delete=False) as stream:
            temporary = Path(stream.name)
            try:
                stream.write(data)
                stream.flush()
                os.replace(temporary, inventory_path)
            finally:
                temporary.unlink(missing_ok=True)
