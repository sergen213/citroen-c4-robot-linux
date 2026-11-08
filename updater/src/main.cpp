#include "runtime_policy.hpp"
#include <QBuffer>
#include <QHash>
#include <QSet>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSslConfiguration>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <algorithm>
#include <array>
#include <csignal>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace {
constexpr qint64 MiB = 1024 * 1024;
constexpr qint64 MaxPackage = 512 * MiB;
constexpr qint64 MaxFile = 128 * MiB;
constexpr qint64 MaxTotal = 1024 * MiB;
volatile std::sig_atomic_t cancelled = 0;
void onSignal(int) { cancelled = 1; }
void require(bool ok, const QString &message) {
    if (!ok) throw std::runtime_error(message.toStdString());
}
void checkCancel() { require(!cancelled, QStringLiteral("Update cancelled; active version unchanged unless activation already completed")); }
QByteArray localBytes(const QString &path, qint64 limit) {
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), "Cannot read " + path);
    require(f.size() >= 0 && f.size() <= limit, "File exceeds size limit: " + path);
    const auto result = f.readAll();
    require(result.size() == f.size() && f.error() == QFileDevice::NoError, "Read failed: " + path);
    return result;
}
QJsonObject object(const QByteArray &bytes) {
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && doc.isObject(), "Invalid JSON object");
    return doc.object();
}
QString string(const QJsonObject &obj, const char *key) {
    require(obj.value(key).isString(), QString("Missing string: ") + key);
    return obj.value(key).toString();
}
qint64 number(const QJsonObject &obj, const char *key, qint64 cap) {
    const auto value = obj.value(key);
    const auto integer = value.toInteger(-1);
    require(value.isDouble() && integer >= 0 && integer <= cap && value.toDouble() == static_cast<double>(integer), QString("Invalid size/integer: ") + key);
    return integer;
}
bool validVersion(const QString &version) {
    static const QRegularExpression re(QStringLiteral("^(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?$"));
    const auto match = re.match(version);
    if (version.size() > 128 || !match.hasMatch()) return false;
    for (const auto &part : match.captured(4).split('.')) {
        bool numeric = false;
        part.toULongLong(&numeric);
        if (numeric && part.size() > 1 && part.startsWith('0')) return false;
        if (!part.isEmpty() && std::all_of(part.begin(), part.end(), [](QChar ch) { return ch.isDigit(); }) && !numeric) return false;
    }
    return true;
}
int compareVersion(const QString &a, const QString &b) {
    const auto ap = a.split('-');
    const auto bp = b.split('-');
    const auto an = ap[0].split('.');
    const auto bn = bp[0].split('.');
    for (int i = 0; i < 3; ++i) {
        const auto x = an[i].toULongLong(), y = bn[i].toULongLong();
        if (x != y) return x < y ? -1 : 1;
    }
    const auto ai = a.indexOf('-'), bi = b.indexOf('-');
    if (ai < 0 || bi < 0) return ai == bi ? 0 : ai < 0 ? 1 : -1;
    const auto ax = a.mid(ai + 1).split('.'), bx = b.mid(bi + 1).split('.');
    for (qsizetype i = 0; i < std::min(ax.size(), bx.size()); ++i) {
        if (ax[i] == bx[i]) continue;
        bool xn = false, yn = false;
        const auto x = ax[i].toULongLong(&xn), y = bx[i].toULongLong(&yn);
        if (xn && yn) return x < y ? -1 : 1;
        if (xn != yn) return xn ? -1 : 1;
        return ax[i] < bx[i] ? -1 : 1;
    }
    return ax.size() == bx.size() ? 0 : ax.size() < bx.size() ? -1 : 1;
}
QString fileDigest(const QString &path, qint64 expected) {
    checkCancel();
    QFile f(path);
    require(f.open(QIODevice::ReadOnly) && f.size() == expected, "File missing or size mismatch: " + path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 65536> buffer{};
    while (!f.atEnd()) {
        checkCancel();
        const auto count = f.read(buffer.data(), static_cast<qint64>(buffer.size()));
        require(count > 0, "Hash read failed: " + path);
        hash.addData(QByteArrayView(buffer.data(), count));
    }
    return QString::fromLatin1(hash.result().toHex());
}
void verifySignature(const QByteArray &bytes, const QByteArray &signature, const QString &publicKey) {
    require(signature.size() == 64, "Invalid Ed25519 signature length");
    const auto pem = localBytes(publicKey, 16384);
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    require(bio != nullptr, "Cannot load trusted key");
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    require(key && EVP_PKEY_is_a(key.get(), "ED25519"), "Trusted key must be an Ed25519 public PEM");
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    require(ctx && EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) == 1 && EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char *>(signature.data()), static_cast<size_t>(signature.size()), reinterpret_cast<const unsigned char *>(bytes.data()), static_cast<size_t>(bytes.size())) == 1, "Release manifest signature verification failed");
}
bool safePath(const QString &path) {
    if (path.isEmpty() || path.size() > 512 || path.startsWith('/') || path.contains('\\') || path.contains(':') || path.contains(QChar::Null)) return false;
    for (const auto &part : path.split('/')) {
        if (part.isEmpty() || part == "." || part == ".." || part.startsWith('.')) return false;
        for (auto ch : part) if (ch.unicode() < 32 || ch.unicode() == 127) return false;
    }
    return true;
}
const QSet<QString> &authoredShaders() {
    static const QSet<QString> paths{
        "share/citroen-c4-robot/shaders/vulkan/raster.vert.spv",
        "share/citroen-c4-robot/shaders/vulkan/fullscreen.vert.spv",
        "share/citroen-c4-robot/shaders/vulkan/raster.frag.spv",
        "share/citroen-c4-robot/shaders/vulkan/compose.frag.spv",
        "share/citroen-c4-robot/shaders/vulkan/rt.comp.spv",
        "share/citroen-c4-robot/shaders/vulkan/temporal.comp.spv"};
    return paths;
}
bool payloadPath(const QString &path) {
    if (!safePath(path)) return false;
    if (path == "bin/citroen-c4-robot" || path == "bin/c4-launcher" || path == "bin/c4-updater") return true;
    static const QSet<QString> projectFiles{"share/citroen-c4-robot/tools/import_assets.py",
                      "share/citroen-c4-robot/tools/original-assets.json",
                      "share/doc/citroen-c4-robot/README.md",
                      "share/doc/citroen-c4-robot/docs/PROGRESS.md",
                      "share/doc/citroen-c4-robot/docs/REVERSE_ENGINEERING.md",
                      "share/doc/citroen-c4-robot/docs/ENGINE_ARCHITECTURE.md",
                      "share/doc/citroen-c4-robot/docs/GRAPHICS_ROADMAP.md",
                      "share/doc/citroen-c4-robot/docs/KNOWN_ISSUES.md",
                      "share/doc/citroen-c4-robot/docs/FUNCTION_MAP.md"};
    return projectFiles.contains(path) || authoredShaders().contains(path) || c4_update::authorizedRuntimePath(path);
}
bool executablePath(const QString &path) {
    return path == "bin/citroen-c4-robot" || path == "bin/c4-launcher" ||
           path == "bin/c4-updater" || path == "tools/innoextract" ||
           path == "runtime/bin/python3";
}
struct FileRecord { QString path, sha; qint64 size; };
struct Manifest { QString version, sha; qint64 size; QList<FileRecord> files; QByteArray bytes, signature; };
Manifest manifest(const QByteArray &bytes, const QByteArray &signature, const QString &key) {
    verifySignature(bytes, signature, key); // Never parse unverified release metadata.
    auto data = object(bytes);
    require(number(data, "schema_version", 1) == 1 && string(data, "os") == "linux" && string(data, "arch") == "x86_64", "Incompatible manifest schema/platform");
    Manifest m;
    m.version = string(data, "version");
    require(validVersion(m.version), "Invalid release version");
    require(data.value("package").isObject(), "Missing package");
    const auto package = data.value("package").toObject();
    require(string(package, "name") == "native-linux-x86_64.tar.gz", "Unexpected package name");
    m.size = number(package, "size", MaxPackage);
    m.sha = string(package, "sha256");
    static const QRegularExpression sha(QStringLiteral("^[0-9a-f]{64}$"));
    require(m.size > 0 && sha.match(m.sha).hasMatch(), "Invalid package size/hash");
    require(data.value("files").isArray(), "Missing file inventory");
    const auto list = data.value("files").toArray();
    require(!list.isEmpty() && list.size() <= 10000, "Invalid file inventory count");
    QSet<QString> seen;
    qint64 total = 0;
    for (const auto &value : list) {
        require(value.isObject(), "Invalid inventory entry");
        const auto entry = value.toObject();
        FileRecord item{string(entry, "path"), string(entry, "sha256"), number(entry, "size", MaxFile)};
        require(payloadPath(item.path) && sha.match(item.sha).hasMatch() && !seen.contains(item.path), "Unsafe/duplicate inventory path or hash");
        // Reject a file that is another listed file's parent, in either order.
        for (const auto &prior : seen) require(!item.path.startsWith(prior + '/') && !prior.startsWith(item.path + '/'), "Conflicting file inventory paths");
        seen.insert(item.path);
        total += item.size;
        require(total <= MaxTotal, "Decompressed size exceeds limit");
        m.files.append(std::move(item));
    }
    for (const auto &name : {"bin/citroen-c4-robot", "bin/c4-launcher", "bin/c4-updater"}) require(seen.contains(name), "Release missing native target");
    qsizetype shaderCount = 0;
    for (const auto &shader : authoredShaders()) shaderCount += seen.contains(shader) ? 1 : 0;
    require(shaderCount == 0 || shaderCount == authoredShaders().size(), "Authored Vulkan shader payload must be complete or absent");
    m.bytes = bytes;
    m.signature = signature;
    return m;
}
struct Config { QString repository, key, root, channel; };
Config config(const QString &path) {
    const auto data = object(localBytes(path, 65536));
    Config c{string(data, "repository"), string(data, "trusted_key"), string(data, "install_root"), string(data, "channel")};
    static const QRegularExpression repository(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,99}/[A-Za-z0-9][A-Za-z0-9_.-]{0,99}$"));
    require(repository.match(c.repository).hasMatch(), "Configure repository as owner/repo; no repository is assumed");
    require(QDir::isAbsolutePath(c.key) && QFileInfo(c.key).isFile(), "Configure trusted_key as an existing absolute pinned Ed25519 public PEM path");
    require(c.channel == "stable" || c.channel == "experimental", "Channel must be stable or experimental");
    require(QDir::isAbsolutePath(c.root) && c.root != "/" && QDir::cleanPath(c.root) == c.root, "install_root must be an absolute clean private managed directory, not /");
    const auto canonicalKey = QFileInfo(c.key).canonicalFilePath();
    const auto insideRoot = [&](const QString &candidate) {
        const auto clean = QDir::cleanPath(candidate);
        return clean == c.root || clean.startsWith(c.root + '/');
    };
    require(!canonicalKey.isEmpty() && !insideRoot(c.key) && !insideRoot(canonicalKey),
            "The independently pinned trust key must reside outside the managed installation tree");
    require(!insideRoot(QFileInfo(path).absoluteFilePath()) && !insideRoot(QFileInfo(path).canonicalFilePath()),
            "Updater configuration must reside outside the managed installation tree");
    c.key = canonicalKey;
    return c;
}
void noSymlinks(const QString &path) {
    QString current;
    for (const auto &part : path.split('/', Qt::SkipEmptyParts)) {
        current += '/' + part;
        struct stat st{};
        if (::lstat(QFile::encodeName(current).constData(), &st) == 0) require(S_ISDIR(st.st_mode), "Install path component is not a real directory: " + current);
        else require(errno == ENOENT, "Cannot inspect install path: " + current);
    }
}
void privateRoot(const QString &root) {
    noSymlinks(root);
    QString ancestor;
    for (const auto &part : root.split('/', Qt::SkipEmptyParts)) {
        ancestor += '/' + part;
        struct stat parent{};
        if (::lstat(QFile::encodeName(ancestor).constData(), &parent) == 0) {
            require(parent.st_uid == 0 || parent.st_uid == ::geteuid(), "Install ancestor is owned by another user");
            require((parent.st_mode & 0022) == 0 || (parent.st_uid == 0 && (parent.st_mode & S_ISVTX)),
                    "Install ancestor is writable by other users without root-owned sticky protection");
        }
    }
    if (!QFileInfo::exists(root)) {
        require(QDir().mkpath(root), "Cannot create install root");
        require(::chmod(QFile::encodeName(root).constData(), 0700) == 0, "Cannot secure install root");
    }
    struct stat st{};
    require(::lstat(QFile::encodeName(root).constData(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == ::geteuid() && (st.st_mode & 0077) == 0, "install_root must be owned by this user with permissions 0700");
    const auto versions = root + "/versions";
    noSymlinks(versions);
    require(QDir().mkpath(versions), "Cannot create versions directory");
}
bool trustedUrl(const QUrl &url) {
    static const QSet<QString> hosts{"api.github.com", "github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"};
    return url.isValid() && url.scheme() == "https" && url.userInfo().isEmpty() &&
           (url.port() == -1 || url.port() == 443) && hosts.contains(url.host());
}
void stderrEvent(const QJsonObject &event) {
    const auto bytes = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stderr);
    std::fflush(stderr);
}
void updateStage(const char *stage) {
    stderrEvent({{"event", "update_stage"}, {"stage", QString::fromLatin1(stage)}});
}
struct PackageProgress {
    qint64 total, received = 0, reported = 0;
    QElapsedTimer elapsed;
    explicit PackageProgress(qint64 size) : total(size) {
        elapsed.start();
        report();
    }
    void report() {
        stderrEvent({{"event", "download_progress"}, {"received_bytes", received}, {"total_bytes", total}});
        reported = received;
        elapsed.restart();
    }
    void wrote(qint64 count) {
        require(count > 0 && count <= total - received, "Package transfer exceeds signed size");
        received += count;
        if (received < total && (received - reported >= MiB || elapsed.elapsed() >= 100)) report();
    }
    void finish() {
        require(received == total, "Package transfer size mismatch");
        report();
    }
};
void download(QNetworkAccessManager &manager, QUrl url, QIODevice &output, qint64 cap, PackageProgress *progress = nullptr) {
    for (int redirects = 0; redirects <= 5; ++redirects) {
        checkCancel();
        require(trustedUrl(url), "Only HTTPS GitHub release endpoints are permitted");
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "CitroenC4Robot-Native-Updater");
        request.setRawHeader("Accept", url.host() == "api.github.com" ? "application/vnd.github+json" : "application/octet-stream");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        auto ssl = QSslConfiguration::defaultConfiguration();
        ssl.setProtocol(QSsl::TlsV1_2OrLater);
        request.setSslConfiguration(ssl);
        std::unique_ptr<QNetworkReply> reply(manager.get(request));
        reply->setReadBufferSize(65536);
        QEventLoop loop;
        QTimer timer, cancelTimer;
        timer.setSingleShot(true);
        QString failure;
        qint64 received = 0;
        auto drain = [&] {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status != 200) { reply->readAll(); return; }
            while (reply->bytesAvailable() > 0 && failure.isEmpty()) {
                const auto bytes = reply->read(65536);
                if (bytes.isEmpty()) failure = "Download read failed";
                else if (bytes.size() > cap - received) failure = "Download exceeds declared/allowed size";
                else if (output.write(bytes) != bytes.size()) failure = "Download storage write failed";
                else {
                    received += bytes.size();
                    if (progress) progress->wrote(bytes.size());
                }
                if (!failure.isEmpty()) reply->abort();
            }
        };
        QObject::connect(reply.get(), &QNetworkReply::readyRead, &loop, drain);
        QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, [&] { failure = "Download timed out"; reply->abort(); });
        QObject::connect(&cancelTimer, &QTimer::timeout, &loop, [&] { if (cancelled) { failure = "Download cancelled"; reply->abort(); } });
        timer.start(120000);
        cancelTimer.start(100);
        loop.exec();
        drain();
        require(failure.isEmpty(), failure);
        require(reply->error() == QNetworkReply::NoError, "HTTPS download failed: " + reply->errorString());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300 && status < 400) {
            url = url.resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
            continue;
        }
        require(status == 200, "HTTPS endpoint did not return 200");
        return;
    }
    require(false, "Too many HTTPS redirects");
}
QByteArray fetch(QNetworkAccessManager &manager, const QUrl &url, qint64 cap) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    require(buffer.open(QIODevice::WriteOnly), "Cannot open download buffer");
    download(manager, url, buffer, cap);
    return bytes;
}
struct Release { Manifest metadata; QString notes, url; QUrl package; QString local; };
Release findRelease(QNetworkAccessManager &manager, const Config &c, const QString &offline) {
    if (!offline.isEmpty()) {
        require(QDir::isAbsolutePath(offline) && QFileInfo(offline).isDir(), "--release-dir must be an absolute local directory");
        auto m = manifest(localBytes(offline + "/manifest.json", 4 * MiB), localBytes(offline + "/manifest.sig", 64), c.key);
        require(c.channel == "experimental" || !m.version.contains('-'), "Prerelease is incompatible with stable channel");
        return {std::move(m), "Offline signed release", "", {}, offline + "/native-linux-x86_64.tar.gz"};
    }
    // ponytail: inspect the newest 100 GitHub releases; paginate if this project's release cadence outgrows that window.
    const auto bytes = fetch(manager, QUrl("https://api.github.com/repos/" + c.repository + "/releases?per_page=100"), 8 * MiB);
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && doc.isArray(), "Invalid GitHub release response");
    QString best;
    QJsonObject selected;
    QHash<QString, QUrl> chosenAssets;
    for (const auto &value : doc.array()) {
        const auto release = value.toObject();
        if (!release.value("draft").isBool() || release.value("draft").toBool() || !release.value("prerelease").isBool()) continue;
        QString version = release.value("tag_name").toString();
        if (version.startsWith('v')) version.remove(0, 1);
        if (!validVersion(version) || (c.channel == "stable" && (release.value("prerelease").toBool() || version.contains('-')))) continue;
        QHash<QString, QUrl> assets;
        for (const auto &assetValue : release.value("assets").toArray()) {
            const auto asset = assetValue.toObject();
            const auto name = asset.value("name").toString();
            if (name == "manifest.json" || name == "manifest.sig" || name == "native-linux-x86_64.tar.gz") {
                require(!assets.contains(name), "Duplicate release asset names");
                const QUrl url(asset.value("browser_download_url").toString());
                require(trustedUrl(url) && url.host() == "github.com" && url.path().startsWith('/' + c.repository + "/releases/download/"), "Invalid GitHub asset URL");
                assets.insert(name, url);
            }
        }
        if (assets.size() != 3 || (!best.isEmpty() && compareVersion(version, best) <= 0)) continue;
        best = version;
        selected = release;
        chosenAssets = assets;
    }
    require(!best.isEmpty(), "No compatible signed native Linux x86_64 release found");
    auto m = manifest(fetch(manager, chosenAssets.value("manifest.json"), 4 * MiB), fetch(manager, chosenAssets.value("manifest.sig"), 64), c.key);
    require(m.version == best, "Signed version differs from GitHub tag");
    return {std::move(m), selected.value("body").toString(), selected.value("html_url").toString(), chosenAssets.value("native-linux-x86_64.tar.gz"), {}};
}
QString activeVersion(const QString &root, const QString &link = "current") {
    const auto path = root + '/' + link;
    struct stat st{};
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0) { require(errno == ENOENT, "Cannot inspect version link"); return {}; }
    require(S_ISLNK(st.st_mode), "Version activation path is not a symlink");
    std::array<char, 512> bytes{};
    const auto count = ::readlink(QFile::encodeName(path).constData(), bytes.data(), bytes.size());
    require(count > 0 && static_cast<size_t>(count) < bytes.size(), "Invalid version activation link");
    const auto target = QString::fromUtf8(bytes.data(), static_cast<qsizetype>(count));
    require(target.startsWith("versions/") && validVersion(target.mid(9)), "Unsafe version activation link");
    return target.mid(9);
}
Manifest installed(const Config &c, const QString &version, bool verifyFiles) {
    const auto directory = c.root + "/versions/" + version;
    noSymlinks(directory);
    auto m = manifest(localBytes(directory + "/.c4-manifest.json", 4 * MiB), localBytes(directory + "/.c4-manifest.sig", 64), c.key);
    require(m.version == version, "Installed signed manifest version mismatch");
    if (verifyFiles) {
        QSet<QString> expected{".c4-manifest.json", ".c4-manifest.sig"};
        QSet<QString> directories;
        for (const auto &file : m.files) {
            expected.insert(file.path);
            QString parent = file.path;
            while (parent.contains('/')) {
                parent.truncate(parent.lastIndexOf('/'));
                directories.insert(parent);
            }
            const auto path = directory + '/' + file.path;
            noSymlinks(QFileInfo(path).absolutePath());
            struct stat st{};
            require(::lstat(QFile::encodeName(path).constData(), &st) == 0 && S_ISREG(st.st_mode) &&
                    st.st_nlink == 1 && (st.st_mode & 07777) == (executablePath(file.path) ? 0755 : 0644) &&
                    fileDigest(path, file.size) == file.sha, "Installed file integrity/mode failure: " + file.path);
        }
        const std::filesystem::path base(directory.toStdString());
        for (const auto &entry : std::filesystem::recursive_directory_iterator(base)) {
            checkCancel();
            const auto relative = QString::fromStdString(entry.path().lexically_relative(base).generic_string());
            struct stat st{};
            const auto full = QString::fromStdString(entry.path().string());
            require(::lstat(QFile::encodeName(full).constData(), &st) == 0, "Cannot inspect installed inventory");
            if (S_ISDIR(st.st_mode)) require(directories.contains(relative), "Unlisted installed directory");
            else require(S_ISREG(st.st_mode) && st.st_nlink == 1 && expected.contains(relative), "Unlisted/link/special installed entry");
        }
    }
    return m;
}
void durableFile(QFile &f) {
    require(f.flush() && ::fsync(f.handle()) == 0, "Cannot flush update file");
}
void writeFile(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    require(f.open(QIODevice::WriteOnly | QIODevice::NewOnly) && f.write(bytes) == bytes.size(), "Cannot write staged metadata");
    durableFile(f);
}
void syncDirectory(const QString &directory) {
    const int fd = ::open(QFile::encodeName(directory).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    require(fd >= 0, "Cannot open directory for durability");
    const int result = ::fsync(fd);
    ::close(fd);
    require(result == 0, "Cannot sync installation directory");
}
void extract(const QString &package, const QString &destination, const Manifest &m) {
    std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    require(reader != nullptr, "Cannot allocate archive reader");
    archive_read_support_filter_gzip(reader.get());
    archive_read_support_format_tar(reader.get());
    require(archive_read_open_filename(reader.get(), QFile::encodeName(package).constData(), 65536) == ARCHIVE_OK, "Cannot open native tar package");
    QHash<QString, const FileRecord *> expected;
    for (const auto &file : m.files) expected.insert(file.path, &file);
    QSet<QString> seen;
    archive_entry *entry = nullptr;
    int status = 0;
    std::array<char, 65536> bytes{};
    while ((status = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        checkCancel();
        const auto raw = archive_entry_pathname(entry);
        require(raw != nullptr, "Archive path missing");
        const auto path = QString::fromUtf8(raw);
        require(path.toUtf8() == QByteArray(raw) && payloadPath(path) && expected.contains(path) && !seen.contains(path), "Unsafe, duplicate or unlisted archive entry");
        require(archive_entry_filetype(entry) == AE_IFREG && archive_entry_symlink(entry) == nullptr && archive_entry_hardlink(entry) == nullptr && archive_entry_sparse_count(entry) == 0, "Archive links/sparse/special entries are forbidden");
        const auto &record = *expected.value(path);
        require(archive_entry_size(entry) == record.size, "Archive entry size mismatch");
        const auto full = destination + '/' + path;
        require(QDir().mkpath(QFileInfo(full).absolutePath()), "Cannot create staged directory");
        QFile f(full);
        require(f.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Cannot create staged file");
        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 size = 0;
        la_ssize_t count = 0;
        while ((count = archive_read_data(reader.get(), bytes.data(), bytes.size())) > 0) {
            checkCancel();
            size += count;
            require(size <= record.size && f.write(bytes.data(), count) == count, "Archive decompression/write exceeds declared size");
            hash.addData(QByteArrayView(bytes.data(), count));
        }
        require(count == 0 && size == record.size && QString::fromLatin1(hash.result().toHex()) == record.sha, "Extracted file hash/size mismatch");
        require(::fchmod(f.handle(), executablePath(path) ? 0755 : 0644) == 0, "Cannot set staged file permissions");
        durableFile(f);
        seen.insert(path);
    }
    require(status == ARCHIVE_EOF && seen.size() == expected.size(), "Malformed or incomplete archive");
    require(archive_read_close(reader.get()) == ARCHIVE_OK, "Archive close failed");
    writeFile(destination + "/.c4-manifest.json", m.bytes);
    writeFile(destination + "/.c4-manifest.sig", m.signature);
    // Persist nested directories before the top-level version rename.
    for (const auto &entry : std::filesystem::recursive_directory_iterator(destination.toStdString())) if (entry.is_directory()) syncDirectory(QString::fromStdString(entry.path().string()));
    syncDirectory(destination);
}
void activate(const QString &root, const QString &version, const QString &link) {
    const auto temporary = root + "/.activate-" + link;
    const auto final = root + '/' + link;
    activeVersion(root, link); // Never overwrite a pre-existing non-version file.
    // Stale temporary links only originate from an interrupted activation under our private locked root.
    struct stat st{};
    if (::lstat(QFile::encodeName(temporary).constData(), &st) == 0) {
        require(S_ISLNK(st.st_mode) && ::unlink(QFile::encodeName(temporary).constData()) == 0, "Unsafe activation temporary path");
    }
    require(::symlink(QFile::encodeName("versions/" + version).constData(), QFile::encodeName(temporary).constData()) == 0, "Cannot prepare activation link");
    require(::rename(QFile::encodeName(temporary).constData(), QFile::encodeName(final).constData()) == 0, "Atomic activation failed");
    syncDirectory(root);
}
void recoverActivation(const Config &c) {
    const auto path = c.root + "/.activation.json";
    if (!QFileInfo::exists(path) && !QFileInfo(path).isSymLink()) return;
    require(!QFileInfo(path).isSymLink(), "Unsafe activation journal");
    const auto data = object(localBytes(path, 4096));
    const auto from = string(data, "from"), to = string(data, "to");
    require((from.isEmpty() || validVersion(from)) && validVersion(to), "Invalid activation journal versions");
    const auto current = activeVersion(c.root);
    require(current == from || current == to, "Activation journal/current conflict");
    if (current == to) {
        installed(c, to, true);
        if (!from.isEmpty()) {
            activate(c.root, from, "previous");
        }
    }
    require(QFile::remove(path), "Cannot clear activation journal");
    syncDirectory(c.root);
}
void switchVersion(const Config &c, const QString &from, const QString &to) {
    checkCancel();
    activeVersion(c.root, "previous");
    QSaveFile journal(c.root + "/.activation.json");
    const auto bytes = QJsonDocument(QJsonObject{{"from", from}, {"to", to}}).toJson(QJsonDocument::Compact);
    require(journal.open(QIODevice::WriteOnly) && journal.write(bytes) == bytes.size() &&
            journal.flush() && ::fsync(journal.handle()) == 0 && journal.commit(), "Cannot persist activation journal");
    syncDirectory(c.root);
    activate(c.root, to, "current");
    // One atomic current rename is the commit point. The durable journal repairs
    // previous on the next invocation if power loss interrupts this step.
    recoverActivation(c);
}
bool prepareRelease(QNetworkAccessManager &manager, const Config &c, const Release &release, const QString &current) {
    const auto &m = release.metadata;
    require(current.isEmpty() || compareVersion(m.version, current) >= 0,
            "Downgrade refused; use explicit rollback for a preserved version");
    checkCancel();
    const auto final = c.root + "/versions/" + m.version;
    if (QFileInfo::exists(final) || QFileInfo(final).isSymLink()) {
        updateStage("verify");
        // main already fully authenticates current before dispatching either action.
        const auto preserved = installed(c, m.version, m.version != current);
        require(preserved.bytes == m.bytes && preserved.signature == m.signature,
                "Existing preserved version differs from this signed release; never overwrite");
        checkCancel();
        return true;
    }
    QTemporaryDir stage(c.root + "/.staging-XXXXXX");
    require(stage.isValid(), "Cannot create private staging directory");
    const auto package = stage.path() + "/package.tar.gz";
    PackageProgress progress(m.size);
    if (!release.local.isEmpty()) {
        QFile input(release.local), output(package);
        require(input.open(QIODevice::ReadOnly) && input.size() == m.size && output.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Cannot copy local release package or size mismatch");
        std::array<char, 65536> buffer{};
        qint64 total = 0;
        while (!input.atEnd()) {
            checkCancel();
            const auto count = input.read(buffer.data(), static_cast<qint64>(buffer.size()));
            total += count;
            require(count > 0 && total <= m.size && output.write(buffer.data(), count) == count, "Local package read/write failed");
            progress.wrote(count);
        }
        durableFile(output);
    } else {
        QFile output(package);
        require(output.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Cannot create download staging file");
        download(manager, release.package, output, m.size, &progress);
        durableFile(output);
    }
    progress.finish();
    updateStage("verify");
    require(fileDigest(package, m.size) == m.sha, "Package SHA256 mismatch");
    const auto tree = stage.path() + "/tree";
    require(QDir().mkdir(tree), "Cannot create extraction staging directory");
    updateStage("extract");
    extract(package, tree, m);
    updateStage("preserve");
    checkCancel();
    require(::rename(QFile::encodeName(tree).constData(), QFile::encodeName(final).constData()) == 0, "Cannot preserve staged version");
    syncDirectory(c.root + "/versions");
    checkCancel();
    return false;
}
QJsonObject install(QNetworkAccessManager &manager, const Config &c, const Release &release, const QString &current) {
    prepareRelease(manager, c, release, current);
    const auto &version = release.metadata.version;
    const bool updated = current != version;
    if (updated) switchVersion(c, current, version);
    return {{"version", version}, {"install_path", c.root + "/versions/" + version}, {"updated", updated}};
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    ::umask(0077);
    try {
        QString configPath, action, offline;
        const auto args = app.arguments();
        for (qsizetype i = 1; i < args.size(); ++i) {
            const auto arg = args[i];
            if (arg == "--config" || arg == "--release-dir") {
                require(i + 1 < args.size(), "Missing CLI option value");
                auto &target = arg == "--config" ? configPath : offline;
                require(target.isEmpty(), "Duplicate CLI option");
                target = args[++i];
            } else if (arg == "--check" || arg == "--download" || arg == "--install" || arg == "--rollback" || arg == "--verify-current") {
                require(action.isEmpty(), "Choose exactly one updater action");
                action = arg;
            } else require(false, "Unknown updater argument: " + arg);
        }
        require(!configPath.isEmpty() && !action.isEmpty(), "Usage: c4-updater --config PATH --check|--download|--install|--rollback|--verify-current [--release-dir ABSOLUTE_SIGNED_FIXTURE]");
        const auto c = config(configPath);
        privateRoot(c.root);
        QLockFile lock(c.root + "/.update.lock");
        lock.setStaleLockTime(0);
        require(lock.tryLock(0), "Another updater owns the install lock");
        recoverActivation(c);
        const auto current = activeVersion(c.root);
        if (!current.isEmpty() && action != "--rollback") installed(c, current, true);
        QJsonObject result;
        if (action == "--verify-current") {
            require(offline.isEmpty() && !current.isEmpty(), "--verify-current requires an active installed version and does not accept --release-dir");
            result = {{"version", current}, {"install_path", c.root + "/versions/" + current}, {"verified", true}};
        } else if (action == "--rollback") {
            require(offline.isEmpty(), "--release-dir is not valid for rollback");
            const auto previous = activeVersion(c.root, "previous");
            require(!current.isEmpty() && !previous.isEmpty() && current != previous, "No distinct preserved version is available for rollback");
            installed(c, previous, true);
            checkCancel();
            switchVersion(c, current, previous);
            result = {{"version", previous}, {"install_path", c.root + "/versions/" + previous}, {"rolled_back", true}};
        } else {
            QNetworkAccessManager manager;
            const auto release = findRelease(manager, c, offline);
            if (action == "--check") result = {{"release", release.url}, {"version", release.metadata.version}, {"notes", release.notes}, {"installed_version", current}, {"update_available", current.isEmpty() || compareVersion(release.metadata.version, current) > 0}};
            else if (action == "--download") {
                const bool already = prepareRelease(manager, c, release, current);
                const auto path = QFileInfo(c.root + "/versions/" + release.metadata.version).canonicalFilePath();
                require(!path.isEmpty(), "Preserved candidate path is unavailable");
                result = {{"version", release.metadata.version}, {"download_path", path}, {"downloaded", true}, {"already_downloaded", already}};
            }
            else result = install(manager, c, release, current);
        }
        const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
        std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
        return 0;
    } catch (const std::exception &error) {
        const auto bytes = QJsonDocument(QJsonObject{{"error", QString::fromUtf8(error.what())}}).toJson(QJsonDocument::Compact) + '\n';
        std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
        return 1;
    }
}
