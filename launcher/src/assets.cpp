#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <elf.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr qint64 ManifestLimit = 64 * 1024 * 1024;
const QString ManifestName = QStringLiteral("asset-manifest.json");

class FileDescriptor {
public:
    explicit FileDescriptor(int fd = -1) : fd_(fd) {}
    ~FileDescriptor() { if (fd_ >= 0) ::close(fd_); }
    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;
    int get() const { return fd_; }
    int release() { const int fd = fd_; fd_ = -1; return fd; }
    void reset(int fd) { if (fd_ >= 0) ::close(fd_); fd_ = fd; }
private:
    int fd_;
};

// Walk the original components, not a cleaned path: cleaning could hide a
// symlink followed by "..". Every directory open rejects symlinks atomically.
int openDirectory(const QString &path)
{
    if (path.isEmpty() || path.contains(QChar(0)))
        return -1;
    FileDescriptor directory(::open(path.startsWith('/') ? "/" : ".",
                                    O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (directory.get() < 0)
        return -1;
    const auto components = path.split('/', Qt::SkipEmptyParts);
    for (const QString &component : components) {
        const QByteArray name = QFile::encodeName(component);
        const int next = ::openat(directory.get(), name.constData(),
                                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0)
            return -1;
        directory.reset(next);
    }
    return directory.release();
}

bool isSha256(const QJsonValue &value)
{
    if (!value.isString())
        return false;
    const QString text = value.toString();
    if (text.size() != 64)
        return false;
    for (QChar character : text) {
        const ushort c = character.unicode();
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

bool isPayloadPath(const QString &path)
{
    if (path.isEmpty() || path == ManifestName || path.contains('\\') || path.contains(':'))
        return false;
    for (QChar character : path) {
        if (character.unicode() < 32 || character.unicode() == 127)
            return false;
    }
    const auto components = path.split('/');
    for (const QString &component : components) {
        if (component.isEmpty() || component == QStringLiteral(".") || component == QStringLiteral(".."))
            return false;
    }
    return true;
}

// QJsonDocument otherwise collapses duplicate object keys and may round numeric
// tokens. Validate their original spelling before trusting the parsed values.
bool strictJsonTokens(const QByteArray &json)
{
    QList<QSet<QString>> keys;
    for (qsizetype i = 0; i < json.size(); ++i) {
        const char c = json[i];
        if (c == '{') {
            keys.append(QSet<QString>{});
        } else if (c == '}') {
            if (keys.isEmpty())
                return false;
            keys.removeLast();
        } else if (c == '"') {
            const qsizetype start = ++i;
            while (i < json.size() && json[i] != '"') {
                if (json[i] == '\\')
                    ++i;
                ++i;
            }
            if (i >= json.size())
                return false;
            qsizetype next = i + 1;
            while (next < json.size() && (json[next] == ' ' || json[next] == '\t'
                                         || json[next] == '\r' || json[next] == '\n'))
                ++next;
            if (next < json.size() && json[next] == ':') {
                if (keys.isEmpty())
                    return false;
                const QByteArray token = json.mid(start, i - start);
                const QString key = token.contains('\\')
                    ? QJsonDocument::fromJson(QByteArray("[\"") + token + "\"]").array().at(0).toString()
                    : QString::fromUtf8(token);
                if (keys.last().contains(key))
                    return false;
                keys.last().insert(key);
            }
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            const qsizetype start = i;
            while (i + 1 < json.size()) {
                const char next = json[i + 1];
                if (next == ',' || next == '}' || next == ']' || next == ' '
                    || next == '\t' || next == '\r' || next == '\n')
                    break;
                ++i;
            }
            bool ok = false;
            json.mid(start, i - start + 1).toLongLong(&ok, 10);
            if (!ok)
                return false;
        }
    }
    return keys.isEmpty();
}

struct Payload {
    qint64 size;
    QByteArray sha256;
};

QString validateOriginalRecords(const QHash<QString, Payload> &files)
{
    QFile catalogFile(QStringLiteral(":/assets/original-assets.json"));
    if (!catalogFile.open(QIODevice::ReadOnly))
        return QStringLiteral("Cannot read embedded original asset catalog");
    const QByteArray bytes = catalogFile.read(ManifestLimit + 1);
    QJsonParseError parseError;
    const QJsonDocument catalog = QJsonDocument::fromJson(bytes, &parseError);
    if (catalogFile.error() != QFileDevice::NoError || bytes.size() > ManifestLimit
        || parseError.error != QJsonParseError::NoError || !catalog.isObject()
        || !strictJsonTokens(bytes)
        || catalog.object().value(QStringLiteral("schema_version")).toInteger(-1) != 1
        || !catalog.object().value(QStringLiteral("files")).isArray())
        return QStringLiteral("Invalid embedded original asset catalog");
    const QJsonArray originals = catalog.object().value(QStringLiteral("files")).toArray();
    if (originals.size() != 73 || files.size() != originals.size())
        return QStringLiteral("Directory asset manifest must contain all 73 original payload files");
    QSet<QString> paths;
    for (const QJsonValue &record : originals) {
        const QJsonObject file = record.toObject();
        const QString path = file.value(QStringLiteral("path")).toString();
        const QJsonValue size = file.value(QStringLiteral("size"));
        const QJsonValue sha256 = file.value(QStringLiteral("sha256"));
        if (file.size() != 3 || !path.startsWith(QStringLiteral("app/")) || !isPayloadPath(path)
            || !size.isDouble() || size.toInteger(-1) < 0 || !isSha256(sha256)
            || paths.contains(path))
            return QStringLiteral("Invalid embedded original asset catalog entry");
        paths.insert(path);
        const auto expected = files.constFind(path);
        if (expected == files.constEnd() || expected->size != size.toInteger()
            || expected->sha256 != sha256.toString().toLatin1())
            return QStringLiteral("Directory asset manifest does not match original payload: %1").arg(path);
    }
    return {};
}

QString hashPayload(int directory, const char *name, const QString &path, const Payload &expected)
{
    FileDescriptor descriptor(::openat(directory, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    struct stat info {};
    if (descriptor.get() < 0 || ::fstat(descriptor.get(), &info) != 0 || !S_ISREG(info.st_mode))
        return QStringLiteral("Cannot safely open regular payload: %1").arg(path);
    if (info.st_size != expected.size)
        return QStringLiteral("Payload size mismatch: %1").arg(path);
    QFile file;
    if (!file.open(descriptor.get(), QIODevice::ReadOnly, QFileDevice::DontCloseHandle))
        return QStringLiteral("Cannot read payload: %1").arg(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 256 * 1024> buffer;
    qint64 total = 0;
    for (;;) {
        const qint64 count = file.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (count < 0)
            return QStringLiteral("Payload read failed: %1").arg(path);
        if (count == 0)
            break;
        if (count > expected.size - total)
            return QStringLiteral("Payload changed size while reading: %1").arg(path);
        total += count;
        hash.addData(QByteArrayView(buffer.data(), static_cast<qsizetype>(count)));
    }
    if (total != expected.size || hash.result().toHex() != expected.sha256)
        return QStringLiteral("Payload size or SHA-256 mismatch: %1").arg(path);
    return {};
}

QString validateDirectory(int directory, const QString &prefix, QHash<QString, Payload> &remaining,
                          const struct stat &manifestInfo)
{
    FileDescriptor copy(::fcntl(directory, F_DUPFD_CLOEXEC, 0));
    if (copy.get() < 0)
        return QStringLiteral("Cannot inspect asset directory: %1").arg(prefix);
    std::unique_ptr<DIR, decltype(&::closedir)> entries(::fdopendir(copy.get()), &::closedir);
    if (!entries)
        return QStringLiteral("Cannot enumerate asset directory: %1").arg(prefix);
    copy.release();
    for (;;) {
        errno = 0;
        const dirent *entry = ::readdir(entries.get());
        if (!entry) {
            if (errno != 0)
                return QStringLiteral("Asset directory enumeration failed: %1").arg(prefix);
            break;
        }
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;
        const QString name = QFile::decodeName(entry->d_name);
        const QString path = prefix.isEmpty() ? name : prefix + '/' + name;
        struct stat info {};
        if (::fstatat(directory, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0)
            return QStringLiteral("Cannot inspect asset entry: %1").arg(path);
        if (S_ISLNK(info.st_mode))
            return QStringLiteral("Symbolic links are forbidden in assets: %1").arg(path);
        if (S_ISDIR(info.st_mode)) {
            FileDescriptor child(::openat(directory, entry->d_name,
                                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            if (child.get() < 0)
                return QStringLiteral("Cannot safely open asset directory: %1").arg(path);
            const QString error = validateDirectory(child.get(), path, remaining, manifestInfo);
            if (!error.isEmpty())
                return error;
        } else {
            if (!S_ISREG(info.st_mode))
                return QStringLiteral("Nonregular asset entry is forbidden: %1").arg(path);
            if (path == ManifestName) {
                if (info.st_dev != manifestInfo.st_dev || info.st_ino != manifestInfo.st_ino)
                    return QStringLiteral("Asset manifest changed during validation");
                continue;
            }
            auto expected = remaining.find(path);
            if (expected == remaining.end() || QFile::encodeName(name) != entry->d_name)
                return QStringLiteral("Unexpected asset file: %1").arg(path);
            const QString error = hashPayload(directory, entry->d_name, path, expected.value());
            if (!error.isEmpty())
                return error;
            remaining.erase(expected);
        }
    }
    return {};
}
} // namespace

QString validateAssets(const QString &root)
{
    FileDescriptor directory(openDirectory(root));
    if (directory.get() < 0)
        return QStringLiteral("Asset root must be a readable directory without symlink parents");
    FileDescriptor descriptor(::openat(directory.get(), "asset-manifest.json",
                                      O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    struct stat info {};
    if (descriptor.get() < 0 || ::fstat(descriptor.get(), &info) != 0 || !S_ISREG(info.st_mode))
        return QStringLiteral("A regular, non-symlink asset-manifest.json is required");
    if (info.st_size <= 0 || info.st_size > ManifestLimit)
        return QStringLiteral("Asset manifest must be nonempty and at most 64 MiB");
    QFile manifestFile;
    if (!manifestFile.open(descriptor.get(), QIODevice::ReadOnly, QFileDevice::DontCloseHandle))
        return QStringLiteral("Cannot read asset manifest");
    const QByteArray bytes = manifestFile.read(ManifestLimit + 1);
    if (manifestFile.error() != QFileDevice::NoError || bytes.size() != info.st_size)
        return QStringLiteral("Asset manifest read failed or its size changed");
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || !strictJsonTokens(bytes))
        return QStringLiteral("Asset manifest must be valid JSON with unique keys and exact integer numbers");
    const QJsonObject manifest = document.object();
    const qint64 schema = manifest.value(QStringLiteral("schema_version")).toInteger(-1);
    const bool directorySource = schema == 2;
    const QSet<QString> allowed = directorySource
        ? QSet<QString> {QStringLiteral("schema_version"), QStringLiteral("source"), QStringLiteral("files")}
        : QSet<QString> {QStringLiteral("schema_version"), QStringLiteral("installer_sha256"),
            QStringLiteral("installer_size"), QStringLiteral("inno_data_version"),
            QStringLiteral("extractor_sha256"), QStringLiteral("files")};
    for (auto field = manifest.begin(); field != manifest.end(); ++field) {
        if (!allowed.contains(field.key()))
            return QStringLiteral("Unknown asset manifest field: %1").arg(field.key());
    }
    if (directorySource) {
        const QJsonObject source = manifest.value(QStringLiteral("source")).toObject();
        const QJsonValue pathValue = source.value(QStringLiteral("path"));
        const QString path = pathValue.toString();
        if (manifest.size() != 3 || source.size() != 2
            || source.value(QStringLiteral("kind")).toString() != QStringLiteral("directory")
            || !pathValue.isString() || !path.startsWith('/') || QDir::cleanPath(path) != path
            || !manifest.value(QStringLiteral("files")).isArray())
            return QStringLiteral("Asset manifest has invalid schema version 2 directory metadata");
        for (const QChar character : path) {
            if (character.category() == QChar::Other_Control)
                return QStringLiteral("Directory source path must not contain control characters");
        }
    } else if (!manifest.value(QStringLiteral("schema_version")).isDouble()
        || schema != 1
        || !isSha256(manifest.value(QStringLiteral("installer_sha256")))
        || !manifest.value(QStringLiteral("installer_size")).isDouble()
        || manifest.value(QStringLiteral("installer_size")).toInteger(-1) <= 0
        || !manifest.value(QStringLiteral("inno_data_version")).isString()
        || manifest.value(QStringLiteral("inno_data_version")).toString().isEmpty()
        || (manifest.contains(QStringLiteral("extractor_sha256"))
            && !isSha256(manifest.value(QStringLiteral("extractor_sha256"))))
        || !manifest.value(QStringLiteral("files")).isArray())
        return QStringLiteral("Asset manifest has invalid schema version 1 metadata");
    const QJsonArray records = manifest.value(QStringLiteral("files")).toArray();
    if (records.isEmpty())
        return QStringLiteral("Asset manifest must list at least one payload file");
    QHash<QString, Payload> remaining;
    remaining.reserve(records.size());
    for (const QJsonValue &record : records) {
        if (!record.isObject())
            return QStringLiteral("Every asset manifest file entry must be an object");
        const QJsonObject file = record.toObject();
        const QJsonValue pathValue = file.value(QStringLiteral("path"));
        const QString path = pathValue.toString();
        const QJsonValue size = file.value(QStringLiteral("size"));
        if (file.size() != 3 || !pathValue.isString() || !isPayloadPath(path)
            || !size.isDouble() || size.toInteger(-1) < 0
            || !isSha256(file.value(QStringLiteral("sha256"))))
            return QStringLiteral("Invalid asset manifest file entry: %1").arg(path);
        if (remaining.contains(path))
            return QStringLiteral("Duplicate asset manifest path: %1").arg(path);
        remaining.insert(path, Payload {size.toInteger(), file.value(QStringLiteral("sha256")).toString().toLatin1()});
    }
    if (directorySource) {
        const QString error = validateOriginalRecords(remaining);
        if (!error.isEmpty())
            return error;
    }
    const QString error = validateDirectory(directory.get(), {}, remaining, info);
    if (!error.isEmpty())
        return error;
    if (!remaining.isEmpty())
        return QStringLiteral("Missing asset file: %1").arg(remaining.constBegin().key());
    return {};
}

bool isNativeElf(const QString &path)
{
#if !defined(__linux__) || (!defined(__x86_64__) && !defined(__aarch64__))
    Q_UNUSED(path);
    return false;
#else
    if (path.isEmpty() || path.contains(QChar(0)) || !QFileInfo(path).isExecutable())
        return false;
    const qsizetype slash = path.lastIndexOf('/');
    const QString parent = slash < 0 ? QStringLiteral(".")
        : slash == 0 ? QStringLiteral("/") : path.left(slash);
    const QString name = slash < 0 ? path : path.mid(slash + 1);
    if (name.isEmpty())
        return false;
    FileDescriptor directory(openDirectory(parent));
    if (directory.get() < 0)
        return false;
    const QByteArray encoded = QFile::encodeName(name);
    FileDescriptor descriptor(::openat(directory.get(), encoded.constData(),
                                      O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    struct stat info {};
    if (descriptor.get() < 0 || ::fstat(descriptor.get(), &info) != 0 || !S_ISREG(info.st_mode)
        || !(info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
        return false;
    QFile file;
    if (!file.open(descriptor.get(), QIODevice::ReadOnly, QFileDevice::DontCloseHandle))
        return false;
    std::array<unsigned char, 64> header {};
    if (file.read(reinterpret_cast<char *>(header.data()), header.size()) != static_cast<qint64>(header.size()))
        return false;
    const auto little16 = [&header](std::size_t offset) {
        return static_cast<unsigned>(header[offset]) | (static_cast<unsigned>(header[offset + 1]) << 8);
    };
    if (std::memcmp(header.data(), ELFMAG, SELFMAG) != 0
        || header[EI_CLASS] != ELFCLASS64 || header[EI_DATA] != ELFDATA2LSB
        || header[EI_VERSION] != EV_CURRENT || little16(52) != header.size()
        || (little16(16) != ET_EXEC && little16(16) != ET_DYN)
        || header[20] != EV_CURRENT || header[21] != 0 || header[22] != 0 || header[23] != 0)
        return false;
#if defined(__x86_64__)
    return little16(18) == EM_X86_64;
#else
    return little16(18) == EM_AARCH64;
#endif
#endif
}
