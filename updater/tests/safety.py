#!/usr/bin/env python3
"""Offline real-process security regression; no network or game assets required."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile


def run(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--updater', required=True)
    args = parser.parse_args()
    updater = str(Path(args.updater).resolve())
    with tempfile.TemporaryDirectory(prefix='c4-update-safety-') as temp:
        base = Path(temp)
        key, public = base / 'private.pem', base / 'public.pem'
        run('openssl', 'genpkey', '-algorithm', 'ED25519', '-out', str(key))
        run('openssl', 'pkey', '-in', str(key), '-pubout', '-out', str(public))
        root = base / 'installed'
        config = base / 'config.json'
        config.write_text(json.dumps(dict(repository='offline/fixture', trusted_key=str(public), install_root=str(root), channel='stable')))
        payload = {f'bin/{name}': b'not executed: test payload\n' for name in ('citroen-c4-robot', 'c4-launcher', 'c4-updater')}
        runtime_payload = {
            'bin/qt.conf': b'[Paths]\nPrefix=..\nPlugins=plugins\n',
            'tools/innoextract': b'not executed: bundled extraction tool\n',
            'runtime/bin/python3': b'not executed: bundled Python interpreter\n',
            'share/citroen-c4-robot/runtime-manifest.json': b'{"schema_version":1}\n',
            'share/citroen-c4-robot/tools/original-assets.json': b'{"files":[]}\n',
            'lib/libQt6Core.so.6': b'not executed: bundled Qt library\n',
            'lib/libQt6Concurrent.so.6': b'not executed: bundled Qt Concurrent library\n',
            'lib/libvulkan.so.1': b'not executed: bundled Vulkan loader\n',
        }
        policy = json.loads((Path(__file__).resolve().parents[2] / 'packaging/runtime_policy.json').read_text())
        runtime_payload.update({
            name: b'not executed: bundled plugin ' + name.encode() + b'\n'
            for name in policy['plugins']
        })
        unauthorized_paths = {
            'unauthorized-lib': 'lib/evil.so',
            'unauthorized-qt-module': 'lib/libQt6Qml.so.6',
            'unauthorized-runtime': 'runtime/lib/evil.py',
            'unauthorized-catalog-neighbor': 'share/citroen-c4-robot/tools/evil.json',
            'unauthorized-gpu-icd': 'share/vulkan/icd.d/evil.json',
        }
        for group in sorted({str(Path(name).parent) for name in policy['plugins']}):
            unauthorized_paths['unauthorized-' + Path(group).name] = group + '/evil.so'
        shader_prefix = 'share/citroen-c4-robot/shaders/vulkan/'
        runtime_payload.update({
            shader_prefix + name: b'not executed: signed shader fixture ' + name.encode() + b'\n'
            for name in ('raster.vert.spv', 'raster.frag.spv', 'fullscreen.vert.spv', 'compose.frag.spv', 'rt.comp.spv', 'temporal.comp.spv')
        })
        executable_paths = set(payload) | {'tools/innoextract', 'runtime/bin/python3'}

        def release(version, attack=None, bad_signature=False, runtime=False):
            directory = base / f'release-{version}-{attack or "ok"}-{bad_signature}'
            directory.mkdir()
            package = directory / 'native-linux-x86_64.tar.gz'
            files = dict(payload)
            if runtime:
                files.update(runtime_payload)
            if attack == 'partial-shaders':
                del files[shader_prefix + 'temporal.comp.spv']
            if attack in unauthorized_paths:
                files[unauthorized_paths[attack]] = b'signed but unauthorized runtime file\n'
            inventory = dict(files)
            if attack in ('traversal', 'absolute', 'backslash', 'unlisted'):
                files[{'traversal': '../escape', 'absolute': '/tmp/c4-escape', 'backslash': 'share\\escape', 'unlisted': 'unexpected'}[attack]] = b'evil'
            with tarfile.open(package, 'w:gz') as archive:
                for name, data in files.items():
                    entry = tarfile.TarInfo(name)
                    entry.mode = 0o777  # Signed archives cannot grant arbitrary permissions.
                    entry.size = len(data)
                    if attack == 'symlink' and name.endswith('c4-updater'):
                        entry.type, entry.linkname, entry.size = tarfile.SYMTYPE, '/tmp/escape', 0
                        archive.addfile(entry)
                    elif attack == 'hardlink' and name.endswith('c4-updater'):
                        entry.type, entry.linkname, entry.size = tarfile.LNKTYPE, 'bin/c4-launcher', 0
                        archive.addfile(entry)
                    elif attack == 'special' and name.endswith('c4-updater'):
                        entry.type, entry.size = tarfile.FIFOTYPE, 0
                        archive.addfile(entry)
                    else:
                        archive.addfile(entry, io.BytesIO(data))
                        if attack == 'duplicate' and name.endswith('c4-updater'):
                            archive.addfile(entry, io.BytesIO(data))
            content = package.read_bytes()
            manifest = dict(schema_version=1, version=version, os='linux', arch='x86_64', package=dict(name=package.name, size=len(content), sha256=hashlib.sha256(content).hexdigest()), files=[dict(path=name, size=len(data), sha256=hashlib.sha256(data).hexdigest()) for name, data in inventory.items()])
            if attack == 'oversize':
                manifest['files'][0]['size'] = 1024 ** 3 + 1
            if attack == 'hash':
                manifest['files'][0]['sha256'] = '0' * 64
            metadata = directory / 'manifest.json'
            metadata.write_text(json.dumps(manifest))
            run('openssl', 'pkeyutl', '-sign', '-rawin', '-inkey', str(key), '-in', str(metadata), '-out', str(directory / 'manifest.sig'))
            if bad_signature:
                (directory / 'manifest.sig').write_bytes(b'0' * 64)
            return directory

        def invoke(action, directory=None, success=True, events=None):
            command = [updater, '--config', str(config), action]
            if directory:
                command += ['--release-dir', str(directory)]
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            answer = json.loads(result.stdout)
            assert (result.returncode == 0) == success, (command, result.returncode, answer, result.stderr)
            if events is not None:
                events.extend(json.loads(line) for line in result.stderr.splitlines())
            return answer

        first = release('1.0.0')
        assert invoke('--check', first)['update_available']
        invoke('--install', first)
        assert root.joinpath('current').readlink() == Path('versions/1.0.0')
        assert not invoke('--check', first)['update_available']
        original = root.joinpath('current/bin/c4-updater').read_bytes()
        for name, data in payload.items():
            installed = root / 'current' / name
            assert installed.read_bytes() == data
            assert installed.stat().st_mode & 0o7777 == 0o755
        for attack in ('traversal', 'absolute', 'backslash', 'symlink', 'hardlink', 'special', 'duplicate', 'oversize', 'hash', 'unlisted'):
            invoke('--install', release('2.0.0', attack), success=False)
            assert root.joinpath('current').readlink() == Path('versions/1.0.0')
            assert root.joinpath('current/bin/c4-updater').read_bytes() == original
        for attack in (*unauthorized_paths, 'partial-shaders'):
            directory = release('2.0.0', attack, runtime=True)
            invoke('--check', directory, success=False)
            invoke('--install', directory, success=False)
            assert root.joinpath('current').readlink() == Path('versions/1.0.0')
            assert root.joinpath('current/bin/c4-updater').read_bytes() == original
            assert not root.joinpath('versions/2.0.0').exists()
        invoke('--install', release('2.0.0', bad_signature=True), success=False)
        invoke('--check', release('01.0.0'), success=False)
        invoke('--check', release('2.0.0-alpha.1'), success=False)
        second = release('2.0.0', runtime=True)
        events = []
        downloaded = invoke('--download', second, events=events)
        candidate = root / 'versions/2.0.0'
        assert downloaded == dict(version='2.0.0', download_path=str(candidate.resolve()),
                                  downloaded=True, already_downloaded=False)
        assert root.joinpath('current').readlink() == Path('versions/1.0.0')
        assert not root.joinpath('previous').exists()
        assert root.joinpath('current/bin/c4-updater').read_bytes() == original
        assert candidate.joinpath('.c4-manifest.json').read_bytes() == second.joinpath('manifest.json').read_bytes()
        assert candidate.joinpath('.c4-manifest.sig').read_bytes() == second.joinpath('manifest.sig').read_bytes()
        for name, data in (payload | runtime_payload).items():
            assert candidate.joinpath(name).read_bytes() == data
            expected_mode = 0o755 if name in executable_paths else 0o644
            assert candidate.joinpath(name).stat().st_mode & 0o7777 == expected_mode, name
        package_size = second.joinpath('native-linux-x86_64.tar.gz').stat().st_size
        progress = [event for event in events if event['event'] == 'download_progress']
        assert progress[0]['received_bytes'] == 0
        assert progress[-1]['received_bytes'] == package_size
        assert all(event['total_bytes'] == package_size for event in progress)
        assert all(0 <= a['received_bytes'] <= b['received_bytes'] <= package_size
                   for a, b in zip(progress, progress[1:]))
        assert [event['stage'] for event in events if event['event'] == 'update_stage'] == ['verify', 'extract', 'preserve']
        # Reuse authenticates the full cached tree and exact signed metadata, not an archive or marker.
        second.joinpath('native-linux-x86_64.tar.gz').unlink()
        events = []
        assert invoke('--download', second, events=events)['already_downloaded'] is True
        assert not any(event['event'] == 'download_progress' for event in events)
        cached_executable = candidate / 'bin/c4-updater'
        cached_executable.write_bytes(b'corrupted inactive candidate')
        for action in ('--download', '--install'):
            invoke(action, second, success=False)
            assert root.joinpath('current').readlink() == Path('versions/1.0.0')
            assert not root.joinpath('previous').exists()
            assert root.joinpath('current/bin/c4-updater').read_bytes() == original
        cached_executable.write_bytes(payload['bin/c4-updater'])
        # Another valid signature for this version still cannot replace a preserved manifest.
        alternate = release('2.0.0', 'alternate', runtime=True)
        metadata = alternate / 'manifest.json'
        metadata.write_text(metadata.read_text() + '\n')
        run('openssl', 'pkeyutl', '-sign', '-rawin', '-inkey', str(key), '-in', str(metadata),
            '-out', str(alternate / 'manifest.sig'))
        for action in ('--download', '--install'):
            invoke(action, alternate, success=False)
            assert root.joinpath('current').readlink() == Path('versions/1.0.0')
            assert not root.joinpath('previous').exists()
        events = []
        invoke('--install', second, events=events)
        assert not any(event['event'] == 'download_progress' for event in events)
        assert root.joinpath('previous').readlink() == Path('versions/1.0.0')
        assert root.joinpath('current').readlink() == Path('versions/2.0.0')
        # Same-version reuse and refused downgrade also leave both activation links intact.
        assert invoke('--download', second)['already_downloaded'] is True
        assert invoke('--install', second)['updated'] is False
        invoke('--download', first, success=False)
        assert root.joinpath('current').readlink() == Path('versions/2.0.0')
        assert root.joinpath('previous').readlink() == Path('versions/1.0.0')
        for name, data in (payload | runtime_payload).items():
            installed = root / 'current' / name
            assert installed.read_bytes() == data
            expected_mode = 0o755 if name in executable_paths else 0o644
            assert installed.stat().st_mode & 0o7777 == expected_mode, name
        verified = invoke('--verify-current')
        assert verified['verified'] is True
        assert verified['version'] == '2.0.0'
        assert Path(verified['install_path']).resolve() == root.joinpath('versions/2.0.0').resolve()
        extra = root / 'current' / 'unlisted'
        extra.write_bytes(b'not in the signed inventory')
        invoke('--verify-current', success=False)
        extra.unlink()
        extra.mkdir()
        invoke('--verify-current', success=False)
        extra.rmdir()
        extra.symlink_to('bin/c4-updater')
        invoke('--verify-current', success=False)
        extra.unlink()
        os.mkfifo(extra)
        invoke('--verify-current', success=False)
        extra.unlink()
        executable = root / 'current' / 'bin/c4-updater'
        executable.chmod(0o644)
        invoke('--verify-current', success=False)
        executable.chmod(0o755)
        assert invoke('--verify-current')['verified'] is True
        assert root.joinpath('versions/1.0.0/bin/c4-updater').exists()
        invoke('--rollback')
        assert root.joinpath('current').readlink() == Path('versions/1.0.0')
        invoke('--rollback')
        assert root.joinpath('current').readlink() == Path('versions/2.0.0')
        # Simulate power loss after the atomic current rename, before previous repair.
        root.joinpath('previous').unlink()
        root.joinpath('previous').symlink_to('versions/2.0.0')
        root.joinpath('.activation.json').write_text(json.dumps({'from': '1.0.0', 'to': '2.0.0'}))
        invoke('--check', second)
        assert root.joinpath('previous').readlink() == Path('versions/1.0.0')
        assert not root.joinpath('.activation.json').exists()
        # Rollback must recover from a damaged active payload, not require it to work.
        root.joinpath('current/bin/c4-updater').write_bytes(b'corrupted')
        invoke('--check', second, success=False)
        invoke('--verify-current', success=False)
        invoke('--rollback')
        assert root.joinpath('current').readlink() == Path('versions/1.0.0')
        invoke('--rollback', success=False)  # damaged preserved payload may never activate
        # Existing unrelated files at managed link names must never be overwritten.
        root.joinpath('previous').unlink()
        root.joinpath('previous').write_bytes(b'preserve unrelated file')
        third = release('3.0.0')
        invoke('--install', third, success=False)
        assert root.joinpath('previous').read_bytes() == b'preserve unrelated file'
        assert root.joinpath('current').readlink() == Path('versions/1.0.0')
        # Reuse only a completely verified identical preserved tree after interruption.
        root.joinpath('previous').unlink()
        invoke('--install', third)
        assert root.joinpath('current').readlink() == Path('versions/3.0.0')
        # A trust anchor must not move or be replaced with the managed version.
        managed_key = root / 'managed-key.pem'
        managed_key.write_bytes(public.read_bytes())
        key_alias = base / 'key-alias.pem'
        key_alias.symlink_to(managed_key)
        valid_config = json.loads(config.read_text())
        for unsafe_key in (managed_key, key_alias):
            config.write_text(json.dumps({**valid_config, 'trusted_key': str(unsafe_key)}))
            invoke('--check', third, success=False)
            assert root.joinpath('current').readlink() == Path('versions/3.0.0')
        config.write_text(json.dumps(dict(repository='', trusted_key='', install_root=str(root), channel='stable')))
        invoke('--check', first, success=False)
    print('secure updater offline signature/archive/download/cache/install/rollback checks passed')


if __name__ == '__main__':
    main()
