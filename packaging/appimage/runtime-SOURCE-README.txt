Native dependencies and AppImage runtime: source and relinking materials
======================================================================

Inventory scope comes from the companion's dependencies/runtime-manifest.json.
The AppImage runtime instructions below apply only when runtime-sources.lock.json
and its runtime archives are present. Native-tar-only companions omit those
nondeployed runtime/libfuse materials; skip their verification/rebuild sections,
and use the deployed-dependency section plus optional FFmpeg/D-Bus instructions.

Runtime upstream: https://github.com/AppImage/type2-runtime
Supplied runtime source commit:
  8f39b89e2ac31e1640b3d3f7e9a5108e6ce805fa
  https://github.com/AppImage/type2-runtime/tree/8f39b89e2ac31e1640b3d3f7e9a5108e6ce805fa
Official runtime-x86_64 binary SHA-256:
  156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074

This distribution retains the actual complete upstream source archives,
including source headers, notices, build scripts, linker script, and the
runtime's actual libfuse patch. It contains no original game installer,
original game assets, or private signing keys. Archives are supplied unchanged;
the enclosing distribution's timestamps, owner and modes are normalized.
The lock records archive checksums, URLs, source identities and trust scope.
notices/ contains the enumerated byte-exact accompanying license texts.
Additional file-level copyright notices and alternative licenses remain in
the complete source archives: retain those when modifying/distributing source.

Source identity and limits
-------------------------

libfuse 3.15.0 and squashfuse 0.5.2 are the actual SHA-256 pins enforced by
scripts/common/install-dependencies.sh in the supplied runtime commit.
libfuse must additionally receive patches/libfuse/mount.c.diff from that
runtime tree. The source below is sufficient to rebuild/relink the runtime
against a modified libfuse; no proprietary application object is linked into
the runtime. The application SquashFS is appended to the runtime separately.

The Docker build uses alpine:3.21 without an image digest and unversioned
apk add. Upstream did not pin exact musl, zstd, zlib or mimalloc package
revisions. Complete upstream source snapshots for musl 1.2.5, zstd 1.5.6,
zlib 1.3.2 and mimalloc 2.1.7 are supplied, together with the complete Alpine
aports tree at b82a90a26fc0965ffd03cca90dfb1daed17101cc, the dated 3.21-stable
reference immediately preceding the official build. This reference includes
actual recipes and their local patch files:
  main/musl/          (reference 1.2.5-r11)
  main/zstd/          (reference 1.5.6-r2)
  main/zlib/          (reference 1.3.2-r0)
  community/mimalloc2/ (reference 2.1.7-r0, secure allocator)
Those are source/patch references, NOT evidence of the versions installed in
the official release container. Exact original distro patch revisions require
the original APK manifest/build logs or equivalent provenance. We do not
claim byte-identical reproduction of the official binary, nor that the source
snapshots establish that unknown distro closure. The official release job
logs were not publicly obtainable during source acquisition.

Acquired GitHub commit archives are checksum-pinned snapshots obtained over
HTTPS, not independently signed source releases. musl's upstream release
archive additionally matched the SHA-512 in the dated Alpine recipe. libfuse
and squashfuse matched upstream runtime's explicit SHA-256 pins. No archive
source code was executed during acquisition.

Unpack and verify every supplied archive
---------------------------------------

In a new empty working directory, unpack this distribution and then enter:
  cd runtime-corresponding-source

Verify before extraction (Python 3.11+; this only reads source data):
  python3 - <<'PY'
  import hashlib, json
  from pathlib import Path
  lock = json.loads(Path('runtime-sources.lock.json').read_text())
  for entry in lock['sources']:
      path = Path('archives') / entry['name']
      with path.open('rb') as stream:
          actual = hashlib.file_digest(stream, 'sha256').hexdigest()
      if actual != entry['sha256']:
          raise SystemExit('Checksum mismatch: ' + str(path))
      print(actual, path)
  for entry in lock['notices']:
      path = Path('notices') / entry['path']
      if hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
          raise SystemExit('Notice checksum mismatch: ' + str(path))
  PY

Extract ALL the archives in an isolated disposable directory, not over an
existing checkout or system filesystem:
  mkdir source
  for archive in archives/*.tar.gz archives/*.tar.xz; do
      tar --no-same-owner -xf "$archive" -C source
  done

The source lock's top_directory fields give the exact extracted root names.
No Git checkout, submodule fetch, or moving tag resolution is necessary.
The runtime archive contains scripts/, patches/, src/runtime/Makefile,
src/runtime/data_sections.ld, src/runtime/version and BUILD.md. The archive
has the actual scripts/docker/ paths; some examples in upstream BUILD.md
omit that directory and are stale.

Original upstream Docker/make pipeline
-------------------------------------

With Docker and an appropriate toolchain/base image already available:
  cd source/type2-runtime-8f39b89e2ac31e1640b3d3f7e9a5108e6ce805fa
  env ARCH=x86_64 bash scripts/docker/build-with-docker.sh

scripts/docker/create-build-container.sh builds scripts/docker/Dockerfile
and runs scripts/build-runtime.sh. The Dockerfile installs build tools and
static dependencies; scripts/common/install-dependencies.sh downloads the
pinned libfuse/squashfuse archives, applies the runtime libfuse patch, then
builds those libraries. scripts/build-runtime.sh copies src/ to a temporary
build tree and invokes make runtime in src/runtime. The Makefile compiles
runtime.c, uses data_sections.ld, and links:
  -static -static-pie -lsquashfuse -lsquashfuse_ll -lzstd -lz -lfuse3 -lmimalloc
The script then splits debug data, strips, adds the debug link and writes
AI\002 at ELF byte offset 8. Outputs are runtime-x86_64 and its .debug file.

Running that pipeline UNMODIFIED downloads dependencies and mutable Alpine
packages: it does not guarantee use of the supplied archives. For source-
controlled rebuilding/relinking, use the following override instead.

Use supplied sources, not future dependency downloads
-----------------------------------------------------

Use a disposable Linux x86_64 Alpine/musl container or VM. Build-tool
provisioning (clang/GCC/binutils, make, pkg-config, meson/ninja, autoconf,
automake/libtool, cmake, bash and Linux headers) is separate from the runtime
library source. Pin/cache your selected base image and tools if you need a
repeatable environment. The original base/toolchain digest is unknown.
Do not install future zstd-static, zlib-static or mimalloc-dev as substitutes
for the libraries below. A container initially requires a functioning libc
and compiler; rebuild libc there, never overwrite your host system's libc.

Mount the extracted source directory at /sources in that disposable system.
The following is a source-rebuild recipe, not an assertion of a tested or
byte-identical upstream release rebuild. Run as root inside the disposable
system so library installation remains confined to it. You can modify the
libfuse source after applying the supplied patch and before its meson build.

  set -eu
  R=/sources/type2-runtime-8f39b89e2ac31e1640b3d3f7e9a5108e6ce805fa
  export CC=clang
  export CFLAGS='-Os -fPIC -ffunction-sections -fdata-sections'

  # Complete supplied libc source, not the future musl-dev archive.
  cd /sources/musl-1.2.5
  ./configure --prefix=/usr --syslibdir=/lib --enable-debug
  make -j2
  make install

  cd /sources/zlib-da607da739fa6047df13e66a2af6b8bec7c2a498
  ./configure --static --prefix=/usr
  make -j2
  make install

  cd /sources/zstd-794ea1b0afca0f020f4e57b6732332231fb23c70
  make -C lib -j2 libzstd.a
  install -m644 lib/libzstd.a /usr/lib/libzstd.a
  install -m644 lib/zstd.h lib/zdict.h lib/zstd_errors.h /usr/include/

  cd /sources/mimalloc-8c532c32c3c96e5ba1f2283e032f69ead8add00f
  cmake -S . -B local-build -G Ninja \
      -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release \
      -DMI_INSTALL_TOPLEVEL=ON -DMI_SECURE=ON \
      -DMI_BUILD_SHARED=OFF -DMI_BUILD_STATIC=ON \
      -DMI_BUILD_OBJECT=OFF -DMI_BUILD_TESTS=OFF
  cmake --build local-build -j2
  cmake --install local-build
  ln -sf libmimalloc-secure.a /usr/lib/libmimalloc.a

  cd /sources/fuse-3.15.0
  patch -p1 < "$R/patches/libfuse/mount.c.diff"
  # Apply your libfuse modifications here, before configuration/build.
  meson setup local-build --prefix=/usr --libdir=lib \
      --default-library=static -Dutils=false -Dexamples=false \
      -Dtests=false -Duseroot=false
  ninja -C local-build install

  cd /sources/squashfuse-0.5.2
  ./autogen.sh
  ./configure --prefix=/usr/local LDFLAGS='-static'
  make -j2
  make install
  install -d /usr/local/include/squashfuse
  install -m644 ./*.h /usr/local/include/squashfuse/

  cd "$R"
  bash scripts/build-runtime.sh

The recipe deliberately rebuilds supplied UPSTREAM dependency snapshots;
it does not silently claim Alpine's patches are already applied. To rebuild
the dated Alpine candidate instead, use the supplied aports APKBUILD files
and local patches: musl has several architecture/security patches, mimalloc2
has cmake-add-insecure-suffix.patch, and the recipes define installation,
flags and allocator symlink conventions. Apply the candidate's musl patches
in APKBUILD source-list order with patch -p1 before configure, and its
mimalloc patch before cmake. Review target-architecture and package prepare/
build/install details from those supplied recipes; do not fetch future
aports HEAD. zstd's recipe uses Meson, PGO and LTO, whereas the simple source
recipe above uses its ordinary static library Makefile. Choosing either
reference build cannot establish the unknown original binary closure.

For a modified-source Docker pipeline, replace the two wget operations in
scripts/common/install-dependencies.sh with copies of archives supplied here
(keeping its SHA-256 checks and patch/build commands), COPY those archives
from the Docker build context, and add source builds for the four distro
libraries in place of their apk static/development package substitutions.
Alternatively, put the source-only recipe above into your local derived
Dockerfile and mount/copy /sources. The essential cutover is that the runtime
links your newly installed .a libraries, not the old distribution archives.
The untouched supplied scripts remain available as the authoritative original
build instructions; mark your altered scripts/source when redistributing.

Relink/repackage with a modified libfuse
--------------------------------------

Rebuild libfuse, squashfuse if necessary, and the runtime using those sources.
Keep src/runtime/version or change it to identify your modified source; make
passes it as GIT_COMMIT. Running scripts/build-runtime.sh rebuilds from a fresh
copy and supplies the AppImage header magic; a bare make alone omits that
postprocessing. Retain the modified source and attribution/licenses.

Use the resulting runtime-x86_64 with your AppImage packager's documented
--runtime-file option, for example:
  appimagetool --runtime-file /absolute/path/to/runtime-x86_64 AppDir modified.AppImage
Or concatenate the processed runtime and an existing separately supplied
application SquashFS into a NEW file:
  cat runtime-x86_64 application.squashfs > modified.AppImage
  chmod +x modified.AppImage
Do not append the full old AppImage, which already contains a runtime.
Changing the runtime invalidates the distributor's old artifact signatures;
identify/sign your modified artifact separately. No original game assets
are required to rebuild the runtime itself or supplied in this source bundle.

LGPL 2.1 Section 6(a) rationale and boundary
------------------------------------------

The runtime statically links patched libfuse. Including only the LGPL notice
is insufficient. LGPL 2.1 Section 6(a) requires the complete corresponding
machine-readable library source, changes used in the work, and machine-
readable object and/or source for the work using the library so that users
can modify the library and relink. Here the work using libfuse is the MIT
runtime: its complete C source, actual patch, dependency source archives,
linker script, Makefile, build/postprocessing scripts and relink instructions
are supplied, not merely a link to future source. The source alternative
permits users to recompile/relink the runtime rather than needing proprietary
game object files. The appended application payload is separate from this
library-linked runtime. Retain the actual LGPL text in
notices/runtime-libfuse-LICENSE.txt. The actual libfuse copyright notices and
license declarations are preserved in the supplied complete source archive's
headers, LICENSE, and additional GPL/BSD texts; retain those as well.

This explains the supplied Section 6(a) source/relink route; it is not a legal
certification or a claim to have recovered unknown original distro packages.
Any remaining obligation to establish an exact original dependency patch
closure requires the original build package provenance described above.

Optional shared FFmpeg WMV3/ASF decoder
--------------------------------------

When C4_FFMPEG_PREFIX selects the authenticated decoder prefix, this same
distribution also contains ffmpeg/ (otherwise that directory is absent):
  archive/ffmpeg-8.1.3.tar.xz       complete, byte-exact upstream source
  archive/ffmpeg-8.1.3.tar.xz.asc   upstream detached release signature
  archive/ffmpeg-devel.asc         checksum-pinned upstream public key
  ffmpeg.lock.json                byte-exact source/profile/license lock
  build_ffmpeg.py                 byte-exact authored rootless build recipe
  ffmpeg-source.json              validated actual build and library evidence
  configuration/                 actual configure proof, paths normalized

FFmpeg 8.1.3 source SHA-256:
  7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3
Official release signing-key fingerprint:
  FCF986EA15E6E293A5644F10B4322F04D67658D8

The four dynamically linked libraries are libavcodec, libavformat, libavutil
and libswscale. The locked profile enables the wmv3 decoder, ASF demuxer and
local file protocol; FFmpeg selects the internal VC-1 decoder/DSP dependency.
GPL, nonfree, version3, networking, external codec autodetection, programs,
avdevice, avfilter and swresample are disabled. The shared build is covered
by LGPL-2.1-or-later, not a claim that every disabled upstream source file has
that license. Complete source copyright/license notices remain in the source
archive. Installed COPYING.LGPLv2.1, LICENSE.md, CREDITS, SOURCE-NOTICES.txt
and aggregate LICENSE.txt are checksum-locked by ffmpeg.lock.json.

ffmpeg-source.json retains actual library digests, signature provenance and
original configure-proof digests. Only declared build/cache/prefix roots and
recognized compiler/tool paths are replaced with stable labels. Each
configuration record distinguishes original_sha256 from published_sha256;
the latter authenticates the supplied path-normalized configuration file.
Normalization does not establish byte-identical rebuilt libraries or erase
different original proof/binary hashes. No source is executed by bundling.

Verify before extracting or building:
  python3 - <<'PY'
  import hashlib, json
  from pathlib import Path
  root = Path('ffmpeg')
  lock = json.loads((root / 'ffmpeg.lock.json').read_text())
  proof = lock['provenance']
  expected = {
      lock['archive']: lock['source_sha256'],
      lock['archive'] + '.asc': proof['signature_sha256'],
      'ffmpeg-devel.asc': proof['key_sha256'],
  }
  for name, digest in expected.items():
      with (root / 'archive' / name).open('rb') as stream:
          assert hashlib.file_digest(stream, 'sha256').hexdigest() == digest, name
  actual = json.loads((root / 'ffmpeg-source.json').read_text())
  for entry in actual['configuration']:
      with (root / entry['published_path']).open('rb') as stream:
          assert hashlib.file_digest(stream, 'sha256').hexdigest() == entry['published_sha256']
  PY

For independent signature verification, use a new private GnuPG home, import
only the supplied key and require VALIDSIG with the fingerprint above:
  mkdir -m700 ffmpeg-verification
  gpg --homedir "$PWD/ffmpeg-verification" --batch --no-options \
      --import ffmpeg/archive/ffmpeg-devel.asc
  gpg --homedir "$PWD/ffmpeg-verification" --batch --no-options \
      --no-auto-key-retrieve --status-fd=1 \
      --verify ffmpeg/archive/ffmpeg-8.1.3.tar.xz.asc \
               ffmpeg/archive/ffmpeg-8.1.3.tar.xz

Rebuild in an isolated Linux x86_64 working directory with Python 3.11+,
gcc, make, nasm and binutils available. Do not install into system directories.
The following uses the supplied source and exact locked profile without
downloading dependencies. Toolchain/base-system versions are separate inputs,
not a guarantee of bit-identical reproduction. Review build_ffmpeg.py for
the original controlled-environment build, notice and installation checks.
Apply your source modifications after extraction and before configuration:
  mkdir ffmpeg-work
  tar --no-same-owner -xf ffmpeg/archive/ffmpeg-8.1.3.tar.xz -C ffmpeg-work
  export FFMPEG_REBUILD_PREFIX="$PWD/ffmpeg-work/modified-prefix"
  python3 - <<'PY'
  import json, os, shutil, subprocess
  from pathlib import Path
  lock = json.loads(Path('ffmpeg/ffmpeg.lock.json').read_text())
  prefix = Path(os.environ['FFMPEG_REBUILD_PREFIX'])
  source = Path('ffmpeg-work') / lock['top_directory']
  args = ['./configure', '--prefix=' + str(prefix),
          '--libdir=' + str(prefix / 'lib'), '--shlibdir=' + str(prefix / 'lib'),
          '--pkgconfigdir=' + str(prefix / 'lib/pkgconfig'), *lock['configure_args']]
  for option, tool in [('cc', 'gcc'), ('ld', 'gcc'), ('ar', 'ar'),
                       ('ranlib', 'ranlib'), ('strip', 'strip'), ('nm', 'nm'),
                       ('x86asmexe', 'nasm')]:
      executable = shutil.which(tool)
      if not executable:
          raise SystemExit('Missing build tool: ' + tool)
      args.append('--' + option + '=' + executable)
  args.append('--pkg-config=false')
  environment = {key: os.environ[key] for key in ('PATH', 'HOME') if key in os.environ}
  environment.update(LC_ALL='C', TZ='UTC',
                     SOURCE_DATE_EPOCH=str(lock['provenance']['signature_unix_time']))
  subprocess.run(args, cwd=source, env=environment, check=True)
  subprocess.run(['make', '-j2'], cwd=source, env=environment, check=True)
  subprocess.run(['make', 'install'], cwd=source, env=environment, check=True)
  PY

Relinking/replacement uses LGPL 2.1 Section 6(b)'s suitable shared-library
mechanism. No application object is statically combined with these libraries.
Extract the AppImage into a fresh directory with --appimage-extract; replace
the four lib/libNAME.so.MAJOR.VERSION files in squashfs-root with your rebuilt
compatible libraries, retaining/recreating the matching SONAME symlinks.
The locked SONAMEs are libavcodec.so.62, libavformat.so.62, libavutil.so.60 and
libswscale.so.9. Preserve the ABI, or rebuild the application against modified
headers if your changes alter it. Replace associated library notices with
the appropriate modified notices and mark your changes. Run the extracted
AppRun to use the replaced libraries, or repackage that AppDir using
appimagetool --runtime-file as described above. Replacement does not require
original game installer/assets to build FFmpeg. A modified/repacked AppImage
does not retain the original distributor's signature; sign it separately.

Optional owned D-Bus shared library
----------------------------------

Main subsequently built the controlled owned prefix at work/tools/dbus-prefix
from authenticated official D-Bus 1.16.2 source, observed 48/48 checks, and passed
the ABI/runtime guards and nine recipe checks. This is actual prefix evidence,
not an exercised standalone rebuild from this published companion or proof of
a successful native-tar/AppImage, clean Qt installation flow, reproducibility,
complete public source offer or release. The instructions below remain separate
from those wider acceptance gates.

Only explicit C4_DBUS_PREFIX selects this rootless alternative. It overrides
ONLY libdbus-1.so.3, whose unmodified upstream file is libdbus-1.so.3.38.3;
there is no broad prefix search or replacement of other host dependencies.
Without that variable, existing Ubuntu/default distro resolution is unchanged.
It installs no daemon, helper, services, bus configuration or machine-id, and
does not change the host system/session buses.

When selected and validated for the actual deployed inventory, the source
companion includes dbus/ in either the native-tar or AppImage companion:
  archive/dbus-1.16.2.tar.xz      complete official upstream 1.16.2 source
  proof/dbus-1.16.2.tar.xz.asc    official detached release signature
  proof/dbus-release-key.asc     pinned release public key
  proof/Arch-PKGBUILD            pinned signer-authority reference ONLY
  dbus.lock.json                byte-exact source/authentication/ABI/policy lock
  build_dbus.py                 byte-exact authored rootless library-only recipe
  dbus-source.json               actual build evidence, paths normalized
  configuration/                generated configuration/build proof
  helpers/build_ffmpeg.py        byte-exact imported extraction/process helper
  helpers/appimage/acquire.py    byte-exact imported acquisition/hash helper

Use source_sha256/source_size and provenance.materials in dbus.lock.json for
the exact authenticated archive/proof hashes and sizes, not a guessed source
archive or package checksum. The signature binds signing subkey
7A073AD1AE694FA25BFF62E5235C099D3EB33076, primary fingerprint
DA98F25C0871C49A59EAFF2C4DE8FF2A63C7CC90 and signature_unix_time=1740674213.
The key includes a historical expired subkey; retain the signature's creation
time evidence. The authentic pinned Arch-PKGBUILD supplies validpgpkeys signer
authority; it is not the owned build recipe and is NOT recovered Cachy source.
The exact Cachy dbus-1.16.2-1.1 PKGBUILD SHA-256 remains unresolved:
  74e4793799261bedf67b0cbd152ed51307b1be5153180b135f75b0f0c6b0b500
The owned upstream build is an explicit alternative choice, not recovery or
byte-identical reproduction of that signed distro binary/recipe.

dbus-source.json retains built_libraries hashes, lock_sha256, recipe_sha256,
proof, meson_args, meson_options, default_bus_addresses, tool_versions and
build_environment. Its source_cache_path is archive/dbus-1.16.2.tar.xz and
prefix is the published label dbus-prefix, not evidence of an existing prefix.
configuration records retain sha256/original_sha256 for original generated
bytes and published_sha256/published_path for the supplied normalized bytes.
authentication_files records use published_path=proof/<filename>;
recipe_helpers records use published_path=helpers/<path> and the lock's sha256.
Only declared source/build/work/prefix and tool roots are normalized; exact
allowed system paths and bus constants remain. Published configuration is
evidence, not input to a future Meson build; it cannot prove identical binaries.

Authenticate the enclosing signed companion first. From runtime-corresponding-
source, the following verifies supplied bytes without extracting/building source
(Python 3.11+). These instructions have NOT been exercised in this docs update:
  python3 - <<'PY'
  import hashlib, json
  from pathlib import Path
  root = Path('dbus')
  lock = json.loads((root / 'dbus.lock.json').read_text())
  actual = json.loads((root / 'dbus-source.json').read_text())
  def verify(relative, digest, size=None):
      path = root / relative
      with path.open('rb') as stream:
          found = hashlib.file_digest(stream, 'sha256').hexdigest()
      if found != digest or (size is not None and path.stat().st_size != size):
          raise SystemExit('D-Bus material mismatch: ' + str(path))
  verify('dbus.lock.json', actual['lock_sha256'])
  verify('build_dbus.py', actual['recipe_sha256'])
  verify('archive/' + lock['archive'], lock['source_sha256'], lock['source_size'])
  for entry in lock['provenance']['materials']:
      verify('proof/' + entry['path'], entry['sha256'], entry['size'])
  for entry in lock['recipe_helpers']:
      verify('helpers/' + entry['path'], entry['sha256'])
  for entry in actual['authentication_files']:
      verify(entry['published_path'], entry['sha256'])
  for entry in actual['recipe_helpers']:
      verify(entry['published_path'], entry['sha256'])
  for entry in actual['configuration']:
      verify(entry['published_path'], entry['published_sha256'])
  PY

Repository-owner command (from the project checkout; not run here):
  python3 packaging/build_dbus.py --prefix work/tools/dbus-prefix --tools-dir work/tools --jobs 4
The parser accepts --jobs 4. Its source cache is work/tools/dbus-source.
For a standalone rebuild from this companion, use a fresh isolated Linux
x86_64 working directory with Python 3.11+, modern glibc, gcc, Meson >=1.1,
Ninja, pkg-config, X11 development metadata, binutils and gpg available.
Provisioning that toolchain/base system is separate from supplied source.
Enter the companion's dbus/ directory and seed the chosen cache with the
verified supplied archive AND proof bytes, so the recipe reuses them rather
than fetching future copies:
  cd dbus
  mkdir -p work/tools/dbus-source
  cp archive/* proof/* work/tools/dbus-source/
  export PYTHONPATH="$PWD/helpers"
  python3 - <<'PY'
  from pathlib import Path
  import build_dbus
  print(build_dbus.acquire_source(Path('work/tools')))
  PY
  python3 build_dbus.py --prefix work/tools/dbus-prefix --tools-dir work/tools --jobs 4
  export C4_DBUS_PREFIX="$PWD/work/tools/dbus-prefix"

The separate acquire_source call checks source/proof sizes and hashes, pinned
Arch validpgpkeys authority and GnuPG VALIDSIG signing/primary fingerprints and
creation time in a new private keyring with automatic key retrieval disabled.
It executes no upstream source. Missing cache bytes would trigger acquisition;
do not omit the seed copies. Both imports are hash-locked by recipe_helpers;
publishing these helpers does not enable/build the optional FFmpeg decoder.

The recipe requires a nonexistent dedicated prefix, sanitizes build environment,
locks every meson_options value and --wrap-mode=nodownload, then builds only the
real shared-library target. It copies the library/public headers/pkg-config,
notices and proof rather than running aggregate install/post-install hooks.
Generated config.h and Meson introspection are checked against config_defines,
forbidden_config_defines and meson_options. The contract requires ELF64 x86-64,
SONAME libdbus-1.so.3, all 241 public dbus_* symbols versioned LIBDBUS_1_3,
libc.so.6 as the only DT_NEEDED and no original RPATH/RUNPATH. Prefix validation
also binds actual binary/source/notice/configuration/recipe/lock digests.

Locked default system address: unix:path=/run/dbus/system_bus_socket. Session
discovery keeps DBUS_SESSION_BUS_ADDRESS, then existing $XDG_RUNTIME_DIR/bus,
then upstream X11 autolaunch via host /usr/bin/dbus-launch or PATH fallback.
No helper/daemon is supplied or installed. Main's actual prefix passed ABI/runtime
guard checks; the locked discovery rules do not prove every system/session-bus
interaction or a clean-machine Qt consumer flow.

After a successful build, original proof is under the prefix's
share/licenses/D-Bus/source-proof/ and metadata under
share/citroen-c4-robot/dbus-source.json. Keep work/tools/dbus-source as the
metadata's corresponding source cache. The lock records GPL-2.0-or-later election
from upstream's AFL-2.1 OR GPL-2.0-or-later; retain complete source and all source
notices, not just that label. This is no legal certification. Use the separate
user-owned-copy replacement route below for an interface-compatible rebuilt
library, preserve its SONAME symlinks/relative RPATH and mark changes; managed
signature/inventory verification remains unchanged.

Deployed shared dependencies and the signed companion
----------------------------------------------------

Native tar and AppImage producers require a separately signed source companion.
dependencies/runtime-manifest.json records the actual deployed runtime inventory;
dependencies/provenance.json binds package/source identities and covered_components
paths to deployed and original hashes, retaining the relative-RPATH transformation.
Its materials contain role, original path, SHA-256, size, URL and published_path.
Material bytes are deduplicated under dependencies/materials/<sha256>; these digest
names are not archive formats or upstream filenames. Verify each material digest,
then use its inventory path/role and exact package instructions to reconstruct the
source, recipe and patches. Follow those supplied instructions, not a guessed latest
upstream recipe. Authentication evidence and missing-source failures remain explicit;
license notices alone do not establish source coverage or legal certification.

Raw BSD component-notice policy
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The raw declaration "BSD" is resolved only for these exact source-package /
numeric-versioned library filename pairs with a matching whole INSTALLED notice:
  libogg / libogg.so.<numeric suffix(es)> -> BSD-3-Clause
    SHA-256: d2ab5758336489da61c12cc5bb757da5339c4ae9001f9bb0562b4370249af814
    Primary: https://raw.githubusercontent.com/xiph/ogg/v1.3.6/COPYING
  bzip2 / libbz2.so.<numeric suffix(es)> -> SPDX bzip2-1.0.6
    SHA-256: c6dbbf828498be844a89eaa3b84adbab3199e342eb5cb2ed2f0d4ba7ec0f38a3
    Primary: https://sourceware.org/git/?p=bzip2.git;a=blob_plain;f=LICENSE;hb=bzip2-1.0.8
Each suffix consists of digits, separated by dots, with at least one suffix.
bzip2-1.0.6 is the license identifier, not the observed software version 1.0.8;
the observed libogg software version is 1.3.6. Neither mapping restricts software
versions: its binding is source-package identity, filename and installed-notice hash.
These hashes cover the exact full installed notice bytes. The primary raw texts
were checked identical modulo outer whitespace; this is not an independent HTTP
response byte-hash, signed-archive authentication or complete package-source claim.

Raw BSD declarations, full copied notices, source/original SHA-256 and origin
provenance remain intact. package_provenance.license_notices records each copied
notice's source and byte hash; component_license_evidence retains all notices and
the matched primary_notice {source, sha256, origin, license_expression}.
Generic BSD, missing/mutated notices, cross-package matches, wrong hashes and
unversioned libraries remain fatal rather than being treated as permissive.
Only these authenticated, enclosed permissive cases avoid unnecessary full-source
copies through source_required=false; this is not a blanket copyleft exemption.

Earlier v10/v11 attempts failed on unknown BSD before producing an artifact.
Main subsequently passed the packaging contract checks, including the strict
notice-binding path; this does not establish an actual package or legal certification.
Fresh signed attempts then failed the libFLAC deployed/source identity guard.
The generic failure reflected missing authenticated binary-member evidence, not
proven unequal installed bytes. No new successful package is asserted.

Strict FLAC source-identity evidence
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
CachyOS x86_64_v3 flac 1.5.0-1.2 archive SHA-256
89942086589495f2f92411e640eb474afc15ac2c3a81c0237dd3e303fe775ed9 was authenticated
with signer 882DCFE48E2051D48E2562ABF3B607488DB35A47 and the signed repository index.
Extracted libFLAC.so.14.0.0 and archive/local MTREE evidence match installed SHA-256
0237d217a9012ab0a6b5a4ab88af794e8a8138efb95d0edccae3948b7bcfa145.
The complete source recipe changes only Arch pkgrel=1 to pkgrel=1.2, matching
authenticated BUILDINFO PKGBUILD SHA-256
ad16b22bca7bea856abffb258cc124f2ec13762c9703a580f77219225d9c3cd9.
Seventeen FLAC source materials retain the source, recipe, inline prepare edit,
notices and origin evidence; these are not a total dependency-inventory count.
Raw source terms and fail-closed identity checks remain unchanged. This closes
the missing evidence, not package/reproducibility/clean-machine/public-offer gates.

Following FLAC evidence closure, both fresh signed attempts failed on libXau's
MIT-open-group source terms (Main's observed failure receipt: 18.54 seconds).

Current authenticated static source closure
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Source-cache provenance is21 COMPLETE records/284 material entries/266 unique
paths (18 duplicate-path entries), SHA-256:
  e804a7d979e4fe458530b2e0fd989fee815fc1076aa04847755a909f809c5bde
Canonical receipt: work/tools/dependency-sources/runtime-closure-ready.json
  SHA-256: b5a03115e7f2c97d7ca86e888f4143ba75c6316b1282af9b96b03b6fba5a122b
Static closure covers157 ELFs/148 selected installed members/78 owner families/
19 source-required installed families. The33 newly authenticated binary owners
are31 additional permissive owners plus LZ4 1:1.10.0-3 and Graphite 1:1.3.15-1;
no corresponding-source obligation was invented for permissive components.
Exact signed packages, selected member/MTREE equality, BUILDINFO-bound recipes,
complete sources, recipe patches and notices are retained for LZ4 and Graphite.
Current runtime-needed, binary-identity and source-required-identity unresolved
sets are empty/0. Global provenance still retains1 legacy NONDEPLOYED distro
dbus1.16.2-1.1 exact-recipe gap; authenticated owned D-Bus supplies the current
SONAME override. This is NOT zero-global-unresolved or legal certification.
All22 existing policy Python extensions are a conservative static superset;
the actual producer import-selected subset remains unobserved. Eight selected
Qt policy plugins all belong to qt6-base6.12.0-3.1; no imageformats/platformthemes/
Qt compositor roots were selected, and qt6-wayland is not a current client gap.
Loader/glibc/graphics-driver/device families remain excluded host dependencies,
not recursively acquired. Vulkan loader is owned with the full six-shader
payload; system CA trust and ICDs remain host resources.
Latest signed0.1.0/0.1.1 producers59/60 (v010/v011) exited1/19.37s and1/19.36s,
after source identity closure, on:
  distro notice path escape: /usr/share/doc/xz/COPYING
No artifacts/signatures/reproducibility receipts resulted. Earlier44/45 LZ4
failures reflected missing authenticated cached member/source evidence, NOT
modified deployed ELF; that evidence closure remains intact. The shared signed
notice fix below is READY with Main64/67 contracts, not an actual fresh005 archive.

Shared authenticated distro notice-path policy
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Approved roots are /usr/share/licenses and /usr/share/doc, plus the existing
EXACT /usr/lib/python3.N/LICENSE.txt route. Root membership is NOT authority.
Exact installed package/version/architecture archive signature, .PKGINFO,
.BUILDINFO recipe and authenticated .MTREE membership remain required.
The checksum-map default is preserved; notice entries additionally retain signed
type/link/mode/size/SHA-256, /set and /unset defaults and normalized octal modes.
Every file-link and intermediate parent-link hop must match authenticated MTREE
spelling/type/mode. Signed relative targets normalize only inside the approved
boundary; candidate traversal/outside/staging paths and unsigned hops are fatal.
Ordinary final content must be bounded regular data in the SAME authenticated
package member map, matching raw mode/size/hash. The existing deliberate
directory-owner enumeration admits a directory alias only after authenticating
the exact canonical directory OWNER/VERSION and signed directory/member entries:
not a general cross-owner file/symlink waiver. Aggregation rechecks actual copied
raw bytes/modes against retained pins/provenance, preserves complete original
notices/source headers with existing aggregate headers, and writes output0644.
Rootless SDL/innoextract/AppImage/source-derived/standard GNU routes retain their
separate pins. No XZ special-case, unrestricted realpath, dropped COPYING,
global custom/OpenSSL/unknown waiver, AND->OR or source-authentication relaxation.

Genuine xz5.8.3-1/x86_64_v3 signed member witness:
  work/tools/dependency-sources/xz/5.8.3-1/signed-MTREE.txt
  line83: /usr/share/licenses/xz/COPYING -> /usr/share/doc/xz/COPYING, mode0777
  line63: same-package regular target, inherited mode0644, 3119 bytes
  target SHA-256: 616a3ad264ce29b8f1cb97e53037b139d406899ca8d1f799651e17bfa09830b8
  archive SHA-256: e675df69df51185665055c05b131bb55998acd350d311b1597c3aedc6295d849
  signed MTREE SHA-256: cee99b123abfdadb42614ace0506f49fcd6ddb58f20fc9796bb63589f5829784
The old licenses-only canonical boundary/checksum-only parser could not justify
this signed cross-root spelling/mode; notice bytes/recipe were not missing.
Static selected78 owners/157 ELFs/148 members yield109 notice CANDIDATES across63
notice owners:107 licenses entries plus ACL doc COPYING.LGPL and exact Python
LICENSE, before standard texts/source supplements/component filtering.
Selected file links1 (XZ), directory aliases0: NOT actual deployed inventory or
independent verification of all copied installed bytes/modes. Source21/284/266,
provenance/cache, owned prefixes and accepted native binaries remain unchanged.

Strict Zstd source-identity evidence
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Earlier signed native0.1.0/0.1.1 attempts both failed34.20s at
/usr/lib/libzstd.so.1.5.7: "deployed ELF differs from authenticated source identity".
Neither produced an artifact or reproducibility receipt. The cause was missing
authenticated binary-member manifest/source inventory, NOT modified installed ELF.
Acquisition retained15 Zstd materials under zstd/1.5.7-2 in the existing cache;
the current21-record provenance and static closure are specified above.
Recipe/binary binding proof:
  work/tools/dependency-sources/zstd/1.5.7-2/recipe-binding-proof.json
  SHA-256: 6bc1c5126eee9b2c3f2c7681b9b6ed9b1e691c9bd4211096b2f61ffbedea1bd9
Exact CachyOS zstd1.5.7-2/x86_64 binary archive SHA-256:
  384bbfbd75be069056ae721705c36dcd7ceaeba2fa58d69a6868dd05f7916ee8
  signer: 882DCFE48E2051D48E2562ABF3B607488DB35A47
Extracted libzstd.so.1.5.7 directly equals installed ELF SHA-256:
  4e61afdf04d80cd25d7689e2fa75f63e53b6fd2029d1edcc683f779e23042121
Installed/signed archive MTREE bytes equal SHA-256:
  f69d19b02393c3af56a25ea52d94891f08ace21aaae095a0a4df0e875faec595
Exact Cachy recipe commit3af5fc7276c81d5c9397bdcc5934396bd8281854
has whole PKGBUILD SHA-256 matching authenticated BUILDINFO:
  6da4bbc7c3b096f11eabc50342e421b207fee20e642c6d869e980b54e7e12b92
No Arch-only patch is part of that exact recipe.
Complete upstream source archive SHA-256:
  5b331d961d6989dc21bb03397fc7a2a4d86bc65a14adc5ffbbce050354e30fd2
  signer: 4EF4AC63455FC9F4545D9B7DEF8FE99528B52FFD
Both binary and source detached-signature verifications exited0.
Conservative (BSD-3-Clause) AND (GPL-2.0-only) obligations remain unchanged.
The full source archive preserves every per-file notice, including authentic
empty build/LICENSE; positive-byte LICENSE and COPYING are retained separately.
An empty authentic file is not fabricated into a positive license notice.
This is acquisition/member/signature closure, not changed runtime_bundle.py,
tests/source guards/alias policy, FFmpeg/D-Bus prefixes, system libraries/keyrings
or original assets. No helper execution, dependency rebuild or ABI replacement
was performed for closure. Earlier AppImage-runtime zstd1.5.6 reference scope
above remains separate from this deployed native dependency.

Host upgrade source records for native0.1.4
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The host upgrade replaced curl8.22.0-1.1->8.23.0-1, freetype2 2.14.3-1.2->
2.15.0-1.1, libarchive3.8.9-1.1->3.8.10-1.1, python3.14.7-2->3.14.8-1,
qt6-base6.12.0-2.1->6.12.0-3.1 and zstd1.5.7-2->1.5.7-4. curl, freetype2 and
python resolve to permissive routes without version pins. The two
source-required packages received new exact provenance records (23 records;
the older-version records remain in the cache inventory and are not selected):
qt6-base6.12.0-3.1 (16 materials, qt6-base/6.12.0-3.1/):
  signed CachyOS x86_64_v3 archive SHA-256 (signer 882DCFE4...8DB35A47):
  fee4e70c38fb15f601bfc5ccf895e0602493dac5ab40a4c5c0ef35316085d2a9
  Arch tag 6.12.0-3 PKGBUILD with only pkgrel=3 -> pkgrel=3.1 equals BUILDINFO:
  f45c8bfab7866df7d1040f056993191c65f5a9674751dae916ddf0e92a7eec53
  Unchanged qtbase v6.12.0 commit 025bdad181de241e81bf853c8a2d7bf3d19261a9
  source; prepare additionally cherry-picks upstream 6b7ee05def510c333fbb8944ae669b72a48367f3
  (code.qt.io patch, SHA-256 baf7229fcc2408d9685614291451b25f4fd08b9287a724cbdcd0c6a4cfb2d54d).
zstd1.5.7-4 (16 materials, zstd/1.5.7-4/):
  signed CachyOS x86_64_v3 archive SHA-256:
  ede6f541ce44548d84b62bd04e856e4c74daff49dc5a9f27c581246679734fee
  CachyOS-PKGBUILDS commit 57350d00981b2cf8aa8a04cb2020f33f87b4be97 PKGBUILD
  equals BUILDINFO: 2df24378148a455aff6cd1840ce8bb49ba112d8b442e296569baaebd3bce8f8b
  Same release archive/signature as above (re-verified, fingerprint
  4EF4AC63455FC9F4545D9B7DEF8FE99528B52FFD) plus recipe-pinned upstream patch
  6af3842118ea5325480b403213b2a9fbed3d3d74 (man pages/manual only), SHA-256:
  7c07be222d45718b81fb16f97e611adfeb24efa0712ca77fb1e08f4c67803ec3
  libzstd.so.1.5.7 member = installed ELF SHA-256:
  28305b9e5e67265edb2b72a6fbd1dcc501dab41816e650dec021a6644af59bbb
Rebuild by following each record's instructions with its retained PKGBUILD.

Exact legacy GNU standard full-text lookup
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Historical signed native0.1.0/0.1.1 zstdclosed-002 producers41/42 passed the restored
Zstd member/source gate, then both exited1/17.98s on:
  missing license text for libidn2: GPL2
No artifact or reproducibility receipt resulted. This is literal standard-text
resolution, not missing authenticated libidn2 source. Existing authenticated
libidn2 2.3.8-1.1 .PKGINFO lines13-14 and PKGBUILD line10 declare GPL2 and LGPL3.
Retained libidn2-2.3.8.tar.gz SHA-256:
  f557911bf6171621e1f72ff35f5b1825bb35b52ed45325dcdee931e5d3c0787a
Its COPYINGv2 and COPYING.LESSERv3 are present; README grants the C library
GPLv2-or-later OR LGPLv3-or-later. Top-level COPYING is GPLv3, NOT a GPL2 substitute.
The host common-license directory is absent; canonical SPDX full texts exist.
runtime_bundle.py preserves raw SPDX/exception/common candidates FIRST, then
uses exact GPL2 -> GPL-2.0-only and LGPL3 -> LGPL-3.0-only canonical text names.
These names select complete TEXT BYTES ONLY, not a grant narrowing/transformation.
GNU GPL Version2, June1991 full text under both GPL-2.0-only and GPL-2.0-or-later
has SHA-256:
  aaf135472f81c5b4a0dca9367e5bb5e9750032b5bebe5442b36e4c0a47430df3
Canonical LGPL-3.0-only full text SHA-256:
  996af0513df21f7496288951c41428a03c174e9e4a9d63665c57d670f845ccb1
Raw metadata/conservative package AND, actual component OR/or-later grants,
source obligations and byte-preserving aggregation remain unchanged. Unknown/
custom terms stay fatal; no permissive/source_required=false or source-guard bypass.
That historical full-text fix changed only existing runtime_bundle.py and its
one runtime_paths.py regression, not dependency/source guards or component grants.
Subsequent source acquisition is described above; no dependency rebuild follows.


Exact standard taxonomy and private versioned notice bindings
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Genuine SPDX MIT-open-group, NAIST-2003, SMLNJ and exactly
BSD-3-Clause WITH PCRE2-exception are admitted with their original identifiers
and full notice obligations, not rewritten to generic MIT/BSD. Primary texts:
  https://spdx.org/licenses/MIT-open-group.html
  https://spdx.org/licenses/NAIST-2003.html
  https://spdx.org/licenses/SMLNJ.html
  https://spdx.org/licenses/PCRE2-exception.html
Unknown/lookalike terms and incompatible exceptions remain fatal. Existing
raw-BSD bindings, strict AND and copyleft/source obligations remain unchanged.
Globally raw custom, opaque LicenseRefs and OpenSSL remain fatal; the following
are exact package/version/real-library/full-material bindings, not global aliases:

ICU 78.3-1.1: only libicudata.so.78.3, libicuuc.so.78.3, libicui18n.so.78.3.
  Whole unchanged NOTICE SHA-256:
  e55522d81edc687a341a4411e0776e54ca654e90147f354a90458aaced4116af
  Preserved expression: Unicode-3.0 AND BSD-2-Clause AND BSD-3-Clause AND NAIST-2003.
  No global LicenseRef-Unicode-3.0 alias.
Xext 1.3.7-1.1: only libXext.so.6.4.0.
  Whole COPYING SHA-256:
  fd62910be4b13829d94e76c1447cf840953f0e225c4dc6c79349c84dd0557f22
  Private LicenseRef-libXext-1.3.7 represents the complete notice bundle, not MIT.
Kerberos 1.22.2-1.1: only libgssapi_krb5.so.2.2, libkrb5.so.3.3,
  libk5crypto.so.3.1 and libkrb5support.so.0.1.
  Private LicenseRef-Kerberos-1.22.2-LibraryNotices requires unchanged authentic
  NOTICE plus complete src/include/k5-queue.h, src/include/copyright.h and
  src/lib/gssapi/LICENSE. The UCB attribution gap is closed by retaining the
  actual queue header, not overwriting NOTICE. SHA-256 pins, respectively:
  d37fcb886de2ad1c602f61c99c6893bcbd9a1949f54d9a6bb40d191204eb605e
  6074ee76bdde96dfcf2a57134676baf7ae37fac7644ff349d3996b748a82d0bb
  435ef19645770fbb3e9cdd2bead820307768ce1981e5d164622f6faca5333f2d
  c4ab82b506f53b97e7bf7ee2b71152da792e0d5578da4089ed17b4dd8798aedf

Libarchive 3.8.10-1.1: only libarchive.so.13.8.10.
  Private LicenseRef-libarchive-3.8.10-LibraryNotices-CC0 binds the exact recovered
  recipe and upstream commit 0c48412be2d94afe1859a807ea7896a18d8900bd (signed tag
  v3.8.10, locally verified against validpgpkeys 659C84C0E23EA1FA97E0B58CC040B508D63D2B36).
  No downstream patches/backports; the recipe reverts upstream commit
  fafe82e3502e8e75a6c4146ec977f77244787c6d (xar XML detection). That revert's only
  legal-comment change removes libarchive/archive_xml_private.h, so the tag-tree
  corpus below is a notice superset of the compiled tree. Whole recipe SHA-256:
  f97910d09d390608ef7177d48f39fdb1e0f340b265c00f557d1d9cb076c8019d
  Signed package, authenticated MTREE and installed ELF SHA-256:
  08960c60b44bb280e84eb35c54350829cd69bd27190b844da6a54ffa227583f0
  All three complete materials below are mandatory, not interchangeable:
    library-notices.txt: 176 manifest-concordant library source/header files,
      199 complete verbatim legal comments, 292117 bytes; SHA-256:
      34c7c75c7410b42a9e556b77ae6a23729d23031301b0b1354c4aee65d6562ee2
    unchanged upstream COPYING: 3089 bytes; SHA-256:
      30e556b3959e3985d66efefec5eaac51d4995053caa1d3cffe6eb916f146f229
    complete CC0-1.0 legalcode: 7048 bytes; SHA-256:
      a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499
  CC0 is elected only for four BLAKE2 files under libarchive/:
    archive_blake2.h, archive_blake2_impl.h, archive_blake2s_ref.c,
    archive_blake2sp_ref.c.
  It is not a blanket notice override. Individual file notices control;
  unchanged COPYING supplies its stated default where needed. Conditional
  platforms are retained conservatively; which files were compiled is unknown.

The source-derived notice producer reads this immutable source archive
ephemerally, without executing source or retaining a source tree:
  https://codeload.github.com/libarchive/libarchive/tar.gz/0c48412be2d94afe1859a807ea7896a18d8900bd
  6829745 bytes; SHA-256:
  391daa6bbb2d0d8315a8dea5dc741f071e759cfeca2fe0bb90999b42979743f1
Exact Makefile.am/library manifest membership, comment offsets/full-byte format
and final corpus size/hash are enforced. Existing output mismatch is fatal;
missing output is published atomically. The official immutable Creative Commons
byte-equivalent CC0 endpoint uses the unchanged shared downloader.
Authentic acquisition/recipe/corpus receipts in the repository:
  work/tools/dependency-sources/notices/krb5-1.22.2/acquisition-receipt.json
  work/tools/dependency-sources/libarchive/3.8.10-1.1/recipe-binding-proof.json
  work/tools/dependency-sources/notices/libarchive-3.8.9/copyright-provenance.json
    (collector algorithm receipt; the 3.8.10 corpus reuses it with the new
    version/commit preamble, 176-file manifest and pins above)

Strict regular-file default and bounded empty stdlib ZIP sources
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Earlier signed0.1.0/0.1.1 attempts71/72 (005) both exited1/21.98s after
XZ signed-notice/source21 authentication at:
  not a bounded regular file: /usr/lib/python3.14/compression/__init__.py
This is a legitimate empty stdlib initializer hitting the default positive-size
guard, not a missing module/owner/archive. No artifacts/signatures/repro resulted.
runtime_bundle.py keeps _regular(..., *, allow_empty=False) strict/nonempty
by default. Only the ordinary bounded stdlib .py source guard in the existing
deterministic _write_stdlib_archive opts into allow_empty=True. Regular type
via lstat/per-file4MiB bound/installed owner/exact /usr/lib/python3.N root,
source-relative ZIP names/source exclusions/hidden and symlink-directory
pruning/.py selection remain required. Symlink files/directories/missing/
unowned/outside-root/oversized/excluded inputs stay rejected. No package or
filename special case/dummy bytes: actual empty source bytes remain unchanged.
Generic copying/ELFs/keys/signatures/archives/source proofs/manifests/notices
and generated-ZIP finish retain positive-size guards; ZIP stays nonempty/0644.
Five selected Python3.14.7-2/x86_64_v3 files are actual regular0644/size0:
  compression/__init__.py
  compression/_common/__init__.py
  email/mime/__init__.py
  pydoc_data/__init__.py
  urllib/__init__.py
All have SHA-256:
  e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
Actual owner files/already-cached signed archive/authenticated MTREE match
type=file/mode644/size0. Archive SHA-256:
  0e9a5d3e712ce85578a6eb452c74b0df898c2bb71b780a2b42f432a617e98043
Signer: 882DCFE48E2051D48E2562ABF3B607488DB35A47
Authenticated MTREE SHA-256:
  8f8df59b29ff8579bddb485218dac5a1cadfd4d3615bc0aa56b3bafcee5c25cd
No acquisition/source-inventory/host change or full actual package import is
claimed; the22-extension policy superset is not the actual producer subset.

Exact cached-source component grant, not a global LGPL alias
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Historical signed0.1.0/0.1.1 attempts76/77 (006) both exited1/46.78s at:
  missing license text for libasyncns: LGPL
Historical81/82 (007) both exited1/44.00s at:
  Unexplained absolute source provenance path: /run]
Authenticated notices, five empty Python initializer and cached libasyncns
whole-LGPL stages cleared before007 failed. Progress is not artifact/legal acceptance.
The exact-component cached-source-members-v1 bridge validates ALL exact signed
binary archive/PKGINFO/BUILDINFO/recipe/MTREE/ELF records, package version,
recipe commit, source archive and member mode/size/SHA records BEFORE raw
loose-license lookup. Only concrete libasyncns1:0.8+r3+g68cd5af-3.2,
usr/lib/libasyncns.so.0.3.1, receives resolved LGPL-2.1-or-later while raw
LGPL metadata is retained. Actual library libasyncns/asyncns.c and asyncns.h
both grant LGPL version2.1 OR any later version; actual Makefile library-source
membership corroborates scope. Whole LICENSE corroborates LGPL2.1, but is
NOT sole grant authority. Complete raw LICENSE/C/H bytes, including form feeds,
are preserved: no whitespace/form-feed stripping, truncation or dummy text.
Global bare LGPL remains unknown/fatal, with no guessed global version/new
license identifier; sourceRequired and conservative AND obligations stay.
Exact source commit:
  68cd5aff1467638c086f1bedcc750e34917168e4
Whole PKGBUILD SHA-256:
  c3406297b1bf539da442e3e35cb43f47652d6adaac487028012fda4f9e48d122
Already-cached source archive:
  work/tools/dependency-sources/libasyncns/1:0.8+r3+g68cd5af-3.2/libasyncns-68cd5aff1467638c086f1bedcc750e34917168e4.tar.gz
Archive SHA-256:
  aef00f63f60ce69816405b7372ca78b7c525b17520bf7a747a700ffb9d82b296
Canonical tar SHA-256:
  c727e140aab4c94c5c235768e496bd2466c299691d5ddd13dd5ffd9e7364b61a
Whole raw LICENSE: 26528 bytes; SHA-256:
  d80c9d084ebfb50ea1ed91bfbc2410d6ce542097a32c43b00781b83adcb8c77f
Whole libasyncns/asyncns.c: 37112 bytes; SHA-256:
  4db61110d36de49df9162499df3b875c2fa6744418bc2f40f79bb7e78a037abb
Whole libasyncns/asyncns.h: 6985 bytes; SHA-256:
  74a2409ab37cddd0fbb8c49230189913b025368f598ec5172ee468d185fe444e
Signed binary archive SHA-256:
  9a5d3d165207882ffee4bfc885194999b3b3c8e6ba73ceafb4d2a7e01670002f
Actual usr/lib/libasyncns.so.0.3.1: 26496 bytes; SHA-256:
  532222b24689f1d81d70748db19cc11f84ba468cb204f4f1db2584357371ac92
Installed/signed MTREE SHA-256 (already-cached authentication receipt):
  07ce825c50ead527ff2eb74983a9aa83243171281b562dbccf22894d6199dfd9
These are policy-only changes, not source-cache acquisition/inventory changes.
Raw producer-derived notices-cache publication remains future producer work,
not observed publication or a source-cache mutation. Batch78 read-only78-owner
GNU26/15-token/three-component-exception audit identified no additional missing
GNU text; static audit is not actual deployed package/legal proof.

Current bounded acceptance, not package/legal certification
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Authentic corpus/legalcode acquisition is complete, not missing material.
Main's current four source-bundle/runtime-paths --dbus-prefix/safety/AppImage
contracts PASS1.31s (safety4.205s); prior PASS11.86s is historical.
Prior signed-notice, real empty-Python
ZIP/import and exact libasyncns whole-LICENSE/C/H positive/authentication/
version/binding/grant/member-error negatives remain; Main79 and earlier
receipts retain their historical scope.
Portable provenance grammar recognizes PATH only: declared roots, finite
parent-root aliases and actual compiler/private/toolchain/GCC paths. Unknown
host paths fail closed: no /run] whitelist and no dropping unknown host paths.
Main's generated intro-targets JSON literal /usr/lib[private-build-root] kept
the correct raw source SHA/BOM but was wrongly accepted. The actual negative
exited1/0.35s at:
  Invalid bundle request was accepted: dbus-json-literal-source-path.tar.gz
The earlier four PASS2.84s therefore were not sufficient acceptance.
The fixed consumer recursively checks ORIGINAL JSON keys and leaf strings
at strict literal boundaries, VALIDATION ONLY: no normalization/reserialization
of published raw JSON and no hash/authentication change.
Signed008 both failed43.13s before complete artifacts at original
intro-targets.json[1].target_sources[0].parameters[0]:
  -I<declared-build>/dbus/libdbus-1.so.3.38.3.p
The exact provenance_roots.build parent was declared; replacement's preceding
I/L exclusion missed attached operands while absolute rejection correctly failed.
Shared conversion now recognizes literal attached -I/-L operands using existing
exact declared-root canonicalization, longest root first. Actual Meson parameters
and negative paths are covered by Main's current bounded contracts, not a producer
PASS. Unknown/private, literal bracket/colon, traversal/sibling, PATH private-parent
and prior typed-argument/JSON-key/leaf guards remain strict; no whitelist,
whole-JSON skip/drop/reserialization, raw-byte/digest or authentication relaxation.
Source-cache21 COMPLETE/284 material entries/266 unique paths/static
authentication remains unchanged. These bounded contracts did not produce008;
the genuine historical009 fixture publication/reproduction is recorded below.
They do not establish signed010, production trust, legal/source-offer/rebuild/
ABI-replacement certification or stronger ordinary Home Play acceptance.
GUI003's viewed03 Enhanced Apply and005 native SDL F5 retain Custom/OpenGL
fallback in private Xvfb/llvmpipe640x480 one-surface Menu. Actual007 is unchanged
Quality/no error footer; its anticipated forced-Apply failure claim stays retracted.
Main viewed actual012 (following011 error footer), proving forced Vulkan Apply
failed with SDL_CreateWindow/missing VK_KHR_surface while applied Custom/base
Enhanced/actualGL/RT0/denoiseOff/PBR/aniso4/MSAA4 and old screen remained alive.
Actual175-byte runtime.conf both after failed Apply and after normal Turkish
EVET quit retains implicit Vulkan/Enhanced/RT0/640x480/VSyncOff/cap60, no backend
or SSAO override; failed manual Custom draft was not saved. Quit EXE exit0 and
closed016 viewed. Restart37 same private XDG/no renderer-quality CLI overrides
exited0/4.21s/source64/index63/targettrue/113 presentations; PNG viewed, actualGL/
requestedVk/Enhanced->Custom/MSAA4/PBR/HDR/bloom/noRT/denoise honest fallback.
The restart image shows early Intro/logo/Menu bitmap and dynamic WMV-preview
black line at source64, not world/per-effect quality; startup pipeline and
actual saved-request consumption are the accepted scope.
This is not host physical/AMD input/F5, other-profile or gameplay-reset proof.
Main hardware36 actual1920x1080 AMD WaylandGL4.6/source320/Original->Original
has four dictionaries equal RT7/fallback and viewed world/sky/car/HUD; cap144 is
requested, not attained. Hardware38 exit0/12.84s/1079 presentations/source320/
index319/targettrue/3470 entities/0 omitted sprites/Original->Original requests
3440x1440 but Wayland clamps actual logical/drawable to2560x1377; world/HUD viewed.
Cap240 requested/swap0/display164.9600067/VRRunknown is not true3440 hardware,
attained240 or scientific performance. Current private Xvfb3/X11/llvmpipe40
exited0/13.32s/319 presentations, actual logical/drawable/capture3440x1440,
all0/Original/source320/index319/targettrue/3470 entities/0 omitted sprites,
Main-viewed authored world/HUD: true3440 SOFTWARE/VIRTUAL, not physical AMD3440.
Main asserts all four published dictionaries for BOTH38 and40 equal validated
RT7/current1920 GL144 source320, not full hidden-state/numeric/pixel parity.
Main's five new builds PASS:0.1.0 17.41s/0.1.1 16.05s/GL-only17.85s/
sanitized19.25s/Debug build+28 CTest91.05s combined (tests72.98s), exit0.
Compiler warnings remain, not warning-clean. Actual54 asset-free llvmpipe
LLVM23.1.1 native query exit0/1.38s restores PBRtrue/shadow4x2048/trilineartrue/
aniso16 without runtime.conf. Support is not enabled Original effects:
effectsOff/filterOriginal/MSAA1/GL RT0; effectiveCustom reflects this case's
explicit renderer override, not SourceHigh readiness. Actual shownQt55
/tmp/c4-launcher-integration-capability-parity-001/report.json exit0/25.75s,
all exercised checks PASS: same-device/five-capability query parity against
actual3-frame startup/150 Original/150 Custom children, no probe-created settings,
and byte-identical native settings after reprobe. Main viewed07 repair validation
and08 Quality Custom: Raster Shadows/PBR now enabled, base Original+FXAA.
The report remains PARTIAL/full_scenario_complete=false/import_native_complete=true,
updates neither requested nor exercised. That historical PARTIAL scope is
distinct from later009 integration contracts and current010 source fixes below;
it is not stronger signed010 ordinary Play or full-launcher acceptance.
Qt46/47 import/Repair/cancel/finite Original/Custom+FXAA receipts remain PARTIAL:
viewed Intro/logo/WMV preview is not interactive MainMenu/world quality,
full signed lifecycle, desktop-menu activation or device/listener audio.
Their query contradiction was a real bug, not unsupported OpenGL; current54
query plus actual55 shown-Qt consumer now resolve it within that observed scope.
Genuine Vulkan1.3 feature absence and unknown OriginalHigh attribution remain
separate. Canonical current details: docs/ENGINE_ARCHITECTURE.md.
Current426 real API/logical/device NULL candidates/same-worker joins lack
named-target resource-view/byte/numeric proof; strict native NULL guards stay
without waiver. Actual package/publisher owner/repository/trusted PEM gates open.
Main56 rebuilt GL-only actual native-window suite exit0/16.90s, not empty CTest.
Main57 rebuilt physical AMD GL Original/all0 exit0/14.91s/797 presentations:
source320/index319/targettrue/actual1280x720/3470 entities/0 omitted sprites;
Main viewed authored world/HUD. Main's proper source_raw, source_vector_raw,
source_array_raw and source_state dictionary comparisons are each TRUE:
exact equality against actual25 validated RT7 source320. Expected render/audio/
present/timing/profile diagnostics differ; no whole-game/scene, complete hidden-
state/original numeric/pixel parity or attained-FPS/performance claim follows.
Prior accepted dictionary receipts retain their scope.
Original006 actual Race CGM3/MTQ2/210.71s and one quiescent hardware-break packet
qualify one TABELA9 MATERIAL/effect/owner identity; canonical limits are in
docs/REVERSE_ENGINEERING.md. No selected Normal target/NULL-shader binding,
numeric parity or full High certificate follows. Strict High guards and
function/address inventories remain unchanged.
Root7 independent wrapper actual return0/genuine waitTRUE spans monotonic
72591.80649->73206.25721 =614.450719s, unchanged requested600 plus teardown
(ObserverSID4124293/Wrapper4124246), without Main TTY Ctrl-C. Authentication
before continue failed device/inode matching across broad D3DX candidates;
hardware breakpoints disabled and continued target received bounded SIGKILL.
No named Normal/effective NULL packet/shader-byte/numeric/High certificate
follows. Exact failure tuple was lost in cleanup/private Root file missing,
so unrelated-builtin implication is [INFERENCE], not an established diagnosis.
Actual Root7 full normalized trace EOF/SHA/count are UNOBSERVED; earlier
bg19 trace proof and valid Original006 packet/interrupted lifecycle stay
separate. Root8 prepared/start attempt pending has no observed result,
observer clock/race/attach/capture/proof. Strict High guards remain unchanged.
Root009 qualified outcome work/verification/high-root009-main-qualified-outcome.json
records natural entry/full600s/independent genuine wait0/Main-viewed Race324,
but canonical D3DX/same inode999512 strict authentication still fails:
maps device(0,55) versus stat(0,92). Corroborated Linux7.2.9/Btrfs proc-map
superblock/getattr-root-anonymous-device behavior is not DLL certification
or guard waiver. resumeRequestedTRUE is not executed-callback proof; no
named Normal/effective NULL/compiled shader/numerical/pixel/full-trace EOF proof.
Root010 Windows NEVER EXECUTED: prove_isolation rejected wrong GDB HOME /tmp;
genuine observer wait1/0.4516s. No600/ext4-authentication/UI/shader failure or
Historical011 rejected Main's request-JSON SHA instead of the exact driver's
fresh whole-PNG SHA: genuine wrapper319.49972046s/return0 and observer308.089s/
launcher_gui_deadline_stop/requests[] prove no Original launch, not600s/650s.
Corrected012's earlier whole-PNG/source-frame crosschecked start-only receipt
was not a capture certificate. map_files EPERM/no new capabilities/High guards stay.
Separately, gtk-launch canonical desktop-ID lookup and Main-viewed Qt Home/
assets/native-version plus normal close are accepted within that bounded scope.
Old xdotool windowquit returned0 but EWMH _NET_CLOSE_WINDOW required a WM:
root _NET_SUPPORTING_WM_CHECK/_NET_SUPPORTED were absent;12s PIDFD exitFALSE
remains failed transport evidence. Correct direct ICCCM WM_PROTOCOLS/
WM_DELETE_WINDOW to owned Qt client2097159 was submittedTRUE and yielded
PIDFD_EXIT_TRUE0.03001656799s through normal Qt close, not Destroy/Kill.
Detached Qt exit code is NULL/unobserved, not0; private D-Bus/shell2 exit0
after Qt finished is a separate parent status. This is not system-menu/search/
AppImage/all-preferences/full launcher lifecycle/listener/world-quality proof.
New ordinary native Qt Settings Save/reopen, private DISPLAY4/full1050x879:
Main viewed three Saved screenshots and reopened UI04. Nine actual Saved
UserPrefs JSON fields equal/reopen values match, with configured NONproduction
test repository/public PEM/Experimental/notifyfalse/autoupdatestrue/
startupfalse/backgroundfalse. First direct Qt service4124249 normal close
has ACTUAL service exit0; reopened second Qt74393 normal close exit0/0.13017s.
Both direct-service exits0 are separate from older detached desktop Qt3880335
codeNULL/unobserved and the failed no-WM EWMH transport above.
No startup/background network timer or Updater was invoked in those FIRST TWO
services. This is genuine preferences Save/ordinary reopen/normal close only,
not check/install/signature/prerelease-selection/live hosting/full scenario.
Third hour Qt003 was interrupted26m23s before deadline: explicit hourfalse/
normalexitfalse/no08 PNG, not hour PASS. Ordinary HourQt004 DISPLAY3/
PID314457/service336366, startupOFF/backgroundTRUE/repositoryEMPTY/notifyOFF/
channelStable/no manual Check or wait-time UI input, completed unshortened3605s
supervisor wait exit0. Main viewed 10-after-real-production-hour-reopened.png:
new-versus-baseline local unconfigured real repository/pinned-Ed25519-PEM
diagnostic, no assumed release URL/trust key. One production background-hour
gate/local callback only; owned ICCCM normal close/pidfd exit0.142552662s/
supervised Qt004 exit0, not retroactive003 pass. No signed network/download/
notification/automatic install/prerelease/full-Updater/full-Qt/live-publisher/
every-hour recurrence/jitter/precise callback timestamp proof.
Actual production publisher owner/repository/trusted PEM remain blocked;
fixture keys are never production trust.
Current source-music five builds PASS43.57/31.05/30.31/41.60/43.48s,
Release boundary3 PASS3.99s/wall4.29s, actual fullDebug28/28 PASS60.73s/
wall61.18s after wrong-path pretest failure: not package or leak-clean proof.
Shown native private hardware-free PipeWire PCM2s/12m and normal Menu/Exit
UI exit0 are bounded; recorders exit1/long PCM saturation remain explicit.
Final logical voices0/device-open/errorNULL is not heard/Original PCM/
interpolation/listener-gain/latency proof; final Menu/Exit acoustic capture
is absent. Canonical scope: docs/ENGINE_ARCHITECTURE.md, expanded audio section.

Actual signed010 evidence; historical009 retained;011 pending
-----------------------------------------------------------
Current five builds exited0: GL-only15.39s/Release0.1.0 16.41s/
Release0.1.1 16.41s/Debug16.57s/ASan18.04s. Full Debug28/28 PASS32.61s/
32.72s wall/exit0, reviewer clear; warnings retained, no new ASan leak-clean claim.
Historical009 producers0/68.85s,68.97s/reproduction0/48.18s/eight byte-identical
outputs/six verified signatures remain earned. Its published companion has
19 DEPLOYED records/264 material entries/243 unique members, every hash/size
verified and embedded runtime-manifest prefix a5e395a7 matching: HISTORICAL009 ONLY.
Actual010 signed producers BOTH EXIT0/55.37s,55.67s; v010 same-input reproduction
EXIT0/49.02s/ALL EIGHT output byte SHA-256 equal, including raw signatures and
runtime-manifest. Six actual OpenSSL public-fixture verifications EXIT0/stdout
"Signature Verified Successfully"; comparison5.902s. Receipt:
work/verification/final-signed-packages-010-main-reproduction-signatures.json
Current010 AppImage SHA-256:
  f050c0864ade3fed35c4b27292ca95cf13e7a5ceb70593b6f6fdbee4bb4a066d
Current010 native tar SHA-256:
  4fe1816ff56ad2e650bfae7ce0456c897a67bfb71e6afa25c1e8078810032b0e
Current010 source SHA-256:
  88f91b104b5aef48ccd45cf050213b4cbb05acd70140935ce1e14bdeeb9e5295
Current runtime-manifest PREFIX ONLY f19ad matches package SOURCE record.
Fresh010 source243-member validation NOT RUN;009 member proof is not promoted.
Unchanged cache21 COMPLETE/284 entries/266 unique paths is distinct.
Offline fixture only; private fixture key is NEVER production trust.
Real owner/repository/independently trusted production PEM remain absent/BLOCKED;
no legal certification/source-publication pledge/rebuild/ABI certificate.
Future011 producers/reproduction PENDING, no011 artifact/result yet.

Old009 full AppImage70/directory71/clean70/71 passed their integration contracts,
but restart JSON echo plus Qt-child0 did NOT exercise ordinary Home Play.
Main viewed old activated1.1 Select Native ELF/no game. Current native lookup
resolves ONLY the canonical activated immutable DIRECT native/versions child;
strict host-compatible ELF guard unchanged, no arbitrary PATH/guard waiver.
Same root serves Home Play/native probe. Actual signed010 stronger integrations:
host AppImage bg64 EXIT0/65.76s/elapsed65371, all72 checks;
clean real-account AppImage bg65 EXIT0/66.27s/elapsed62005, all72 checks;
host installed0.1.0 tar Qt DIRECTORY bg66 EXIT0/24.43s/elapsed24212, all73 checks;
clean SAME installed0.1.0 tar/read-only first native Qt DIRECTORY bg67
EXIT0/25.91s/elapsed21418, all73 checks. EVERY check exercised/passed, errors empty,
full/import/startup/menu/updates true. All four actually saved restarted.json
records now include saved-widget Home Play into canonical immutable
native/versions/0.1.1/bin/citroen-c4-robot, finite150 NORMAL_EXIT_TRUE/EXIT_CODE0
with PPM/JSON, not old009 JSON echo. Signed1.0 -> genuine copied/tampered1.1
reject/preserve ->1.1 -> rollback1.0 -> reinstall1.1/history/settings/all0/assets73.
Main viewed host/clean restarted Qt Home active1.1 and converted native captures:
finite INTRO/SPONSORS, NOT interactive MainMenu/Race despite JSON phase Menu.
Separately ordinary signed010 Qt1354863 with NO integration arguments/flags:
actual Home click(525,310) launched detached native1359679/exact immutable1.1
executable/window10485813/640x480. Main viewed physical-updated-play-002.png
as Turkish MainMenu, then CIKIS confirmation/EVET(264,286) held0.15s.
Prospective PIDFD lifetime end0.6497658690059325s; native exit status UNAVAILABLE
because startDetached exposes no child status, NEVER native0.
Owned ordinary Qt WM_DELETE/Popen wait0 is separate. Current output root receipt:
ordinary-signed010-play-main-qualified-001.json
Ordinary reopened Qt1381246 NOargs: Main viewed
physical-updated-restarted-home-005.png/Home active1.1/saved asset
20261009T024817733-96bd1d34-d7bf-49ba-baf8-739ad5e332f5.
Owned PID/window WM_DELETE normal Popen wait0 recorded in current output root:
ordinary-signed010-restart-main-qualified-002.json
NO SECOND Native Play after reopen claimed. Earlier source-only old009
Qt1166799/native1172984/PIDFD0.670737s remains narrower history.

Actual clean floor, historical009/current010 distinct: Linux x86_64_v3/glibc2.44,
not Ubuntu24/general portability.
Official Arch base4e77cf2ea5f410e6f8be5abf93ccf17ce2436e87a138c167208356711a405dbd
probe exit0/0.47s. Derived0366c640775e2e01af8fbc847bea3781a34c0d6cf807eee074944988d8209b74
real AppImage FULL70 PASS69.13s/elapsed64440 retains numericUID1000-not-in-passwd/
D-Bus warnings. Separate useradd-derived installed-tar DIRECTORY FULL71
PASS25.23s/elapsed21659/all rows passed/exercised/stdout empty/no those warnings;
71 does not erase70 warnings. Actual010 real-account image PREFIX50e/same exact
base passes72/73 above; AppImage stdout EMPTY/no those UID warnings, NOT all native
warnings clean. Mesa26.2.4/llvmpipe LLVM23.1.1/fonts/private image D-Bus/Xvfb,
networkNONE/user1000/read-only root/capsALLdrop/no-new-privileges/private PID+IPC/
tmpfs/shm256MiB. Only read-only two releases/original installer AS DATA/fixture
PUBLIC key plus writable private output; no host usr/lib/etc/run/home/build/
prefix/display/audio/GPU/socket mounts. Shared-kernel OCI is not bare-metal
boot/hardware audio/RT/listener/Original parity.

Clean signed009 GL150 exited0/6.82s/640x480/all0/UI3666/gamma150/world-mesh/
material-incomplete0: startup receipt, not a viewed world/MainMenu claim.
Same clean VK150 rendered then EXIT1/9.06s/UI count greater than total,
two malformed-cache diagnostics with EMPTY reasons retained as counterevidence.
Current shared total counts every actual vkCmdDraw/vkCmdDrawIndexed across
geometry/sky/shadows/particles/UI/fullscreen post; no HUD double count or
compute/resolve counted as draws. Total guard remains, not suppressed.
Strict32-byte header/device-UUID cache parser unchanged; precise rejection
reason/driver-reported size added, no capacity-only/unreported-byte reads,
driver-name special case or malformed-cache acceptance. Actual installed clean
signed010 Vulkan bg69 EXIT0/9.02s/completed150 source ticks, explicit Vulkan/
requestedOriginal/effectiveCustom/no modern quality/seven choices0/ANO-Bloom0/
RTrequested+enabled0/queries0. Main viewed native PNG as INTRO/SPONSORS,
NOT interactive MainMenu/Race. Last frame TOTALDRAW27=UI24+fullscreenpost3,
geometry/material/shadow/particle0/HUD UI-subset0, retained total guard PASS.
Cache missing->malformed: reported20 bytes/saved0/TWO explicit warnings.
Fresh directory ONLY mesa_shader_cache/no native .vkcache; no bad-cache acceptance,
unreported-capacity reads or driver special case. Loader-free fixture EXIT0/0.43s
covers20/32 bad-header rejection/valid-header persistence only, NOT actual driver
cache writing. Mesa26.2.4/LLVM23.1.1/WMV3 extradata8 warnings remain;
validation_enabled=FALSE, not validation-clean/warning-clean/cache persistence/
parity/broad Race proof. Existing Main-qualified outcome:
/mnt/4TBNVMEXT4/c4-clean-installer-signed010-001/finite-installed-v011-vulkan/main-qualified-outcome.json

Root13 requested600s genuinely completed wrapper607.5130s, but helper's
incorrect WORD assertion is not canonical NULL-sampler proof. Corrected helper
self-check0/0.08s is synthetic. Authentic canonical bundled D3DX static
GetPass/getters/resolvers/full-vtable HIGHLOW evidence gives pass+0x28 versus
effect+0x5c0 unequal0/equal1, not invariant1; direct0x0a/0x0b independent selectors,
evaluator0x0e/shared remap NONNULL unsupported. Root14 UNCHANGED requested600s argv
completed start85235.710917556/end85846.991488783/611.280571227s,
observer genuine waitTRUE/return0. Supervisor014 EXIT0 only AFTER observer wait/
GDB quit, NO early interrupt. Read-only x64-to32 hardware breakpoints/73 LWPs/
no target calls or memory-register writes:77 successful RAW spans/7148 bytes/
0.010709098991355859s, all19 loaded spans authenticated canonical D3DX5edeed.../
base0x410000. Main viewed Race/HUD1/2/02:11.631 at350.91s.
Named TABELA9/B MATERIAL114471360/ENTITY236303632/D3DMATERIAL114471632/
SpecularTechnique236440308 join: mtSkin1 NAME verified/index3/raw resource COM0,
semantic NULL certificate FALSE; public selector0 from pass28=0/effect5c=65535.
BOTH VS/PS records type0x0e UNSUPPORTED evaluator recursion: no compiled-token
storage/active Normal/effective sampler/worker/numeric/pixel certificate.
Only pre250 Ghost195.61s viewed, NOT pre260/290/300. Root15 NOT RUN/no blind600;
new helper self-check0/0.11s leaves cached-cell bound UNKNOWN/no candidate tokens.
Strict High/NULL guards/function-address counts/prior MusicPCM/hour/privacy
limits unchanged; no flat-normal fallback. Existing docs FINALREADY/FROZEN for
Main's011 input hash; future011 signed producers/reproduction PENDING.
No programs/commands/hashes/tests/imports/source/newfiles/UI/prefix execution
performed for this documentation cycle; Main alone hashes after READY.

For local replacement, use a user-owned extracted native/AppImage copy outside
updater-managed versions. Replace interface-compatible shared libraries in lib/,
preserving SONAME symlinks and relative RPATH, then run bin/c4-launcher or AppRun
directly. No publisher signing key is needed for that separate copy. Official
launcher/updater signature and inventory checks remain unchanged and reject modified
managed files. This replacement route is a contract, not an exercised replacement
or byte-identical rebuild claim; retain applicable notices and mark modifications.
