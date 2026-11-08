#include "integration.hpp"

#include <QApplication>
#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QPixmap>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QVariant>
#include <QtConcurrent/QtConcurrentRun>

#include <array>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <QMap>
#include <cmath>
#include <cctype>
#include <functional>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>

QString validateAssets(const QString &root);
bool isNativeElf(const QString &path);

namespace {
QString configPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/launcher.json";
}

QJsonObject jsonFile(const QString &path, QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = path + ": " + file.errorString();
        return {};
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        error = path + ": expected JSON object: " + parse.errorString();
        return {};
    }
    return document.object();
}

QString digest(const QString &path, QString &error) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
        error = "Cannot hash " + path + ": " + file.errorString();
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool graphicsMatch(const QJsonObject &config) {
    return config.value("width").toInt() == 640 && config.value("height").toInt() == 480
        && config.value("frame_limit").toInt() == 75
        && config.value("vsync").isBool() && !config.value("vsync").toBool()
        && config.value("fullscreen").isBool() && !config.value("fullscreen").toBool();
}

struct Descriptor {
    int value;
    explicit Descriptor(int fd) : value(fd) {}
    ~Descriptor() { if (value >= 0) ::close(value); }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
};

bool sameFile(const struct stat &a, const struct stat &b) {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_mode == b.st_mode
        && a.st_nlink == b.st_nlink && a.st_size == b.st_size
        && a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec
        && a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}

bool safeSourceName(const QByteArray &name) {
    if (name.isEmpty() || name == "." || name == ".." || name.contains('\\') || name.contains(':')
        || QString::fromUtf8(name).toUtf8() != name) return false;
    for (const unsigned char character : name)
        if (character < 32 || character == 127) return false;
    return true;
}

QJsonObject directorySnapshot(const QString &path) {
    if (!QDir::isAbsolutePath(path))
        return {{"error", "Original application directory must be an absolute path"}};
    Descriptor root(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    for (const auto &component : path.split('/', Qt::SkipEmptyParts)) {
        const QByteArray name = component.toUtf8();
        if (!safeSourceName(name))
            return {{"error", "Unsafe source directory path component: " + component}};
        const int next = ::openat(root.value, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0)
            return {{"error", "Cannot open nonsymlink source directory: " + path + ": " + QString::fromLocal8Bit(std::strerror(errno))}};
        ::close(root.value);
        root.value = next;
    }
    QString error;
    QMap<QString, QJsonObject> inventory;
    QStringList directories;
    std::function<bool(int, const QString &)> scan = [&](int fd, const QString &relative) {
        struct stat before{}, after{};
        if (::fstat(fd, &before) != 0) { error = "Cannot stat source directory: " + relative; return false; }
        Descriptor iterator(::fcntl(fd, F_DUPFD_CLOEXEC, 0));
        std::unique_ptr<DIR, decltype(&::closedir)> entries(
            iterator.value < 0 ? nullptr : ::fdopendir(iterator.value), &::closedir);
        if (!entries) { error = "Cannot enumerate source directory: " + relative; return false; }
        iterator.value = -1; // fdopendir owns the descriptor only after success.
        for (;;) {
            errno = 0;
            const auto *entry = ::readdir(entries.get());
            if (!entry) {
                if (errno != 0) { error = "Cannot finish source directory inventory: " + relative; return false; }
                break;
            }
            const QByteArray name(entry->d_name);
            if (name == "." || name == "..") continue;
            const QString child = relative.isEmpty() ? QString::fromUtf8(name) : relative + "/" + QString::fromUtf8(name);
            if (!safeSourceName(name)) {
                error = "Unsafe original source entry: " + child; return false;
            }
            struct stat listed{}, opened{}, final{};
            if (::fstatat(fd, name.constData(), &listed, AT_SYMLINK_NOFOLLOW) != 0) {
                error = "Cannot stat original source entry: " + child; return false;
            }
            if (S_ISDIR(listed.st_mode)) {
                Descriptor directory(::openat(fd, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
                if (directory.value < 0 || ::fstat(directory.value, &opened) != 0 || !sameFile(listed, opened)) {
                    error = "Original source directory changed or is a symlink: " + child; return false;
                }
                directories.append(child);
                if (!scan(directory.value, child)) return false;
            } else if (S_ISREG(listed.st_mode) && listed.st_nlink == 1) {
                Descriptor file(::openat(fd, name.constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
                if (file.value < 0 || ::fstat(file.value, &opened) != 0 || !sameFile(listed, opened)) {
                    error = "Original source file changed while opening: " + child; return false;
                }
                QCryptographicHash hash(QCryptographicHash::Sha256);
                std::array<char, 65536> buffer;
                qint64 size = 0;
                for (;;) {
                    const ssize_t count = ::read(file.value, buffer.data(), buffer.size());
                    if (count < 0 && errno == EINTR) continue;
                    if (count < 0) { error = "Cannot hash original source file: " + child; return false; }
                    if (count == 0) break;
                    size += count;
                    if (size > opened.st_size) { error = "Original source file grew during inventory: " + child; return false; }
                    hash.addData(QByteArrayView(buffer.data(), count));
                }
                if (::fstat(file.value, &final) != 0 || !sameFile(opened, final) || size != opened.st_size
                    || ::fstatat(fd, name.constData(), &final, AT_SYMLINK_NOFOLLOW) != 0 || !sameFile(opened, final)) {
                    error = "Original source file changed during inventory: " + child; return false;
                }
                inventory.insert(child, QJsonObject{{"path", "app/" + child}, {"size", size},
                    {"sha256", QString::fromLatin1(hash.result().toHex())}});
            } else {
                error = "Original source contains a symlink, special file or hardlink: " + child; return false;
            }
        }
        if (::fstat(fd, &after) != 0 || !sameFile(before, after)) {
            error = "Original source directory changed during inventory: " + relative; return false;
        }
        return true;
    };
    if (!scan(root.value, {})) return {{"error", error}};
    QJsonArray files;
    for (const auto &file : inventory) files.append(file);
    directories.sort();
    return {{"kind", "directory"}, {"path", path}, {"files", files},
        {"directories", QJsonArray::fromStringList(directories)}};
}

QJsonObject sourcePreservation(const QJsonObject &source) {
    if (source.value("kind").toString() == "directory") {
        const auto actual = directorySnapshot(source.value("path").toString());
        if (actual.contains("error")) return actual;
        if (actual != source) return {{"error", "Original application directory inventory or bytes changed during integration scenario"}};
        return actual;
    }
    QString error;
    const QString hash = digest(source.value("path").toString(), error);
    if (!error.isEmpty()) return {{"error", error}};
    if (hash != source.value("installer_sha256").toString())
        return {{"error", "Original installer changed during integration scenario"}};
    return source;
}

QJsonObject assetEvidence(const QString &root, const QJsonObject &source) {
    QString error = validateAssets(root);
    if (!error.isEmpty()) return {{"error", error}};
    const auto manifest = jsonFile(root + "/asset-manifest.json", error);
    if (!error.isEmpty()) return {{"error", error}};
    const auto files = manifest.value("files").toArray();
    if (source.value("kind").toString() == "directory") {
        const QJsonObject expectedSource{{"kind", "directory"}, {"path", source.value("path")}};
        if (manifest.size() != 3 || manifest.value("schema_version").toInt() != 2
            || !manifest.value("source").isObject() || manifest.value("source").toObject() != expectedSource
            || manifest.contains("installer_sha256") || manifest.contains("installer_size")
            || manifest.contains("inno_data_version") || manifest.contains("extractor_sha256"))
            return {{"error", "Validated directory manifest metadata is not truthful strict schema 2"}};
        QMap<QString, QJsonObject> inventory;
        for (const auto &file : source.value("files").toArray()) {
            const auto entry = file.toObject();
            inventory.insert(entry.value("path").toString(), entry);
        }
        const bool rawApp = inventory.contains("app/C4_Robot.exe");
        if (!rawApp && !inventory.contains("app/app/C4_Robot.exe"))
            return {{"error", "Actual original source has no raw or wrapped C4_Robot.exe anchor"}};
        const auto catalog = jsonFile(":/assets/original-assets.json", error);
        if (!error.isEmpty()) return {{"error", error}};
        QMap<QString, QJsonObject> expected, actual;
        for (const auto &file : catalog.value("files").toArray()) {
            const auto entry = file.toObject();
            const QString path = entry.value("path").toString();
            auto original = inventory.value(rawApp ? path : "app/" + path);
            original.insert("path", path);
            if (!path.startsWith("app/") || original != entry)
                return {{"error", "Trusted catalog record differs from the actual original source: " + path}};
            expected.insert(path, original);
        }
        for (const auto &file : files) {
            const auto entry = file.toObject();
            actual.insert(entry.value("path").toString(), entry);
        }
        if (expected.isEmpty() || files.size() != expected.size() || actual != expected)
            return {{"error", "Validated manifest differs from the complete actual original catalog inventory"}};
    } else {
        if (manifest.value("installer_sha256").toString() != source.value("installer_sha256").toString())
            return {{"error", "Validated manifest installer digest differs from the supplied original installer"}};
        if (files.size() < 73)
            return {{"error", "Actual validated original payload contains fewer than 73 files"}};
    }
    const QString hash = digest(root + "/asset-manifest.json", error);
    if (!error.isEmpty()) return {{"error", error}};
    return {{"manifest_sha256", hash}, {"files", files.size()}, {"manifest", manifest}};
}

class Scenario final : public QObject {
    enum class State {
        Setup, InitialOutput, InitialCancelled, ImportFinished,
        RepairOutput, RepairCancelled, RepairFinished, GraphicsProbeFinished, QualityProbeFinished, NativeFinished,
        FirstChecked, FirstInstalled, RejectedInstalled, SecondChecked, SecondDownloaded, SecondInstalled,
        RolledBack, Reinstalled, RestartFinished, Evidence, FailureCleanup
    };
    QMainWindow &window_;
    IntegrationOptions options_;
    QTimer timer_;
    QElapsedTimer total_, phaseClock_;
    QFutureWatcher<QJsonObject> evidence_;
    std::function<void(const QJsonObject &)> continuation_;
    State state_ = State::Setup;
    QString phase_ = "setup", firstAssets_, activeAssets_, firstManifestHash_;
    QJsonObject originalSource_;
    QString firstVersion_, secondVersion_, firstNative_, secondNative_;
    QString pendingError_, previousNative_, activeManifestHash_;
    QJsonArray historyBeforeRejection_;
    QByteArray nativeSettings_;
    QJsonObject configBeforeDownload_;
    QString previousBeforeDownload_;
    QJsonObject requestedQuality_{{"preset", "original"}};
    bool customExercised_ = false, readinessStarted_ = false, readinessObserved_ = false;
    QJsonArray checks_, screenshots_;
    bool finished_ = false, updates_ = false, signedChecks_ = false;

    template<class T> T *widget(const QString &name) {
        if (finished_ || !pendingError_.isEmpty()) return nullptr;
        for (auto *item : window_.findChildren<T *>())
            if (item->accessibleName() == name) return item;
        fail("Missing real widget with accessibleName: " + name);
        return nullptr;
    }
    template<class T> T *namedWidget(const QString &name) {
        if (finished_ || !pendingError_.isEmpty()) return nullptr;
        auto *control = window_.findChild<T *>(name);
        if (!control) fail("Missing real widget with objectName: " + name);
        return control;
    }
    bool show(QWidget *control) {
        if (!control) return false;
        auto *tabs = widget<QTabWidget>("Launcher sections");
        if (!tabs) return false;
        for (QWidget *parent = control; parent; parent = parent->parentWidget()) {
            const int index = tabs->indexOf(parent);
            if (index >= 0 && tabs->currentIndex() != index) {
                if (!tabs->isEnabled()) { fail("Cannot show disabled section for " + control->accessibleName()); return false; }
                tabs->setCurrentIndex(index);
                break;
            }
        }
        for (QWidget *parent = control; parent; parent = parent->parentWidget())
            if (auto *scroll = qobject_cast<QScrollArea *>(parent)) scroll->ensureWidgetVisible(control);
        if (!control->isVisible() || !control->isEnabled()) {
            fail("Widget is not shown and enabled: " + control->accessibleName());
            return false;
        }
        return true;
    }
    bool line(const QString &name, const QString &value) {
        auto *control = widget<QLineEdit>(name);
        if (!show(control)) return false;
        control->setText(value);
        return true;
    }
    bool number(const QString &name, int value) {
        auto *control = widget<QSpinBox>(name);
        if (!show(control)) return false;
        control->setValue(value);
        return true;
    }
    bool box(const QString &name, bool value) {
        auto *control = widget<QCheckBox>(name);
        if (!show(control)) return false;
        control->setChecked(value);
        return true;
    }
    bool choice(const QString &name, const QVariant &value) {
        auto *control = namedWidget<QComboBox>(name);
        if (!show(control)) return false;
        const int index = control->findData(value);
        if (index < 0) { fail("Missing semantic choice in " + name + ": " + value.toString()); return false; }
        const auto modelIndex = control->model()->index(index, control->modelColumn(), control->rootModelIndex());
        if (!control->model()->flags(modelIndex).testFlag(Qt::ItemIsEnabled)) {
            fail("Disabled semantic choice in " + name + ": " + value.toString()); return false;
        }
        control->setCurrentIndex(index);
        return !finished_ && pendingError_.isEmpty();
    }
    bool click(const QString &name) {
        auto *control = widget<QPushButton>(name);
        if (!show(control)) return false;
        control->click();
        return !finished_;
    }
    QJsonObject config() {
        QString error;
        const auto value = jsonFile(configPath(), error);
        if (!error.isEmpty()) fail(error);
        return value;
    }
    QString log() const {
        for (auto *control : window_.findChildren<QPlainTextEdit *>())
            if (control->accessibleName() == "Real operation log") return control->toPlainText();
        return "Real operation log widget missing";
    }
    bool check(bool passed, const QString &name, const QJsonObject &details = {}) {
        if (finished_ || !pendingError_.isEmpty()) return false;
        QJsonObject record{{"name", name}, {"passed", passed}, {"exercised", true}, {"details", details}};
        checks_.append(record);
        if (!passed) fail(name + ": " + QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact)));
        return passed;
    }
    bool screenshot(const QString &name) {
        if (finished_ || !pendingError_.isEmpty()) return false;
        const QString path = options_.root + "/" + name + ".png";
        if (!window_.isVisible() || !window_.grab().save(path, "PNG")) {
            fail("Cannot capture shown widget state: " + path);
            return false;
        }
        screenshots_.append(path);
        return true;
    }
    bool report(bool success, const QString &error) {
        QJsonObject report{{"status", success ? (updates_ ? "passed" : "partial") : "failed"},
            {"full_scenario_complete", success && updates_}, {"import_native_complete", success},
            {"updates_requested", updates_}, {"updates_exercised", signedChecks_}, {"phase", phase_}, {"elapsed_ms", total_.elapsed()},
            {"coverage", "Three-frame native-game startup plus separate finite 150-frame original-menu readiness and available quality override; not full interactive gameplay parity"},
            {"native_startup_complete", readinessStarted_}, {"native_menu_ready", readinessObserved_},
            {"error", error}, {"checks", checks_}, {"screenshots", screenshots_},
            {"widget_log", log()}, {"config_path", configPath()}, {"active_assets", activeAssets_},
            {"source_kind", options_.directory.isEmpty() ? "installer" : "directory"},
            {"scenario", options_.directory.isEmpty() ? "original-installer" : "original-installation-directory"}};
        if (options_.directory.isEmpty()) {
            report.insert("installer", options_.installer);
            report.insert("installer_sha256", originalSource_.value("installer_sha256"));
        } else {
            report.insert("source_directory", options_.directory);
            report.insert("original_source_inventory", originalSource_);
        }
        QSaveFile file(options_.root + "/report.json");
        const QByteArray data = QJsonDocument(report).toJson();
        const bool written = file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
        if (!written) fprintf(stderr, "Integration report write failed: %s\n", qPrintable(file.errorString()));
        return written;
    }
    void finish(bool success, const QString &error = {}) {
        if (finished_) return;
        finished_ = true;
        timer_.stop();
        const bool written = report(success, error);
        if (!success) fprintf(stderr, "Integration failed [%s]: %s\n%s\n", qPrintable(phase_), qPrintable(error), qPrintable(log()));
        QCoreApplication::exit(success && written ? 0 : 1);
    }
    void fail(const QString &error) {
        if (finished_ || !pendingError_.isEmpty()) return;
        pendingError_ = error;
        const QString image = options_.root + "/failure.png";
        if (window_.isVisible() && window_.grab().save(image, "PNG")) screenshots_.append(image);
        report(false, error);
        if (idle() && !evidence_.isRunning()) { finish(false, error); return; }
        state_ = State::FailureCleanup;
        phaseClock_.restart();
        // Let the real importer's SIGTERM handler kill/reap its extraction group.
        for (auto *control : window_.findChildren<QPushButton *>()) {
            if (control->accessibleName() == "Cancel operation" && control->isVisible() && control->isEnabled()) {
                control->click();
                break;
            }
        }
    }
    void enter(State state, const QString &phase) {
        if (finished_ || !pendingError_.isEmpty()) return;
        state_ = state;
        phase_ = phase;
        phaseClock_.restart();
    }
    void collect(const std::function<QJsonObject()> &job,
                 const std::function<void(const QJsonObject &)> &next) {
        if (finished_ || !pendingError_.isEmpty()) return;
        state_ = State::Evidence;
        continuation_ = next;
        evidence_.setFuture(QtConcurrent::run(job));
    }
    bool idle() const {
        return !window_.property("busy").toBool() && !window_.property("importRunning").toBool()
            && !window_.property("nativeRunning").toBool() && !window_.property("restartRunning").toBool()
            && !window_.property("graphicsProbeRunning").toBool();
    }
    bool operationOkay() {
        const QString error = window_.property("operationError").toString();
        if (!error.isEmpty()) { fail("Real operation failed: " + error); return false; }
        return true;
    }
    bool qualityMatch(const QJsonObject &saved) const {
        return saved.value("renderer").toString() == "opengl"
            && saved.value("rt_effects").isDouble() && saved.value("rt_effects").toInt(-1) == 0
            && saved.value("graphics").isObject() && saved.value("graphics").toObject() == requestedQuality_;
    }
    bool sourceMatch(const QJsonObject &saved) const {
        return saved.value("source_kind").toString() == (options_.directory.isEmpty() ? "installer" : "directory")
            && (options_.directory.isEmpty() ? saved.value("installer").toString() == options_.installer
                : saved.value("source_directory").toString() == options_.directory);
    }
    bool savedGraphics(const QString &name) {
        const auto saved = config();
        return !finished_ && check(graphicsMatch(saved) && qualityMatch(saved)
            && sourceMatch(saved)
            && saved.value("managed_root").toString() == options_.root + "/managed", name, {{"config", saved}});
    }
    bool previousUnchanged(const QString &name) {
        QString error;
        const auto saved = config();
        const QString hash = digest(firstAssets_ + "/asset-manifest.json", error);
        return !finished_ && check(error.isEmpty() && saved.value("assets").toString() == firstAssets_
            && hash == firstManifestHash_ && sourceMatch(saved), name,
            {{"active", saved.value("assets")}, {"manifest_sha256", hash}, {"error", error}});
    }
    bool activate(const QString &release, const QString &button, State state, const QString &phase) {
        if (!line("Offline signed release directory", release)) return false;
        enter(state, phase);
        return click(button);
    }
    bool installed(const QString &version, QString &destination, const QString &name) {
        const QString current = QFileInfo(options_.root + "/native/current").canonicalFilePath();
        const auto saved = config();
        const auto history = saved.value("history").toArray();
        const auto last = history.isEmpty() ? QJsonObject{} : history.last().toObject();
        if (!check(!current.isEmpty() && current.startsWith(options_.root + "/native/versions/")
            && QFileInfo(current).fileName() == version && isNativeElf(current + "/bin/c4-launcher")
            && isNativeElf(current + "/bin/citroen-c4-robot") && last.value("version").toString() == version
            && last.value("action").toString() == (state_ == State::RolledBack ? "--rollback" : "--install")
            && saved.value("assets").toString() == activeAssets_ && graphicsMatch(saved) && qualityMatch(saved)
            && sourceMatch(saved), name,
            {{"current", current}, {"history_entry", last}})) return false;
        destination = current;
        return screenshot(phase_);
    }
    void setup();
    void nativeEvidence();
    bool launchAvailableOverride();
    void continueAfterNative();
    void restartEvidence();
    void finalIntegrity();
    void rejectTamperedRelease();
    bool checkedRelease(const QString &version, const QString &name);
    void tick();
public:
    Scenario(QMainWindow &window, const IntegrationOptions &options) : QObject(&window), window_(window), options_(options) {
        total_.start();
        phaseClock_.start();
        connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
        connect(&evidence_, &QFutureWatcher<QJsonObject>::finished, this, [this] {
            if (finished_ || !pendingError_.isEmpty()) return;
            const auto result = evidence_.result();
            if (result.contains("error")) { fail(result.value("error").toString()); return; }
            const auto next = std::move(continuation_);
            next(result);
        });
        timer_.start(10);
    }
};

void Scenario::setup() {
    const QString root = QFileInfo(options_.root).canonicalFilePath();
    if (!check(window_.isVisible() && root == options_.root
        && root.startsWith(QFileInfo(QDir::tempPath()).canonicalFilePath() + "/c4-launcher-integration-")
        && QFileInfo(root + "/scenario-owner.json").isFile() && window_.property("integrationRoot").toString() == root
        && QDir::homePath().startsWith(root + "/")
        && QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) == root + "/data"
        && configPath().startsWith(root + "/"), "isolated_real_shown_launcher",
        {{"root", root}, {"home", QDir::homePath()}, {"config", configPath()}})) return;
    const int supplied = int(!options_.firstRelease.isEmpty()) + int(!options_.secondRelease.isEmpty()) + int(!options_.key.isEmpty());
    if (!check(supplied == 0 || supplied == 3, "release_inputs_all_or_none")
        || !check(options_.installer.isEmpty() != options_.directory.isEmpty(), "exactly_one_real_original_source")) return;
    updates_ = supplied == 3;
    if (!choice("importSource", options_.directory.isEmpty() ? "installer" : "directory")) return;
    if (options_.directory.isEmpty()) {
        if (!line("Original installer", options_.installer)) return;
    } else {
        auto *directory = namedWidget<QLineEdit>("originalDirectory");
        if (!directory || !check(directory->accessibleName() == "Original installation application directory",
            "original_directory_semantic_widget_binding")
            || !line("Original installation application directory", options_.directory)) return;
    }
    if (!line("Managed asset root", root + "/managed")
        || !box("Create user application-menu shortcut after validated import", true)
        || !number("Window width", 640) || !number("Window height", 480)
        || !number("Frame limit (0 = unlimited)", 75)
        || !box("VSync (driver/context permitting)", false)
        || !box("Fullscreen (display desktop size)", false)
        || !choice("qualityRenderer", "opengl") || !choice("qualityPreset", 0)
        || !line("Native versioned install root", root + "/native")
        || !box("Notify when signed update is available", false)
        || !box("Automatically install verified native updates", false)
        || !box("Prefer activated managed native version", true)) return;
    if (updates_ && (!line("Release repository owner/repo", "local/integration")
        || !line("Pinned Ed25519 public PEM key", options_.key))) return;
    if (!click("Save settings") || !operationOkay() || !savedGraphics("graphics_settings_saved")
        || !screenshot("01-settings")) return;
    if (!show(widget<QSpinBox>("Window width")) || !screenshot("01-graphics")) return;
    const auto saved = config();
    if (!check(saved.value("assets").toString().isEmpty(), "fresh_config_no_active_assets")) return;
    auto *gameField = widget<QLineEdit>("Native game ELF");
    auto *updaterField = widget<QLineEdit>("Native updater ELF");
    if (!gameField || !updaterField) return;
    const QString selectedGame = gameField->text().trimmed();
    const QString selectedUpdater = updaterField->text().trimmed();
    if (!check(!saved.contains("executable") && !saved.contains("updater")
        && isNativeElf(selectedGame) && isNativeElf(selectedUpdater),
        "validated_self_bundled_defaults_omitted_from_saved_config",
        {{"game_field", selectedGame}, {"updater_field", updaterField->text()}})) return;
    const auto options = options_;
    phase_ = options_.directory.isEmpty() ? "hash_original_installer" : "snapshot_entire_original_application_directory";
    collect([options, selectedGame, selectedUpdater] {
        QString error;
        QJsonObject source;
        if (!options.directory.isEmpty()) {
            source = directorySnapshot(options.directory);
            if (source.contains("error")) return source;
        } else {
            if (!QFileInfo(options.installer).isFile() || QFileInfo(options.installer).isSymLink())
                return QJsonObject{{"error", "Original installer is not a regular nonsymlink file"}};
            const QString hash = digest(options.installer, error);
            if (!error.isEmpty()) return QJsonObject{{"error", error}};
            source = {{"kind", "installer"}, {"path", options.installer}, {"installer_sha256", hash},
                {"installer_size", QFileInfo(options.installer).size()}};
        }
        if (!isNativeElf(selectedGame))
            return QJsonObject{{"error", "Consumer-selected game executable is not a real native ELF"}};
        QJsonObject result{{"source", source}};
        if (!options.firstRelease.isEmpty()) {
            if (!isNativeElf(selectedUpdater) || !QFileInfo(options.key).isFile())
                return QJsonObject{{"error", "Signed update inputs require a real updater ELF and supplied public key file"}};
            const auto first = jsonFile(options.firstRelease + "/manifest.json", error);
            if (!error.isEmpty()) return QJsonObject{{"error", error}};
            const auto second = jsonFile(options.secondRelease + "/manifest.json", error);
            if (!error.isEmpty()) return QJsonObject{{"error", error}};
            const QString a = first.value("version").toString(), b = second.value("version").toString();
            if (a.isEmpty() || b.isEmpty() || a == b)
                return QJsonObject{{"error", "Supply two real signed releases with distinct nonempty versions"}};
            result["first_version"] = a;
            result["second_version"] = b;
        }
        return result;
    }, [this](const QJsonObject &result) {
        originalSource_ = result.value("source").toObject();
        firstVersion_ = result.value("first_version").toString();
        secondVersion_ = result.value("second_version").toString();
        check(true, options_.directory.isEmpty() ? "original_installer_hashed" : "entire_actual_original_directory_snapshotted", result);
        enter(State::InitialOutput, "cancel_initial_import_after_real_output");
        click("Import and validate");
    });
}

void Scenario::tick() {
    if (finished_) return;
    if (state_ == State::FailureCleanup) {
        if (idle() && !evidence_.isRunning()) finish(false, pendingError_);
        else if (phaseClock_.elapsed() >= 15000)
            finish(false, pendingError_ + "; cancellation cleanup exceeded 15 seconds; an owned operation is still active");
        return;
    }
    if (total_.elapsed() >= 15 * 60 * 1000) {
        fail("15-minute total integration deadline exceeded; inspect widget_log and the last captured state");
        return;
    }
    if (state_ == State::Setup) { setup(); return; }
    if (state_ == State::Evidence) return;
    const bool cancelling = state_ == State::InitialOutput || state_ == State::InitialCancelled
        || state_ == State::RepairOutput || state_ == State::RepairCancelled || state_ == State::RejectedInstalled;
    if (!cancelling && !operationOkay()) return;
    if ((state_ == State::NativeFinished || state_ == State::RestartFinished
        || state_ == State::GraphicsProbeFinished || state_ == State::QualityProbeFinished) && phaseClock_.elapsed() > 120000) {
        fail("Native child did not finish within two minutes"); return;
    }
    switch (state_) {
    case State::InitialOutput:
    case State::RepairOutput: {
        if (window_.property("importRunning").toBool() && window_.property("importPhase").toString() == "output") {
            const bool initial = state_ == State::InitialOutput;
            if (initial) {
                if (!check(config().value("assets").toString().isEmpty(), "initial_import_output_before_activation")) return;
            } else if (!previousUnchanged("repair_real_output_preserves_active_manifest")) return;
            if (!screenshot(initial ? "02-import-real-output" : "05-repair-real-output")) return;
            enter(initial ? State::InitialCancelled : State::RepairCancelled,
                  initial ? "initial_import_cancelled" : "repair_cancelled");
            click("Cancel operation");
        } else if (idle()) {
            fail("Import finished before cancellation could observe real importer output; operation must remain active at cancellation");
        }
        return;
    }
    case State::InitialCancelled:
        if (!idle()) return;
        if (!check(config().value("assets").toString().isEmpty(), "cancelled_initial_import_never_activated")
            || !screenshot("03-import-cancelled")) return;
        {
            const auto source = originalSource_;
            collect([source] { return sourcePreservation(source); }, [this](const QJsonObject &result) {
                if (!check(true, "original_source_untouched_after_initial_cancellation", {{"source", result}})) return;
                window_.setProperty("operationError", QString{});
                enter(State::ImportFinished, "successful_import_and_validation");
                click("Import and validate");
            });
        }
        return;
    case State::ImportFinished: {
        if (!idle()) return;
        firstAssets_ = config().value("assets").toString();
        if (!check(firstAssets_.startsWith(options_.root + "/managed/assets/"), "real_import_activated_fresh_directory",
                   {{"assets", firstAssets_}})) return;
        const QString root = firstAssets_;
        const auto source = originalSource_;
        collect([root, source] {
            const auto preserved = sourcePreservation(source);
            return preserved.contains("error") ? preserved : assetEvidence(root, source);
        }, [this](const QJsonObject &result) {
            firstManifestHash_ = result.value("manifest_sha256").toString();
            const auto manifest = result.value("manifest").toObject();
            if (options_.directory.isEmpty()) {
                if (!check(manifest.value("installer_size").toDouble() == double(QFileInfo(options_.installer).size()),
                    "original_installer_digest_and_complete_73_file_tree",
                    {{"manifest_sha256", firstManifestHash_}, {"files", result.value("files")},
                     {"installer_sha256", manifest.value("installer_sha256")}, {"installer_size", manifest.value("installer_size")}})) return;
            } else if (!check(true, "actual_original_directory_catalog_bytes_strict_schema2_validated_and_source_preserved",
                {{"manifest_sha256", firstManifestHash_}, {"files", result.value("files")},
                 {"source", manifest.value("source")}, {"original_inventory_files", originalSource_.value("files").toArray().size()}})) return;
            if (!screenshot("04-import-validated")) return;
            enter(State::RepairOutput, "cancel_repair_after_real_output");
            click("Repair via fresh import");
        });
        return;
    }
    case State::RepairCancelled:
        if (!idle()) return;
        if (!previousUnchanged("cancelled_repair_no_activation_manifest_unchanged") || !screenshot("06-repair-cancelled")) return;
        {
            const auto source = originalSource_;
            collect([source] { return sourcePreservation(source); }, [this](const QJsonObject &result) {
                if (!check(true, "original_source_untouched_after_repair_cancellation", {{"source", result}})) return;
                window_.setProperty("operationError", QString{});
                enter(State::RepairFinished, "successful_fresh_repair");
                click("Repair via fresh import");
            });
        }
        return;
    case State::RepairFinished: {
        if (!idle()) return;
        activeAssets_ = config().value("assets").toString();
        if (!check(activeAssets_ != firstAssets_ && activeAssets_.startsWith(options_.root + "/managed/assets/"),
                   "repair_activated_distinct_fresh_directory", {{"before", firstAssets_}, {"after", activeAssets_}})) return;
        const QString current = activeAssets_, previous = firstAssets_, manifestHash = firstManifestHash_;
        const auto source = originalSource_;
        collect([current, previous, source, manifestHash] {
            const auto preserved = sourcePreservation(source);
            if (preserved.contains("error")) return preserved;
            auto result = assetEvidence(current, source);
            if (result.contains("error")) return result;
            const auto old = assetEvidence(previous, source);
            if (old.contains("error")) return old;
            if (old.value("manifest_sha256").toString() != manifestHash)
                return QJsonObject{{"error", "Successful repair changed the previously activated manifest"}};
            return result;
        }, [this](const QJsonObject &result) {
            check(true, "fresh_repair_validated_previous_manifest_and_tree_preserved", {{"manifest_sha256", result.value("manifest_sha256")}, {"files", result.value("files")}});
            const QString shortcut = options_.root + "/data/applications/citroen-c4-robot-native.desktop";
            QStringList desktopFiles;
            QDirIterator entries(options_.root, {"*.desktop"}, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (entries.hasNext()) desktopFiles.append(entries.next());
            QFile desktop(shortcut);
            const bool opened = desktop.open(QIODevice::ReadOnly);
            const QByteArray contents = opened ? desktop.readAll() : QByteArray{};
            if (!check(desktopFiles.size() == 1 && desktopFiles.first() == shortcut && !QFileInfo(shortcut).isSymLink()
                && contents.contains("[Desktop Entry]") && contents.contains("Exec=")
                && contents.contains(QCoreApplication::applicationFilePath().toUtf8()),
                "shortcut_created_only_in_isolated_xdg_applications", {{"path", shortcut}, {"desktop_files", QJsonArray::fromStringList(desktopFiles)}})
                || !screenshot("07-repair-validated") || !savedGraphics("graphics_preserved_after_import_and_repair")) return;
            enter(State::GraphicsProbeFinished, "probe_actual_native_graphics_pipelines");
            click("Probe native graphics pipelines");
        });
        return;
    }
    case State::GraphicsProbeFinished:
    case State::QualityProbeFinished: {
        if (!idle()) return;
        const auto capabilities = window_.property("graphicsCapabilities").toJsonObject();
        if (!check(window_.property("graphicsProbeError").toString().isEmpty()
            && capabilities.value("schema_version").toInt() == 1
            && capabilities.value("renderer").toString() == "opengl"
            && capabilities.value("capabilities").isObject() && capabilities.value("resolved").isObject(),
            "actual_native_graphics_pipeline_probe",
            {{"capabilities", capabilities}, {"error", window_.property("graphicsProbeError").toString()}})) return;
        auto *displayed = namedWidget<QWidget>("qualityCapabilities");
        if (!displayed || !show(displayed)
            || !check(displayed->property("nativeCapabilities").toJsonObject() == capabilities,
                "quality_widgets_display_actual_native_pipeline_capabilities")) return;
        const bool reprobe = state_ == State::QualityProbeFinished;
        const QString settingsPath = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + "/citroen-c4-robot/runtime.conf";
        QFile settings(settingsPath);
        const bool unchanged = reprobe
            ? settings.open(QIODevice::ReadOnly) && settings.readAll() == nativeSettings_
            : !QFileInfo::exists(settingsPath);
        if (!check(unchanged, reprobe ? "actual_capability_reprobe_preserved_native_settings_byte_identical"
            : "actual_capability_probe_did_not_create_native_settings", {{"path", settingsPath}})) return;
        if (reprobe) {
            if (!launchAvailableOverride()) continueAfterNative();
            return;
        }
        enter(State::NativeFinished, "actual_native_game_child");
        click("Launch native game");
        return;
    }
    case State::NativeFinished:
        if (!idle()) return;
        nativeEvidence();
        return;
    case State::FirstChecked:
        if (!idle()) return;
        if (!checkedRelease(firstVersion_, "first_signed_release_check") || !screenshot("09-first-release-checked")) return;
        activate(options_.firstRelease, "Install signed update", State::FirstInstalled, "first_signed_release_installed");
        return;
    case State::FirstInstalled:
        if (!idle()) return;
        if (!installed(firstVersion_, firstNative_, "first_signed_release_really_activated")) return;
        rejectTamperedRelease();
        return;
    case State::RejectedInstalled: {
        if (!idle()) return;
        const auto saved = config();
        const QString rejected = window_.property("operationError").toString();
        QString error;
        const QString hash = digest(activeAssets_ + "/asset-manifest.json", error);
        if (!check(rejected.contains("Package SHA256 mismatch")
            && QFileInfo(options_.root + "/native/current").canonicalFilePath() == firstNative_
            && QFileInfo(options_.root + "/native/previous").canonicalFilePath() == previousNative_
            && saved.value("history").toArray() == historyBeforeRejection_
            && saved.value("assets").toString() == activeAssets_ && graphicsMatch(saved) && qualityMatch(saved)
            && sourceMatch(saved)
            && error.isEmpty() && hash == activeManifestHash_
            && !QFileInfo(options_.root + "/native/versions/" + secondVersion_).exists(),
            "tampered_real_signed_package_rejected_without_activation_or_config_asset_changes",
            {{"updater_error", rejected}, {"manifest_sha256", hash}})
            || !screenshot("10-tampered-release-rejected")) return;
        window_.setProperty("operationError", QString{});
        activate(options_.secondRelease, "Check signed release", State::SecondChecked, "second_signed_release_checked");
        return;
    }
    case State::SecondChecked:
        if (!idle()) return;
        if (!checkedRelease(secondVersion_, "second_signed_release_check") || !screenshot("11-second-release-checked")) return;
        configBeforeDownload_ = config();
        previousBeforeDownload_ = QFileInfo(options_.root + "/native/previous").canonicalFilePath();
        enter(State::SecondDownloaded, "second_signed_release_downloaded_without_activation");
        click("Download signed update");
        return;
    case State::SecondDownloaded: {
        if (!idle()) return;
        const QString candidate = QFileInfo(options_.root + "/native/versions/" + secondVersion_).canonicalFilePath();
        QJsonObject response;
        const auto lines = log().split('\n');
        for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
            const auto document = QJsonDocument::fromJson(it->toUtf8());
            if (document.isObject() && document.object().contains("downloaded")) {
                response = document.object(); break;
            }
        }
        if (!check(response.size() == 4 && response.value("downloaded").isBool() && response.value("downloaded").toBool()
            && response.value("already_downloaded").isBool() && !response.value("already_downloaded").toBool()
            && response.value("version").toString() == secondVersion_
            && !candidate.isEmpty() && response.value("download_path").toString() == candidate
            && candidate == options_.root + "/native/versions/" + secondVersion_
            && isNativeElf(candidate + "/bin/c4-launcher") && isNativeElf(candidate + "/bin/c4-updater")
            && isNativeElf(candidate + "/bin/citroen-c4-robot")
            && QFileInfo(options_.root + "/native/current").canonicalFilePath() == firstNative_
            && QFileInfo(options_.root + "/native/previous").canonicalFilePath() == previousBeforeDownload_
            && config() == configBeforeDownload_,
            "real_signed_download_prepared_inactive_native_candidate_preserved_activation_history_settings_paths_assets",
            {{"real_updater_response", response}, {"candidate", candidate}})
            || !screenshot("11-second-release-downloaded")) return;
        const QString assets = activeAssets_;
        const QString settingsPath = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + "/citroen-c4-robot/runtime.conf";
        const QByteArray expectedSettings = nativeSettings_;
        const QJsonObject source = originalSource_;
        const QString release = options_.secondRelease, cachedRelease = options_.root + "/cached-install-release";
        collect([assets, settingsPath, expectedSettings, source, release, cachedRelease] {
            const QString error = validateAssets(assets);
            if (!error.isEmpty()) return QJsonObject{{"error", error}};
            QFile settings(settingsPath);
            if (!settings.open(QIODevice::ReadOnly) || settings.readAll() != expectedSettings)
                return QJsonObject{{"error", "Download changed saved native settings"}};
            const auto preserved = sourcePreservation(source);
            if (preserved.contains("error")) return preserved;
            if (!QDir().mkpath(cachedRelease)) return QJsonObject{{"error", "Cannot create manifest-only cached-install fixture"}};
            for (const QString &name : {QStringLiteral("manifest.json"), QStringLiteral("manifest.sig")})
                if (!QFile::copy(release + "/" + name, cachedRelease + "/" + name))
                    return QJsonObject{{"error", "Cannot copy real signed cached-install metadata: " + name}};
            if (QFileInfo::exists(cachedRelease + "/native-linux-x86_64.tar.gz"))
                return QJsonObject{{"error", "Cached-install fixture must have no source archive"}};
            return QJsonObject{{"cached_release", cachedRelease}, {"source_preserved", true},
                {"active_asset_hashes_verified", true}, {"native_settings_byte_identical", true}, {"source_archive_absent", true}};
        }, [this, cachedRelease](const QJsonObject &result) {
            if (!check(true, "download_preserved_real_source_active_asset_hashes_and_native_settings_cached_install_has_no_archive", result)) return;
            activate(cachedRelease, "Install signed update", State::SecondInstalled, "second_signed_release_cached_install_without_source_archive");
        });
        return;
    }
    case State::SecondInstalled:
        if (!idle()) return;
        if (!installed(secondVersion_, secondNative_, "second_signed_release_really_activated")) return;
        enter(State::RolledBack, "first_signed_release_rollback");
        click("Rollback native version");
        return;
    case State::RolledBack: {
        if (!idle()) return;
        QString actual;
        if (!installed(firstVersion_, actual, "rollback_really_reactivated_first_release")
            || !check(actual == firstNative_, "rollback_exact_first_version_directory")) return;
        activate(options_.secondRelease, "Install signed update", State::Reinstalled, "second_signed_release_reinstalled");
        return;
    }
    case State::Reinstalled: {
        if (!idle()) return;
        QString actual;
        if (!installed(secondVersion_, actual, "second_release_reinstall_really_activated")
            || !check(actual == secondNative_, "reinstall_exact_second_version_directory")) return;
        enter(State::RestartFinished, "restart_actual_activated_second_launcher");
        click("Restart activated launcher");
        return;
    }
    case State::RestartFinished:
        if (!idle()) return;
        restartEvidence();
        return;
    case State::Setup:
    case State::Evidence:
    case State::FailureCleanup:
        return;
    }
}

bool Scenario::checkedRelease(const QString &version, const QString &name) {
    auto *notes = widget<QPlainTextEdit>("Release notes");
    if (!show(notes)) return false;
    signedChecks_ = true;
    QJsonObject response;
    const auto lines = log().split('\n');
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const auto document = QJsonDocument::fromJson(it->toUtf8());
        if (document.isObject() && document.object().contains("update_available")) {
            response = document.object();
            break;
        }
    }
    return check(notes->toPlainText().startsWith("Release: " + version + "\n")
        && response.value("version").toString() == version
        && response.value("update_available").isBool() && response.value("update_available").toBool(), name,
        {{"version", version}, {"real_release_notes", notes->toPlainText()}, {"real_updater_response", response}});
}

void Scenario::rejectTamperedRelease() {
    previousNative_ = QFileInfo(options_.root + "/native/previous").canonicalFilePath();
    historyBeforeRejection_ = config().value("history").toArray();
    QString error;
    activeManifestHash_ = digest(activeAssets_ + "/asset-manifest.json", error);
    if (!error.isEmpty()) { fail(error); return; }
    const QString source = options_.secondRelease, destination = options_.root + "/rejected-release";
    phase_ = "prepare_tampered_copy_of_real_signed_release";
    collect([source, destination] {
        if (!QDir().mkpath(destination)) return QJsonObject{{"error", "Cannot create isolated rejected-release directory"}};
        for (const QString &name : {QStringLiteral("manifest.json"), QStringLiteral("manifest.sig"), QStringLiteral("native-linux-x86_64.tar.gz")}) {
            if (!QFile::copy(source + "/" + name, destination + "/" + name))
                return QJsonObject{{"error", "Cannot copy real signed release member: " + name}};
        }
        QFile package(destination + "/native-linux-x86_64.tar.gz");
        if (!package.open(QIODevice::ReadWrite) || package.size() == 0 || !package.seek(package.size() / 2))
            return QJsonObject{{"error", "Cannot open isolated copy of real package for negative hash test"}};
        const qint64 offset = package.pos();
        QByteArray byte = package.read(1);
        if (byte.size() != 1) return QJsonObject{{"error", "Cannot read tamper byte from isolated package copy"}};
        byte[0] = char(static_cast<unsigned char>(byte[0]) ^ 1U);
        if (!package.seek(offset) || package.write(byte) != 1 || !package.flush())
            return QJsonObject{{"error", "Cannot change tamper byte in isolated package copy"}};
        return QJsonObject{{"tampered_copy", destination}, {"offset", offset}};
    }, [this, destination](const QJsonObject &result) {
        if (!check(true, "negative_update_uses_copied_real_manifest_signature_and_one_changed_package_byte", result)) return;
        activate(destination, "Install signed update", State::RejectedInstalled, "tampered_real_signed_package_rejection");
    });
}

bool Scenario::launchAvailableOverride() {
    customExercised_ = true;
    const auto capabilities = window_.property("graphicsCapabilities").toJsonObject().value("capabilities").toObject();
    QString field;
    QJsonValue value;
    if (capabilities.value("fxaa").toBool()) {
        field = "post_aa"; value = "fxaa";
    } else {
        for (const auto *feature : {"ssao", "pbr", "internal_hdr", "bloom"}) {
            if (capabilities.value(feature).toBool()) { field = feature; value = true; break; }
        }
        if (field.isEmpty() && capabilities.value("sample_counts").toArray().contains(2)) {
            field = "samples"; value = 2;
        }
        if (field.isEmpty() && capabilities.value("max_shadow_cascades").toInt() > 0
            && capabilities.value("max_shadow_size").toInt() > 0) {
            field = "shadows"; value = "low";
        }
    }
    if (field.isEmpty()) {
        checks_.append(QJsonObject{{"name", "available_native_enhancement_custom_override"},
            {"exercised", false}, {"reason", "Actual OpenGL pipeline probe exposes no available enhancement"},
            {"details", capabilities}});
        return false;
    }
    if (!choice("quality_" + field, value.toVariant())) return true;
    requestedQuality_.insert(field, value);
    auto *effective = namedWidget<QWidget>("qualityEffectivePreset");
    if (!effective || !show(effective)
        || !check(effective->property("requestedPreset").toString() == "original"
            && effective->property("effectivePreset").toString() == "custom",
            "semantic_quality_override_effective_custom_with_original_requested_base",
            {{"field", field}, {"value", value}})) return true;
    if (!click("Save settings") || !operationOkay()
        || !savedGraphics("quality_override_saved_requested_original_base_preserved")
        || !screenshot("08-quality-custom-override")) return true;
    enter(State::NativeFinished, "actual_native_game_custom_quality_child");
    click("Launch native game");
    return true;
}

void Scenario::nativeEvidence() {
    const QString executable = window_.property("nativeExecutable").toString();
    auto *gameField = widget<QLineEdit>("Native game ELF");
    if (!gameField) return;
    if (!check(window_.property("nativeNormalExit").toBool() && window_.property("nativeExitCode").toInt() == 0
        && isNativeElf(executable)
        && QFileInfo(executable).canonicalFilePath() == QFileInfo(gameField->text().trimmed()).canonicalFilePath(),
        "actual_native_elf_child_exited_normally", {{"executable", executable}, {"exit_code", window_.property("nativeExitCode").toInt()}})) return;
    const QString diagnosticsPath = window_.property("nativeDiagnosticsPath").toString();
    const QString capturePath = window_.property("nativeCapturePath").toString();
    if (!check(QFileInfo(diagnosticsPath).absolutePath() == options_.root
        && QFileInfo(capturePath).absolutePath() == options_.root
        && !QFileInfo(diagnosticsPath).isSymLink() && !QFileInfo(capturePath).isSymLink(),
        "native_child_artifacts_owned_by_isolated_scenario",
        {{"diagnostics", diagnosticsPath}, {"capture", capturePath}})) return;
    QString error;
    const auto diagnostics = jsonFile(diagnosticsPath, error);
    if (!error.isEmpty()) { fail(error); return; }
    const auto window = diagnostics.value("window").toObject();
    const auto display = diagnostics.value("display").toObject();
    const auto sources = diagnostics.value("sources").toObject();
    const auto scene = diagnostics.value("scene").toObject();
    const auto opengl = diagnostics.value("opengl").toObject();
    const int expectedFrames = readinessStarted_ ? 150 : 3;
    if (!check(diagnostics.value("schema_version").toInt() == 1
        && diagnostics.value("runtime_kind").toString() == "native-game"
        && diagnostics.value("backend").toString() == "opengl"
        && window.value("logical_width").toInt() == 640 && window.value("logical_height").toInt() == 480
        && window.value("drawable_width").toInt() > 0 && window.value("drawable_height").toInt() > 0
        && window.value("fullscreen").isBool() && !window.value("fullscreen").toBool()
        && display.value("requested_vsync").isBool() && !display.value("requested_vsync").toBool()
        && display.value("frame_limit").toInt() == 75 && diagnostics.value("rendered_frames").toInt() == expectedFrames
        && window_.property("nativeFrames").toInt() == expectedFrames,
        readinessStarted_ ? "native_readiness_child_observed_window_and_150_presented_frames"
            : "native_startup_child_observed_window_and_three_presented_frames",
        {{"window", window}, {"display", display}, {"rendered_frames", diagnostics.value("rendered_frames")},
         {"expected_frames", expectedFrames}})) return;
    const auto game = diagnostics.value("game").toObject();
    const QString sourcePreset = requestedQuality_.value("preset").toString();
    const int expectedCgm = sourcePreset == "enhanced" || sourcePreset == "ultra" ? 3 : 0;
    const QJsonObject expectedSourceCompile{{"ATQ", 0}, {"MTQ", 0}, {"STQ", 0}, {"CGM", expectedCgm},
        {"SHQ", 0}, {"ANO", 0}, {"Bloom", 0}};
    if (!check(game.value("source_compile_options").isObject()
        && game.value("source_compile_options").toObject() == expectedSourceCompile,
        "actual_source_compile_options_match_requested_native_preset_default",
        {{"source_compile_options", game.value("source_compile_options")},
         {"requested_preset", sourcePreset}, {"expected", expectedSourceCompile}})) return;
    const auto draws = diagnostics.value("draw_calls").toObject();
    const QString sourceRoot = QFileInfo(activeAssets_).canonicalFilePath();
    const QString originalUiRoot = game.value("original_ui_root").toString();
    if (!check(sources.value("game_dir").toString() == sourceRoot
        && game.value("game_mode").toString() == "native-source-port" && game.value("phase").toString() == "menu"
        && originalUiRoot == sourceRoot + "/app" && QFileInfo(originalUiRoot).isDir()
        && !QFileInfo(originalUiRoot).isSymLink()
        && game.value("ui_images").isDouble() && game.value("ui_images").toInt(-1) >= 0
        && game.value("ui_quads").isDouble() && game.value("ui_quads").toInt(-1) >= 0
        && draws.value("ui").isDouble() && draws.value("ui").toInt(-1) >= 0
        && (!readinessStarted_ || (game.value("ui_images").toInt() > 0 && game.value("ui_quads").toInt() > 0
            && draws.value("ui").toInt() > 0))
        && draws.value("world").toInt(-1) == 0
        && scene.value("world_instances").toInt(-1) == 0 && scene.value("draw_calls").toInt(-1) == 0
        && !opengl.value("vendor").toString().isEmpty() && !opengl.value("renderer").toString().isEmpty()
        && !opengl.value("version").toString().isEmpty(),
        readinessStarted_ ? "native_ready_original_menu_rendered_source_ui_without_inspection_world"
            : "native_startup_original_source_and_observed_menu_draw_counts",
        {{"sources", sources}, {"game", game}, {"draw_calls", draws}, {"scene", scene}, {"opengl", opengl}})) return;
    const auto graphics = diagnostics.value("graphics").toObject();
    const auto probedGraphics = window_.property("graphicsCapabilities").toJsonObject();
    const auto probedCapabilities = probedGraphics.value("capabilities").toObject();
    const auto renderedCapabilities = graphics.value("capabilities").toObject();
    bool pipelinesMatch = probedGraphics.value("device") == opengl.value("renderer");
    for (const char *field : {"pbr", "max_shadow_cascades", "max_shadow_size", "trilinear", "max_anisotropy"})
        pipelinesMatch = pipelinesMatch && probedCapabilities.contains(field)
            && probedCapabilities.value(field) == renderedCapabilities.value(field);
    if (!check(pipelinesMatch,
        "native_capability_probe_matches_actual_inactive_menu_pipelines",
        {{"probe", probedGraphics}, {"graphics", graphics}, {"opengl", opengl}})) return;
    const auto resolved = graphics.value("resolved").toObject();
    QJsonObject expectedResolved{{"requested_preset", "original"},
        {"effective_preset", requestedQuality_.size() > 1 ? "custom" : "original"},
        {"samples", 1}, {"post_aa", "off"}, {"ssao", false}, {"shadows", "off"},
        {"pbr", false}, {"internal_hdr", false}, {"bloom", false}, {"exposure_ev", 0},
        {"tone_mapping", "reinhard"}, {"fallbacks", 0}};
    for (auto it = requestedQuality_.begin(); it != requestedQuality_.end(); ++it)
        if (it.key() != "preset") expectedResolved.insert(it.key(), it.value());
    bool resolvedMatches = graphics.value("capabilities").isObject();
    for (auto it = expectedResolved.begin(); it != expectedResolved.end(); ++it)
        resolvedMatches = resolvedMatches && resolved.value(it.key()) == it.value();
    if (!check(resolvedMatches,
        requestedQuality_.size() > 1 ? "available_native_enhancement_applied_as_custom_with_original_base"
            : "native_original_preset_has_no_requested_enhancements",
        {{"requested", requestedQuality_}, {"graphics", graphics}, {"expected_resolved", expectedResolved}})) return;
    const QImage capture(capturePath);
    bool varied = false;
    if (!capture.isNull()) {
        const QRgb first = capture.pixel(0, 0);
        for (int y = 0; y < capture.height() && !varied; ++y)
            for (int x = 0; x < capture.width(); ++x)
                if (capture.pixel(x, y) != first) { varied = true; break; }
    }
    if (!check(!capture.isNull() && capture.width() == window.value("drawable_width").toInt()
        && capture.height() == window.value("drawable_height").toInt() && (!readinessStarted_ || varied)
        && QFileInfo(capturePath).size() > 0,
        readinessStarted_ ? "native_ready_actual_framebuffer_capture_nonuniform"
            : "native_startup_actual_framebuffer_capture_uniformity_observed",
        {{"path", capturePath}, {"width", capture.width()}, {"height", capture.height()},
         {"nonuniform", varied}, {"source_startup_uniform", !readinessStarted_ && !varied}})) return;
    if (readinessStarted_) readinessObserved_ = true;
    const QString framebufferEvidence = options_.root + (!readinessStarted_ ? "/native-startup.png"
        : requestedQuality_.size() > 1 ? "/native-custom.png" : "/native-original-ready.png");
    if (!capture.save(framebufferEvidence, "PNG")) { fail("Cannot preserve actual framebuffer: " + framebufferEvidence); return; }
    screenshots_.append(framebufferEvidence);
    const QString settingsPath = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + "/citroen-c4-robot/runtime.conf";
    QFile settings(settingsPath);
    if (!settings.open(QIODevice::ReadOnly)) { fail("Cannot read real native runtime settings: " + settingsPath); return; }
    nativeSettings_ = settings.readAll();
    std::istringstream stream(nativeSettings_.toStdString());
    std::string key, text;
    QJsonObject persisted;
    bool valid = true;
    while (stream >> key) {
        if (persisted.contains(QString::fromStdString(key))) { valid = false; break; }
        if (key == "game_dir" || key == "renderer") {
            stream >> std::ws;
            if (stream.peek() != '"' || !(stream >> std::quoted(text))) { valid = false; break; }
            const auto next = stream.peek();
            if (next != std::char_traits<char>::eof() && !std::isspace(static_cast<unsigned char>(next))) {
                valid = false; break;
            }
            persisted[QString::fromStdString(key)] = QString::fromStdString(text);
        } else if (key == "quality" || key == "post_aa" || key == "shadows" || key == "tone_map"
            || key == "texture_filter" || key == "denoising") {
            if (!(stream >> text)) { valid = false; break; }
            const bool accepted = key == "quality"
                ? (text == "original" || text == "enhanced" || text == "ultra" || text == "custom")
                : key == "post_aa" ? (text == "off" || text == "fxaa")
                : key == "shadows" ? (text == "off" || text == "low" || text == "high")
                : key == "tone_map" ? (text == "reinhard" || text == "aces")
                : key == "texture_filter" ? (text == "original" || text == "trilinear" || text == "anisotropic")
                : (text == "off" || text == "standard" || text == "high");
            if (!accepted) { valid = false; break; }
            persisted[QString::fromStdString(key)] = QString::fromStdString(text);
        } else if (key == "exposure_ev") {
            double value = 0;
            if (!(stream >> value) || !std::isfinite(value) || value < -10 || value > 10) { valid = false; break; }
            persisted[QString::fromStdString(key)] = value;
        } else if (key == "fullscreen" || key == "vsync" || key == "ssao" || key == "pbr"
            || key == "internal_hdr" || key == "bloom" || key == "backend_override") {
            if (!(stream >> text) || (text != "on" && text != "off")) { valid = false; break; }
            persisted[QString::fromStdString(key)] = text == "on";
        } else if (key == "source_atq" || key == "source_mtq" || key == "source_stq"
            || key == "source_cgm" || key == "source_shq" || key == "source_ano" || key == "source_bloom") {
            int value = 0;
            const int maximum = key == "source_stq" || key == "source_cgm" ? 3
                : key == "source_ano" || key == "source_bloom" ? 1 : 2;
            if (!(stream >> value) || value < 0 || value > maximum) { valid = false; break; }
            persisted[QString::fromStdString(key)] = value;
        } else if (key == "version" || key == "width" || key == "height" || key == "frame_limit"
            || key == "rt_effects" || key == "msaa") {
            int value = 0;
            if (!(stream >> value) || (key == "rt_effects" && (value < 0 || value > 7))
                || (key == "msaa" && value != 1 && value != 2 && value != 4 && value != 8)) { valid = false; break; }
            persisted[QString::fromStdString(key)] = value;
        } else { valid = false; break; }
    }
    QJsonObject expectedSettings{{"version", 1}, {"game_dir", sourceRoot}, {"renderer", "opengl"},
        {"width", 640}, {"height", 480}, {"fullscreen", false}, {"vsync", false},
        {"frame_limit", 75}, {"rt_effects", 0}, {"quality", "original"}};
    for (auto it = requestedQuality_.begin(); it != requestedQuality_.end(); ++it) {
        if (it.key() == "preset") continue;
        const QString nativeKey = it.key() == "samples" ? "msaa"
            : it.key() == "tone_mapping" ? "tone_map"
            : it.key() == "texture_filtering" ? "texture_filter" : it.key();
        expectedSettings.insert(nativeKey, it.value());
    }
    if (!check(valid && stream.eof() && persisted == expectedSettings,
        "native_runtime_conf_persisted_exact_requested_original_source_graphics_and_optional_overrides",
        {{"path", settingsPath}, {"settings", persisted}, {"expected", expectedSettings}})
        || !screenshot(!readinessStarted_ ? "08-native-startup-child-finished"
            : requestedQuality_.size() > 1 ? "08-native-custom-child-finished" : "08-native-original-ready-child-finished")) return;
    if (!readinessStarted_) {
        readinessStarted_ = true;
        window_.setProperty("integrationFrameCount", 150);
        enter(State::NativeFinished, "actual_native_original_menu_readiness_child");
        click("Launch native game");
        return;
    }
    if (!customExercised_) {
        enter(State::QualityProbeFinished, "reprobe_actual_graphics_without_native_settings_changes");
        click("Probe native graphics pipelines");
        return;
    }
    continueAfterNative();
}

void Scenario::continueAfterNative() {
    if (!updates_) {
        checks_.append(QJsonObject{{"name", "signed_release_install_rollback_reinstall_and_versioned_launcher_restart"},
            {"exercised", false}, {"reason", "No first-release, second-release and public-key inputs supplied; full integration scenario was not exercised"}});
        finalIntegrity();
        return;
    }
    activate(options_.firstRelease, "Check signed release", State::FirstChecked, "first_signed_release_checked");
}

void Scenario::restartEvidence() {
    if (!check(window_.property("restartNormalExit").toBool() && window_.property("restartExitCode").toInt() == 0,
        "actual_activated_launcher_child_exited_normally", {{"exit_code", window_.property("restartExitCode").toInt()}})) return;
    QString error;
    const auto restarted = jsonFile(options_.root + "/restarted.json", error);
    if (!error.isEmpty()) { fail(error); return; }
    const QString actual = restarted.value("executable").toString();
    const QString expected = QFileInfo(secondNative_ + "/bin/c4-launcher").canonicalFilePath();
    if (!check(!expected.isEmpty() && actual == expected && isNativeElf(actual)
        && QFileInfo(actual).canonicalFilePath() == actual,
        "restart_executed_real_second_version_launcher", {{"actual", actual}, {"expected", expected}})) return;
    const auto saved = config();
    const auto childConfig = restarted.value("config").toObject();
    auto persistedChild = childConfig;
    persistedChild.remove("executable"); persistedChild.remove("updater");
    const auto history = childConfig.value("history").toArray();
    const QStringList actions{"--install", "--install", "--rollback", "--install"};
    const QStringList versions{firstVersion_, secondVersion_, firstVersion_, secondVersion_};
    bool validHistory = history.size() == actions.size();
    for (qsizetype i = 0; validHistory && i < history.size(); ++i) {
        const auto entry = history.at(i).toObject();
        validHistory = entry.value("action").toString() == actions.at(i)
            && entry.value("version").toString() == versions.at(i) && !entry.value("time").toString().isEmpty();
    }
    if (!check(persistedChild == saved && !saved.contains("executable") && !saved.contains("updater")
        && graphicsMatch(childConfig) && qualityMatch(childConfig)
        && childConfig.value("assets").toString() == activeAssets_ && validHistory && sourceMatch(childConfig),
        "versioned_launcher_restart_preserved_full_graphics_assets_and_real_history",
        {{"config", childConfig}, {"history", history}})) return;
    const QString expectedGame = QFileInfo(secondNative_ + "/bin/citroen-c4-robot").canonicalFilePath();
    const auto native = restarted.value("native").toObject();
    const QString diagnosticsPath = options_.root + "/restarted-native/native.json";
    const QString capturePath = options_.root + "/restarted-native/native.ppm";
    if (!check(!expectedGame.isEmpty() && isNativeElf(expectedGame)
        && restarted.value("loaded_config").toObject() == childConfig
        && childConfig.value("executable").toString() == expectedGame
        && childConfig.value("updater").toString() == QFileInfo(secondNative_ + "/bin/c4-updater").canonicalFilePath()
        && restarted.value("game_field").toString() == expectedGame
        && restarted.value("selected_game").toString() == expectedGame
        && native.value("executable").toString() == expectedGame
        && native.value("normal_exit").isBool() && native.value("normal_exit").toBool()
        && native.value("exit_code").toInt(-1) == 0
        && native.value("frames").toInt() == 150
        && native.value("diagnostics").toString() == diagnosticsPath
        && native.value("capture").toString() == capturePath,
        "reopened_saved_widgets_launch_actual_activated_game_elf", {{"restart", restarted}})) return;
    const auto diagnostics = jsonFile(diagnosticsPath, error);
    if (!error.isEmpty()) { fail(error); return; }
    const auto game = diagnostics.value("game").toObject();
    const auto childWindow = diagnostics.value("window").toObject();
    const auto childDisplay = diagnostics.value("display").toObject();
    const auto childDraws = diagnostics.value("draw_calls").toObject();
    const QImage capture(capturePath);
    bool varied = false;
    if (!capture.isNull()) {
        const QRgb first = capture.pixel(0, 0);
        for (int y = 0; y < capture.height() && !varied; ++y)
            for (int x = 0; x < capture.width(); ++x)
                if (capture.pixel(x, y) != first) { varied = true; break; }
    }
    const QString sourcePreset = saved.value("graphics").toObject().value("preset").toString();
    const int expectedCgm = sourcePreset == "enhanced" || sourcePreset == "ultra" ? 3 : 0;
    const QJsonObject expectedSourceCompile{{"ATQ", 0}, {"MTQ", 0}, {"STQ", 0}, {"CGM", expectedCgm},
        {"SHQ", 0}, {"ANO", 0}, {"Bloom", 0}};
    if (!check(diagnostics.value("schema_version").toInt() == 1
        && diagnostics.value("runtime_kind").toString() == "native-game"
        && diagnostics.value("backend").toString() == saved.value("renderer").toString()
        && diagnostics.value("rendered_frames").toInt() == 150
        && diagnostics.value("sources").toObject().value("game_dir").toString() == QFileInfo(activeAssets_).canonicalFilePath()
        && childWindow.value("logical_width").toInt() == saved.value("width").toInt()
        && childWindow.value("logical_height").toInt() == saved.value("height").toInt()
        && childWindow.value("fullscreen").isBool()
        && childWindow.value("fullscreen").toBool() == saved.value("fullscreen").toBool()
        && childDisplay.value("requested_vsync").isBool()
        && childDisplay.value("requested_vsync").toBool() == saved.value("vsync").toBool()
        && childDisplay.value("frame_limit").toInt() == saved.value("frame_limit").toInt()
        && game.value("game_mode").toString() == "native-source-port"
        && game.value("phase").toString() == "menu"
        && game.value("source_compile_options").toObject() == expectedSourceCompile
        && game.value("original_ui_root").toString() == QFileInfo(activeAssets_).canonicalFilePath() + "/app"
        && game.value("ui_images").toInt() > 0 && game.value("ui_quads").toInt() > 0
        && childDraws.value("ui").toInt() > 0 && childDraws.value("world").toInt(-1) == 0
        && !capture.isNull() && varied
        && capture.width() == childWindow.value("drawable_width").toInt()
        && capture.height() == childWindow.value("drawable_height").toInt(),
        "reopened_saved_launcher_native_child_actual_finite_source_ui_readiness",
        {{"diagnostics", diagnostics}, {"capture", capturePath}, {"nonuniform", varied}})) return;
    const QImage childScreenshot(options_.root + "/restarted.png");
    if (!check(!childScreenshot.isNull() && childScreenshot.width() > 0 && childScreenshot.height() > 0,
        "restarted_real_shown_launcher_widget_screenshot", {{"path", options_.root + "/restarted.png"}})) return;
    screenshots_.append(options_.root + "/restarted.png");
    if (!screenshot("15-second-launcher-restarted")) return;
    finalIntegrity();
}

void Scenario::finalIntegrity() {
    const auto source = originalSource_;
    const QString oldAssets = firstAssets_, assets = activeAssets_, expectedOld = firstManifestHash_;
    const QString settingsPath = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + "/citroen-c4-robot/runtime.conf";
    const QByteArray expectedSettings = nativeSettings_;
    phase_ = options_.directory.isEmpty() ? "final_original_installer_assets_and_native_settings_preservation"
        : "final_entire_original_directory_catalog_assets_and_native_settings_preservation";
    collect([source, oldAssets, assets, expectedOld, settingsPath, expectedSettings] {
        const auto preserved = sourcePreservation(source);
        if (preserved.contains("error")) return preserved;
        const auto old = assetEvidence(oldAssets, source);
        if (old.contains("error")) return old;
        if (old.value("manifest_sha256").toString() != expectedOld)
            return QJsonObject{{"error", "Previously activated original asset manifest changed"}};
        const auto current = assetEvidence(assets, source);
        if (current.contains("error")) return current;
        QFile settings(settingsPath);
        if (!settings.open(QIODevice::ReadOnly) || settings.readAll() != expectedSettings)
            return QJsonObject{{"error", "Actual native settings changed during update/restart sequence"}};
        QJsonObject result{{"source_kind", source.value("kind")}, {"old_manifest_sha256", expectedOld},
            {"active_manifest_sha256", current.value("manifest_sha256")}, {"files", current.value("files")}};
        if (source.value("kind").toString() == "installer")
            result.insert("installer_sha256", source.value("installer_sha256"));
        else {
            result.insert("source_directory", source.value("path"));
            result.insert("original_source_inventory", preserved);
        }
        return result;
    }, [this](const QJsonObject &result) {
        if (!check(true, options_.directory.isEmpty()
            ? "original_installer_previous_and_active_complete_asset_trees_native_settings_preserved"
            : "entire_original_directory_bytes_previous_and_active_catalog_trees_native_settings_preserved", result)
            || !savedGraphics("final_launcher_graphics_preserved")
            || !check(config().value("assets").toString() == activeAssets_, "final_launcher_active_assets_preserved")) return;
        finish(true);
    });
}

} // namespace

void startIntegration(QMainWindow &window, const IntegrationOptions &options) {
    new Scenario(window, options);
}
