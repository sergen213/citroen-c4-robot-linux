#include <QApplication>
#include "integration.hpp"
#include "quality_controls.hpp"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QPixmap>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariant>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <cstdio>
#include <functional>
#include <memory>

QJsonObject probeHardware();
QString validateAssets(const QString &root);
bool isNativeElf(const QString &path);

namespace {
QString configPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/launcher.json";
}
QString saveJson(const QString &path, const QJsonObject &object) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return "Cannot create configuration directory";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    const QByteArray bytes = QJsonDocument(object).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit()) return file.errorString();
    return {};
}
QString importerPath() {
    const QString bin = QCoreApplication::applicationDirPath();
    QStringList paths{bin + "/../share/citroen-c4-robot/tools/import_assets.py",
                      bin + "/tools/import_assets.py"};
    const QString developerImporter = qEnvironmentVariable("C4_IMPORTER_PATH");
    if (QDir::isAbsolutePath(developerImporter)) paths.append(developerImporter);
    for (const auto &path : paths)
        if (QFileInfo(path).isFile() && !QFileInfo(path).isSymLink()) return QFileInfo(path).canonicalFilePath();
    return {};
}
QString deployedTool(const QString &relative, const QString &systemName) {
    const QString root = QFileInfo(QCoreApplication::applicationDirPath() + "/..").canonicalFilePath();
    const QString packaged = root + "/" + relative;
    return QFileInfo(packaged).isExecutable() ? packaged : QStandardPaths::findExecutable(systemName);
}
QProcessEnvironment nativeEnvironment(const QString &executable) {
    auto environment = QProcessEnvironment::systemEnvironment();
    const QString root = QFileInfo(QFileInfo(executable).absolutePath() + "/..").canonicalFilePath();
    // A relocated authenticated closure must not load plugins/libraries from the old AppImage mount.
    if (QFileInfo(root + "/plugins").isDir()) {
        for (const auto *name : {"APPDIR", "APPIMAGE", "OWD", "QTDIR", "QML2_IMPORT_PATH",
                                 "PYTHONHOME", "PYTHONPATH", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH",
                                 "QT_QPA_PLATFORM_PLUGIN_PATH"})
            environment.remove(QString::fromLatin1(name));
        environment.insert("QT_PLUGIN_PATH", root + "/plugins");
    }
    return environment;
}
QString desktopArgument(QString value) {
    value.replace('\\', "\\\\\\\\");
    value.replace('"', "\\\\\"");
    value.replace('`', "\\\\`");
    value.replace('$', "\\\\$");
    value.replace('%', "%%");
    return '"' + value + '"';
}
QString nativeBinary(const QString &name) {
    const QString bin = QCoreApplication::applicationDirPath();
    const QString deployed = bin + "/" + name;
    if (isNativeElf(deployed)) return QFileInfo(deployed).canonicalFilePath();
    const QFileInfo cache(bin + "/../CMakeCache.txt");
    if (QFileInfo(bin).fileName() == "launcher" && cache.isFile() && !cache.isSymLink()) {
        const QString build = bin + "/../" + name;
        if (isNativeElf(build)) return QFileInfo(build).canonicalFilePath();
        if (name == "c4-updater") {
            const QString updater = bin + "/../updater/" + name;
            if (isNativeElf(updater)) return QFileInfo(updater).canonicalFilePath();
        }
    }
    return deployed;
}
bool obsoleteAppImageBinary(const QString &path, const QString &name) {
    const auto parts = path.split('/');
    const QFileInfo file(path);
    return parts.size() == 6 && parts[0].isEmpty() && parts[1] == "tmp"
        && parts[2].startsWith(".mount_") && parts[2].size() > 7
        && parts[3] == "usr" && parts[4] == "bin" && parts[5] == name
        && !file.exists() && !file.isSymLink();
}
QString writeConfig(const QJsonObject &config) {
    QJsonObject saved = config;
    const QString game = nativeBinary("citroen-c4-robot"), updater = nativeBinary("c4-updater");
    // Deployment-relative defaults are rediscovered; only explicit custom paths belong on disk.
    if (saved.value("executable").toString() == game && isNativeElf(game)) saved.remove("executable");
    if (saved.value("updater").toString() == updater && isNativeElf(updater)) saved.remove("updater");
    return saveJson(configPath(), saved);
}
QJsonObject defaults() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/citroen-c4-robot";
    return {{"managed_root", data}, {"assets", ""}, {"installer", ""},
            {"source_kind", "installer"}, {"source_directory", ""},
            {"executable", nativeBinary("citroen-c4-robot")}, {"updater", nativeBinary("c4-updater")},
            {"prefer_managed", true}, {"width", 1280}, {"height", 720}, {"fullscreen", false},
            {"vsync", true}, {"frame_limit", 60}, {"language", "en"}, {"repository", ""},
            {"trusted_key", ""}, {"install_root", data + "/native"}, {"channel", "stable"},
            {"startup_check", false}, {"background_check", false}, {"notify_updates", true},
            {"background_downloads", false}, {"auto_install", false}, {"shortcut", false}, {"history", QJsonArray{}}};
}
}

class Launcher final : public QMainWindow {
    QJsonObject config_ = defaults();
    QString loadError_;
    bool turkish_ = false;
    bool busy_ = false;
    bool cancelled_ = false;
    bool quietUpdate_ = false;
    bool backgroundDownload_ = false, oversizedUpdateLine_ = false;
    qint64 downloadReceived_ = -1, downloadTotal_ = -1;
    QString downloadCurrent_, downloadPrevious_;
    QString stagedAssets_;
    QString updateAction_;
    QByteArray updateOutput_, updateErrorLine_, graphicsOutput_;
    QString graphicsExecutable_, graphicsSelected_, graphicsRenderer_, graphicsError_;
    QJsonObject graphicsBackends_;
    unsigned graphicsGeneration_ = 0;
    unsigned nativeLaunchCount_ = 0;
    QTabWidget *tabs_{};
    QLabel *assetStatus_{}, *versionStatus_{}, *artwork_{};
    QPlainTextEdit *log_{}, *hardware_{}, *notes_{}, *history_{};
    QProgressBar *progress_{};
    QProcess importProcess_, updateProcess_, nativeProcess_, restartProcess_, graphicsProcess_;
    QFutureWatcher<QString> validation_;
    QTimer backgroundTimer_, graphicsTimeout_;
    std::unique_ptr<QualityControls> qualityControls_;
    QLineEdit *installer_{}, *originalDirectory_{}, *managedRoot_{}, *game_{}, *updater_{}, *repository_{}, *key_{}, *updateRoot_{}, *offlineRelease_{};
    QComboBox *sourceKind_{}, *language_{}, *channel_{};
    QSpinBox *width_{}, *height_{}, *limit_{};
    QCheckBox *fullscreen_{}, *vsync_{}, *shortcut_{}, *preferManaged_{}, *startup_{}, *background_{}, *downloads_{}, *notify_{}, *automatic_{};
    QPushButton *launch_{}, *import_{}, *repair_{}, *check_{}, *downloadUpdate_{}, *installUpdate_{}, *rollback_{}, *restart_{}, *save_{}, *cancel_{}, *probeGraphics_{};

    QString t(const char *english, const char *turkish) const {
        return QString::fromUtf8(turkish_ ? turkish : english);
    }
    void append(const QString &text) { log_->appendPlainText(text.trimmed()); }
    void failure(const QString &text, bool silent = false) {
        append(text);
        setProperty("operationError", text);
        if (!silent && property("integrationRoot").toString().isEmpty())
            QMessageBox::warning(this, t("Operation failed", "İşlem başarısız"), text);
    }
    QLabel *label(const QString &text, QWidget *parent = nullptr) {
        auto *result = new QLabel(text, parent);
        result->setWordWrap(true);
        result->setTextFormat(Qt::PlainText);
        result->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return result;
    }
    QPushButton *button(const QString &text, QLayout *layout, const std::function<void()> &action) {
        auto *result = new QPushButton(text);
        result->setAccessibleName(text);
        layout->addWidget(result);
        connect(result, &QPushButton::clicked, this, action);
        return result;
    }
    QWidget *page(const QString &title, QVBoxLayout *&layout) {
        auto *widget = new QWidget;
        layout = new QVBoxLayout(widget);
        layout->setContentsMargins(24, 22, 24, 22);
        layout->setSpacing(14);
        tabs_->addTab(widget, title);
        return widget;
    }
    QLineEdit *pathField(QFormLayout *form, const QString &title, const QString &initial,
                         bool directory = false, const QString &filter = {}) {
        auto *row = new QWidget;
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QLineEdit(initial);
        edit->setAccessibleName(title);
        layout->addWidget(edit);
        button(t("Browse…", "Gözat…"), layout, [this, edit, title, directory, filter] {
            const QString chosen = directory ? QFileDialog::getExistingDirectory(this, title, edit->text())
                : QFileDialog::getOpenFileName(this, title, edit->text(), filter);
            if (!chosen.isEmpty()) edit->setText(chosen);
        });
        form->addRow(title, row);
        return edit;
    }
    QCheckBox *checkbox(QFormLayout *form, const QString &title, const char *key) {
        auto *box = new QCheckBox(title);
        box->setAccessibleName(title);
        box->setChecked(config_.value(key).toBool());
        form->addRow(box);
        return box;
    }
    QSpinBox *number(QFormLayout *form, const QString &title, const char *key, int min, int max) {
        auto *box = new QSpinBox;
        box->setRange(min, max);
        box->setValue(config_.value(key).toInt());
        box->setAccessibleName(title);
        form->addRow(title, box);
        return box;
    }
    void readConfig() {
        QFile file(configPath());
        if (!file.exists()) return;
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
            loadError_ = "Configuration unreadable or exceeds 1 MiB; not overwritten automatically.";
            return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            loadError_ = "Invalid configuration JSON; not overwritten automatically.";
            return;
        }
        auto saved = document.object();
        // Only missing historical AppImage bundle paths migrate, and only to validated current defaults.
        if (obsoleteAppImageBinary(saved.value("executable").toString(), "citroen-c4-robot") &&
            isNativeElf(config_.value("executable").toString())) saved.remove("executable");
        if (obsoleteAppImageBinary(saved.value("updater").toString(), "c4-updater") &&
            isNativeElf(config_.value("updater").toString())) saved.remove("updater");
        for (auto it = saved.begin(); it != saved.end(); ++it) config_[it.key()] = it.value();
    }
    bool saveSettings(bool report = true) {
        QJsonObject next = config_;
        next["installer"] = installer_->text().trimmed();
        next["source_kind"] = sourceKind_->currentData().toString();
        next["source_directory"] = originalDirectory_->text().trimmed();
        next["managed_root"] = QDir::cleanPath(managedRoot_->text().trimmed());
        next["executable"] = game_->text().trimmed();
        next["updater"] = updater_->text().trimmed();
        next["repository"] = repository_->text().trimmed();
        next["trusted_key"] = key_->text().trimmed();
        next["install_root"] = QDir::cleanPath(updateRoot_->text().trimmed());
        next["offline_release"] = offlineRelease_->text().trimmed();
        next["channel"] = channel_->currentData().toString();
        next["language"] = language_->currentData().toString();
        next["width"] = width_->value(); next["height"] = height_->value();
        next["frame_limit"] = limit_->value(); next["fullscreen"] = fullscreen_->isChecked();
        next["vsync"] = vsync_->isChecked(); next["shortcut"] = shortcut_->isChecked();
        next["prefer_managed"] = preferManaged_->isChecked();
        next["startup_check"] = startup_->isChecked(); next["background_check"] = background_->isChecked();
        next["background_downloads"] = downloads_->isChecked();
        next["notify_updates"] = notify_->isChecked(); next["auto_install"] = automatic_->isChecked();
        const auto graphics = qualityControls_->save();
        for (auto it = graphics.begin(); it != graphics.end(); ++it) next[it.key()] = it.value();
        if (!QDir::isAbsolutePath(next["managed_root"].toString()) ||
            !QDir::isAbsolutePath(next["install_root"].toString())) {
            if (report) failure(t("Managed asset and native update roots must be absolute paths.",
                                  "Varlık ve yerel güncelleme kökleri mutlak yollar olmalıdır."));
            return false;
        }
        const QString error = writeConfig(next);
        if (!error.isEmpty()) { if (report) failure(error); return false; }
        config_ = next;
        backgroundTimer_.setInterval(60 * 60 * 1000);
        if (background_->isChecked()) backgroundTimer_.start(); else backgroundTimer_.stop();
        refreshStatus();
        return true;
    }
    void setBusy(bool busy) {
        busy = busy || backgroundDownload_;
        if (busy && !busy_) { cancelled_ = false; setProperty("operationError", QString()); }
        busy_ = busy;
        setProperty("busy", busy);
        setProperty("backgroundDownload", backgroundDownload_);
        for (auto *control : {import_, repair_, check_, downloadUpdate_, installUpdate_, rollback_, restart_, save_, probeGraphics_}) control->setEnabled(!busy);
        launch_->setEnabled(!busy || (backgroundDownload_ && !property("nativeRunning").toBool()));
        tabs_->setEnabled(!busy || backgroundDownload_);
        // Keep sections navigable, but freeze inputs used by the in-flight updater and Home Play.
        for (int index : {1, 2, 4}) tabs_->widget(index)->setEnabled(!busy);
        cancel_->setEnabled(busy);
        if (!backgroundDownload_) {
            progress_->setRange(0, busy ? 0 : 1);
            progress_->setValue(busy ? 0 : 1);
            progress_->setTextVisible(false);
            progress_->setAccessibleName(t("Operation busy indicator", "İşlem meşgul göstergesi"));
            progress_->setAccessibleDescription({});
        }
    }
    void refreshStatus() {
        const QString assets = config_.value("assets").toString();
        assetStatus_->setText(assets.isEmpty() ? t("No validated assets activated.", "Doğrulanmış varlık etkin değil.")
            : t("Active asset directory: ", "Etkin varlık dizini: ") + assets);
        const QString root = config_.value("install_root").toString();
        const QString current = QFileInfo(root + "/current").canonicalFilePath();
        versionStatus_->setText(t("Active native version: ", "Etkin yerel sürüm: ") +
            (current.isEmpty() ? t("local build / no managed release", "yerel derleme / yönetilen sürüm yok") : QFileInfo(current).fileName()));
        QStringList lines;
        for (const auto &entry : config_.value("history").toArray()) {
            const auto object = entry.toObject();
            lines << object.value("time").toString() + "  " + object.value("action").toString() + "  " + object.value("version").toString();
        }
        const QStringList versions = QDir(root + "/versions").entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        lines << t("Local version directories (not a signature audit):", "Yerel sürüm dizinleri (imza denetimi değildir):") << versions;
        history_->setPlainText(lines.join('\n'));
    }
    void record(const QString &action, const QString &version) {
        QJsonArray history = config_.value("history").toArray();
        history.append(QJsonObject{{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"action", action}, {"version", version}});
        while (history.size() > 100) history.removeFirst();
        config_["history"] = history;
        const QString error = writeConfig(config_);
        if (!error.isEmpty()) failure(error);
        refreshStatus();
    }
    void showValidatedArtwork(const QString &root) {
        const QPixmap image(root + QString::fromUtf8("/app/C4 Robot Yardım_files/c4_robot_title.png"));
        if (image.isNull()) { artwork_->clear(); artwork_->hide(); return; }
        artwork_->setPixmap(image.scaled(640, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        artwork_->setToolTip(t("Original artwork loaded locally from your validated import; never bundled.",
                               "Özgün görsel doğrulanmış yerel aktarmanızdan yüklendi; paketlenmez."));
        artwork_->show();
    }
    void makeShortcut() {
        // Restrict shortcut creation to this user's own application menu directory.
        const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        const QString dir = data + "/applications";
        const QString executable = QCoreApplication::applicationFilePath();
        if (executable.contains('\n') || executable.contains('\r') || !QDir().mkpath(dir)) {
            failure(t("Cannot create user desktop shortcut.", "Kullanıcı masaüstü kısayolu oluşturulamıyor.")); return;
        }
        const QString dataRoot = QFileInfo(data).canonicalFilePath();
        const QString destination = dir + "/citroen-c4-robot-native.desktop";
        if (dataRoot.isEmpty() || !QFileInfo(dir).canonicalFilePath().startsWith(dataRoot + "/") ||
            QFileInfo(destination).isSymLink()) {
            failure(t("Shortcut destination must stay inside your real XDG data directory and not be a symlink.",
                      "Kısayol hedefi gerçek XDG veri dizininizde olmalı ve sembolik bağlantı olmamalıdır.")); return;
        }
        QSaveFile file(destination);
        const QByteArray text = ("[Desktop Entry]\nType=Application\nName=Citroën C4 Robot Native Launcher\n"
            "Comment=Native Linux reconstruction launcher\nExec=" + desktopArgument(executable) +
            "\nTerminal=false\nCategories=Game;\n").toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size() || !file.commit()) failure(file.errorString());
        else append(t("User application-menu shortcut created.", "Kullanıcı uygulama menüsü kısayolu oluşturuldu."));
    }
    void validateAsync(const QString &root, const std::function<void()> &success) {
        disconnect(&validation_, nullptr, this, nullptr);
        connect(&validation_, &QFutureWatcher<QString>::finished, this, [this, success] {
            const QString error = validation_.result();
            if (cancelled_) { setProperty("nativeRunning", false); append(t("Operation cancelled; active assets unchanged.", "İşlem iptal edildi; etkin varlıklar değişmedi.")); setBusy(false); return; }
            if (!error.isEmpty()) { setProperty("nativeRunning", false); setBusy(false); failure(error); return; }
            success();
        });
        validation_.setFuture(QtConcurrent::run([root] { return validateAssets(root); }));
    }
    void importAssets() {
        if (busy_ || !saveSettings()) return;
        const bool directorySource = config_.value("source_kind").toString() == "directory";
        const QString source = config_.value(directorySource ? "source_directory" : "installer").toString();
        const QString importer = importerPath();
        const QString python = deployedTool("runtime/bin/python3", "python3");
        const QString extractor = directorySource ? QString() : deployedTool("tools/innoextract", "innoextract");
        const QFileInfo sourceInfo(source);
        if (sourceInfo.isSymLink() || (directorySource ? !sourceInfo.isDir() : !sourceInfo.isFile())) {
            failure(directorySource
                ? t("Select your original installation application directory first. It is copied, never activated in place.",
                    "Önce özgün kurulumun uygulama dizinini seçin. Dizin kopyalanır, yerinde etkinleştirilmez.")
                : t("Select a regular original installer file first.", "Önce normal bir özgün kurulum dosyası seçin."));
            return;
        }
        if (importer.isEmpty() || python.isEmpty() || (!directorySource && extractor.isEmpty())) {
            failure(t("Import requires the packaged Python runtime and importer; installer input also requires native innoextract. Source builds may set C4_IMPORTER_PATH to the absolute tools/import_assets.py path.",
                      "Aktarma paketlenmiş Python ve aktarıcı gerektirir; kurulum dosyası ayrıca yerel innoextract gerektirir. Kaynak derlemesi C4_IMPORTER_PATH için tools/import_assets.py mutlak yolunu kullanabilir."));
            return;
        }
        const QString base = QDir::cleanPath(QFileInfo(config_.value("managed_root").toString()).absoluteFilePath() + "/assets");
        if (!directorySource && !QDir().mkpath(base)) {
            failure(t("Cannot create managed asset directory.", "Yönetilen varlık dizini oluşturulamıyor."));
            return;
        }
        stagedAssets_ = base + "/" + QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzz") + "-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        setBusy(true);
        setProperty("importRunning", true);
        setProperty("importPhase", "started");
        append(t("Importing into a fresh directory; current assets and the original source remain untouched:\n",
                 "Yeni dizine aktarılıyor; mevcut varlıklar ve özgün kaynak korunuyor:\n") + stagedAssets_);
        importProcess_.setProgram(python);
        QStringList arguments{"-I", "-u", importer, directorySource ? "--directory" : "--installer",
                              source, "--output", stagedAssets_};
        if (!directorySource) arguments << "--innoextract" << extractor;
        importProcess_.setArguments(arguments);
        importProcess_.start();
    }
    QString nativeGame() const {
        if (preferManaged_ ? preferManaged_->isChecked() : config_.value("prefer_managed").toBool()) {
            const QString root = updateRoot_ ? updateRoot_->text().trimmed() : config_.value("install_root").toString();
            const QString current = root + "/current";
            if (QFileInfo(current + "/bin/citroen-c4-robot").exists()) {
                // Resolve the activation link, not the executable: its directories/file still pass the strict ELF guard.
                const QString activated = QFileInfo(current).canonicalFilePath();
                if (activated.isEmpty() || QFileInfo(activated).absolutePath() != root + "/versions") return {};
                return activated + "/bin/citroen-c4-robot";
            }
        }
        return game_ ? game_->text().trimmed() : config_.value("executable").toString();
    }
    void invalidateGraphics() {
        qualityControls_->setCapabilities({});
        setProperty("graphicsCapabilities", QVariant::fromValue(QJsonObject{}));
    }
    void startGraphicsProbe(const QString &renderer) {
        ++graphicsGeneration_;
        graphicsRenderer_ = renderer;
        graphicsError_.clear(); graphicsOutput_.clear();
        graphicsProcess_.setProgram(graphicsExecutable_);
        graphicsProcess_.setProcessEnvironment(nativeEnvironment(graphicsExecutable_));
        const QStringList arguments{"--graphics-capabilities", "--renderer", renderer, "--quality", "original",
                                    "--rt-reflections", "off", "--rt-shadows", "off", "--rt-gi", "off"};
        graphicsProcess_.setArguments(arguments);
        graphicsProcess_.setWorkingDirectory(QFileInfo(graphicsExecutable_).absolutePath());
        graphicsTimeout_.start(60000);
        graphicsProcess_.start();
    }
    void probeGraphics() {
        if (busy_) return;
        qualityControls_->setCapabilities({});
        setProperty("graphicsCapabilities", QVariant::fromValue(QJsonObject{}));
        setProperty("graphicsProbeError", QString());
        graphicsExecutable_ = nativeGame();
        if (!isNativeElf(graphicsExecutable_)) {
            const QString error = t("Select an executable native game ELF before probing its actual graphics pipelines.",
                                    "Gerçek grafik işleme hatlarını yoklamadan önce çalıştırılabilir yerel oyun ELF seçin.");
            qualityControls_->setCapabilities({{"error", error}});
            setProperty("graphicsProbeError", error); append(error);
            return;
        }
        graphicsSelected_ = qualityControls_->renderer();
        graphicsBackends_ = {};
        setBusy(true); setProperty("graphicsProbeRunning", true);
        append(t("Probing actual native OpenGL and Vulkan initialization; hardware extensions alone do not enable controls.",
                 "Gerçek yerel OpenGL ve Vulkan başlatması yoklanıyor; yalnızca donanım uzantıları denetimleri etkinleştirmez."));
        startGraphicsProbe(graphicsSelected_);
    }
    void graphicsProbeFinished(int code, QProcess::ExitStatus status) {
        graphicsTimeout_.stop();
        graphicsOutput_ += graphicsProcess_.readAllStandardOutput();
        const QString stderrText = QString::fromUtf8(graphicsProcess_.readAllStandardError());
        if (!stderrText.isEmpty()) append(stderrText);
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(graphicsOutput_, &parse);
        const auto object = document.object();
        const bool available = !cancelled_ && graphicsError_.isEmpty() && code == 0 && status == QProcess::NormalExit
            && parse.error == QJsonParseError::NoError && document.isObject()
            && object.value("schema_version").toInt() == 1
            && object.value("renderer").toString() == graphicsRenderer_ && object.value("capabilities").isObject()
            && !object.contains("error");
        QString error = graphicsError_;
        if (!available && error.isEmpty()) error = cancelled_ ? t("Graphics probe cancelled.", "Grafik yoklaması iptal edildi.")
            : object.value("error").toString(stderrText.isEmpty()
                ? t("Native backend initialization failed or returned invalid capability JSON.",
                    "Yerel arka uç başlatması başarısız veya geçersiz yetenek JSON'u döndürdü.") : stderrText);
        graphicsBackends_[graphicsRenderer_] = QJsonObject{{"available", available}, {"error", error}, {"report", object}};
        if (!cancelled_ && graphicsBackends_.size() == 1) {
            startGraphicsProbe(graphicsRenderer_ == "opengl" ? "vulkan" : "opengl");
            return;
        }
        const auto selected = graphicsBackends_.value(graphicsSelected_).toObject();
        QJsonObject combined = selected.value("report").toObject();
        combined["backends"] = graphicsBackends_;
        if (!selected.value("available").toBool()) combined["error"] = selected.value("error");
        setProperty("graphicsProbeRunning", false);
        setProperty("graphicsCapabilities", QVariant::fromValue(combined));
        setProperty("graphicsProbeError", combined.value("error").toString());
        qualityControls_->setCapabilities(combined);
        hardware_->appendPlainText(QString::fromUtf8(QJsonDocument(combined).toJson()));
        setBusy(false);
    }
    void launchGame() {
        if ((busy_ && !backgroundDownload_) || property("nativeRunning").toBool()) return;
        if (!backgroundDownload_ && !saveSettings()) return;
        const QString executable = nativeGame();
        const QString assets = config_.value("assets").toString();
        if (!isNativeElf(executable)) {
            failure(t("Select an executable native ELF matching this Linux host; Windows files and scripts are refused.",
                      "Bu Linux sistemiyle uyumlu çalıştırılabilir yerel ELF seçin; Windows dosyaları ve betikler reddedilir.")); return;
        }
        if (assets.isEmpty()) { failure(t("Import and validate assets first.", "Önce varlıkları aktarın ve doğrulayın.")); return; }
        setProperty("nativeRunning", true);
        if (!backgroundDownload_) setBusy(true);
        else launch_->setEnabled(false);
        setProperty("nativeExecutable", QFileInfo(executable).canonicalFilePath());
        append(t("Validating active asset hashes before launching…", "Başlatmadan önce etkin varlık özetleri doğrulanıyor…"));
        const QStringList arguments{"--game-dir", assets, "--width", QString::number(width_->value()),
            "--height", QString::number(height_->value()), "--vsync", vsync_->isChecked() ? "on" : "off",
            "--frame-limit", QString::number(limit_->value())};
        QStringList actual = arguments;
        actual.append(qualityControls_->arguments());
        actual << (fullscreen_->isChecked() ? "--fullscreen" : "--windowed");
        const QString integrationRoot = property("integrationRoot").toString();
        if (!integrationRoot.isEmpty()) {
            bool capValid = false;
            int frames = property("integrationFrameCount").toInt(&capValid);
            if (!capValid) frames = 3;
            if (frames < 1 || frames > 600) {
                setProperty("nativeRunning", false); setBusy(false);
                failure(t("Integration frame cap must be between 1 and 600.",
                          "Bütünleştirme kare sınırı 1 ile 600 arasında olmalıdır.")); return;
            }
            setProperty("nativeFrames", frames);
            const QString suffix = ++nativeLaunchCount_ == 1 ? QString() : "-" + QString::number(nativeLaunchCount_);
            const QString capture = integrationRoot + "/native" + suffix + ".ppm";
            const QString diagnostics = integrationRoot + "/native" + suffix + ".json";
            setProperty("nativeCapturePath", capture); setProperty("nativeDiagnosticsPath", diagnostics);
            actual << "--frames" << QString::number(frames) << "--capture" << capture << "--diagnostics-json" << diagnostics;
        }
        validateAsync(assets, [this, executable, actual] {
            showValidatedArtwork(config_.value("assets").toString());
            if (!property("integrationRoot").toString().isEmpty()) {
                nativeProcess_.setProgram(executable); nativeProcess_.setArguments(actual);
                nativeProcess_.setProcessEnvironment(nativeEnvironment(executable));
                nativeProcess_.setWorkingDirectory(QFileInfo(executable).absolutePath());
                nativeProcess_.start();
            } else {
                QProcess process;
                process.setProgram(executable); process.setArguments(actual);
                process.setProcessEnvironment(nativeEnvironment(executable));
                process.setWorkingDirectory(QFileInfo(executable).absolutePath());
                qint64 pid = 0;
                const bool started = process.startDetached(&pid);
                setProperty("nativeRunning", false);
                setBusy(false);
                if (!started) failure(process.errorString());
                else append(t("Native game process started, PID ", "Yerel oyun süreci başlatıldı, PID ") + QString::number(pid));
            }
        });
    }
    bool writeUpdaterConfig(QString &path) {
        path = QFileInfo(configPath()).absolutePath() + "/updater.json";
        QJsonObject updater{{"repository", config_.value("repository")}, {"trusted_key", config_.value("trusted_key")},
                            {"install_root", config_.value("install_root")}, {"channel", config_.value("channel")}};
        const QString error = saveJson(path, updater);
        if (!error.isEmpty()) { failure(error, quietUpdate_); return false; }
        return true;
    }
    void updateErrorLine(const QByteArray &line) {
        const auto document = QJsonDocument::fromJson(line);
        const auto object = document.object();
        const QString event = object.value("event").toString();
        if ((updateAction_ == "--download" || updateAction_ == "--install") && event == "download_progress") {
            const auto receivedValue = object.value("received_bytes"), totalValue = object.value("total_bytes");
            const qint64 received = receivedValue.toInteger(-1), total = totalValue.toInteger(-1);
            if (object.size() != 3 || !receivedValue.isDouble() || !totalValue.isDouble()
                || received < 0 || total <= 0 || total > 512LL * 1024 * 1024 || received > total
                || receivedValue.toDouble() != double(received) || totalValue.toDouble() != double(total)
                || (downloadTotal_ >= 0 && (total != downloadTotal_ || received < downloadReceived_))) {
                append(t("Invalid updater progress ignored.", "Geçersiz güncelleyici ilerlemesi yok sayıldı."));
                return;
            }
            downloadReceived_ = received; downloadTotal_ = total;
            const QString text = t("Downloading: ", "İndiriliyor: ") + QString::number(received) + " / " + QString::number(total) + t(" bytes", " bayt");
            progress_->setRange(0, int(total));
            progress_->setValue(int(received));
            progress_->setFormat(text); progress_->setTextVisible(true);
            progress_->setAccessibleName(t("Signed package download progress", "İmzalı paket indirme ilerlemesi"));
            progress_->setAccessibleDescription(text);
            return;
        }
        if ((updateAction_ == "--download" || updateAction_ == "--install") && event == "update_stage") {
            const QString stage = object.value("stage").toString();
            QString text;
            if (object.size() == 2 && stage == "verify") text = t("Verifying signed package and files…", "İmzalı paket ve dosyalar doğrulanıyor…");
            if (object.size() == 2 && stage == "extract") text = t("Extracting verified native package…", "Doğrulanmış yerel paket çıkarılıyor…");
            if (object.size() == 2 && stage == "preserve") text = t("Preserving authenticated native version…", "Kimliği doğrulanmış yerel sürüm saklanıyor…");
            if (!text.isEmpty()) {
                progress_->setRange(0, 0); progress_->setFormat(text); progress_->setTextVisible(true);
                progress_->setAccessibleName(t("Updater verification stage", "Güncelleyici doğrulama aşaması"));
                progress_->setAccessibleDescription(text); append(text);
                return;
            }
        }
        if (!line.isEmpty()) append(QString::fromUtf8(line));
    }
    void consumeUpdateError(bool finished = false) {
        const QByteArray bytes = updateProcess_.readAllStandardError();
        for (const char byte : bytes) {
            if (byte == '\n') {
                if (!oversizedUpdateLine_) updateErrorLine(updateErrorLine_);
                updateErrorLine_.clear(); oversizedUpdateLine_ = false;
            } else if (!oversizedUpdateLine_) {
                if (updateErrorLine_.size() < 4096) updateErrorLine_.append(byte);
                else {
                    append(QString::fromUtf8(updateErrorLine_) + t(" [stderr line truncated]", " [stderr satırı kısaltıldı]"));
                    updateErrorLine_.clear(); oversizedUpdateLine_ = true;
                }
            }
        }
        if (finished) {
            if (!oversizedUpdateLine_ && !updateErrorLine_.isEmpty()) updateErrorLine(updateErrorLine_);
            updateErrorLine_.clear(); oversizedUpdateLine_ = false;
        }
    }
    void runUpdate(const QString &action, bool quiet = false) {
        if (busy_ || !saveSettings(!quiet)) return;
        quietUpdate_ = quiet;
        if (config_.value("repository").toString().isEmpty() || config_.value("trusted_key").toString().isEmpty()) {
            failure(t("Updates are unconfigured: set your real owner/repository and pinned Ed25519 PEM public key in Settings. No release URL or trust key is assumed.",
                      "Güncellemeler yapılandırılmadı: Ayarlar'da gerçek sahip/depo ve sabitlenmiş Ed25519 PEM açık anahtarını girin. Yayın adresi veya güven anahtarı varsayılmaz."), quiet); return;
        }
        const QString executable = config_.value("updater").toString();
        if (!isNativeElf(executable)) { failure(t("Native c4-updater ELF is missing or incompatible.", "Yerel c4-updater ELF eksik veya uyumsuz."), quiet); return; }
        QString path;
        if (!writeUpdaterConfig(path)) return;
        updateAction_ = action; updateOutput_.clear(); updateErrorLine_.clear();
        oversizedUpdateLine_ = false; downloadReceived_ = downloadTotal_ = -1;
        backgroundDownload_ = action == "--download";
        if (backgroundDownload_) {
            const QString root = config_.value("install_root").toString();
            downloadCurrent_ = QFileInfo(root + "/current").canonicalFilePath();
            downloadPrevious_ = QFileInfo(root + "/previous").canonicalFilePath();
        }
        setBusy(true);
        if (backgroundDownload_) {
            progress_->setRange(0, 0); progress_->setValue(0);
            progress_->setAccessibleDescription(t("Waiting for authenticated package download.", "Kimliği doğrulanmış paket indirmesi bekleniyor."));
        }
        append(t("Updater operation: ", "Güncelleyici işlemi: ") + action);
        updateProcess_.setProgram(executable);
        updateProcess_.setProcessEnvironment(nativeEnvironment(executable));
        QStringList arguments{"--config", path, action};
        if (action != "--rollback" && action != "--verify-current" && !offlineRelease_->text().trimmed().isEmpty())
            arguments << "--release-dir" << offlineRelease_->text().trimmed();
        updateProcess_.setArguments(arguments);
        updateProcess_.start();
    }
    void updateFinished(int code, QProcess::ExitStatus status) {
        updateOutput_ += updateProcess_.readAllStandardOutput();
        consumeUpdateError(true);
        backgroundDownload_ = false;
        setBusy(property("nativeRunning").toBool());
        if (cancelled_) { append(t("Updater cancelled; consult operation log for activation status.", "Güncelleyici iptal edildi; etkinleştirme durumunu işlem günlüğünden inceleyin.")); refreshStatus(); return; }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(updateOutput_, &error);
        const auto object = document.object();
        if (code != 0 || status != QProcess::NormalExit || error.error != QJsonParseError::NoError || !document.isObject() || object.contains("error")) {
            failure(object.value("error").toString(t("Updater failed or returned invalid JSON.", "Güncelleyici başarısız veya geçersiz JSON döndürdü.")), quietUpdate_); return;
        }
        append(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
        if (updateAction_ == "--verify-current") {
            const QString installed = QFileInfo(object.value("install_path").toString()).canonicalFilePath();
            const QString current = QFileInfo(config_.value("install_root").toString() + "/current").canonicalFilePath();
            if (!object.value("verified").toBool() || !object.value("version").isString() ||
                installed.isEmpty() || installed != current) {
                failure(t("Updater did not authenticate the active launcher version.",
                          "Güncelleyici etkin başlatıcı sürümünü doğrulamadı.")); return;
            }
            launchVerifiedLauncher(installed);
            return;
        }
        if (updateAction_ == "--check") {
            if (!object.value("update_available").isBool() || !object.value("version").isString() || !object.value("notes").isString()) {
                failure(t("Updater check response is missing required fields.", "Güncelleme yanıtında gerekli alanlar eksik."), quietUpdate_); return;
            }
            notes_->setPlainText(t("Release: ", "Yayın: ") + object.value("version").toString() + "\n" + object.value("release").toString() + "\n\n" + object.value("notes").toString());
            const bool available = object.value("update_available").toBool();
            if (!available) append(t("No newer signed release found.", "Daha yeni imzalı yayın bulunamadı."));
            if (available && config_.value("auto_install").toBool()) QTimer::singleShot(0, this, [this] {
                if (!busy_ && config_.value("auto_install").toBool()) runUpdate("--install", true);
            });
            else if (available && config_.value("background_downloads").toBool()) QTimer::singleShot(0, this, [this] {
                if (!busy_ && !config_.value("auto_install").toBool() && config_.value("background_downloads").toBool()) runUpdate("--download", true);
            });
            else if (available && config_.value("notify_updates").toBool() && property("integrationRoot").toString().isEmpty()) QMessageBox::information(this, t("Native update available", "Yerel güncelleme mevcut"),
                t("A signed native release is available. See Updates for notes and install when ready.",
                  "İmzalı yerel yayın mevcut. Notlar için Güncellemeler'e bakın ve hazır olduğunuzda yükleyin."));
        } else if (updateAction_ == "--download") {
            const QString version = object.value("version").toString();
            const QString versions = QFileInfo(config_.value("install_root").toString() + "/versions").canonicalFilePath();
            const QString path = object.value("download_path").toString();
            const QString candidate = QFileInfo(path).canonicalFilePath();
            if (object.size() != 4 || !object.value("version").isString() || version.isEmpty()
                || version == "." || version == ".." || version.contains('/') || version.contains('\\')
                || !object.value("download_path").isString() || !object.value("downloaded").isBool()
                || !object.value("downloaded").toBool() || !object.value("already_downloaded").isBool()
                || versions.isEmpty() || candidate != versions + "/" + version || path != candidate
                || QFileInfo(candidate).isSymLink()
                || QFileInfo(config_.value("install_root").toString() + "/current").canonicalFilePath() != downloadCurrent_
                || QFileInfo(config_.value("install_root").toString() + "/previous").canonicalFilePath() != downloadPrevious_
                || !isNativeElf(candidate + "/bin/citroen-c4-robot")
                || !isNativeElf(candidate + "/bin/c4-launcher") || !isNativeElf(candidate + "/bin/c4-updater")) {
                failure(t("Updater download response did not identify a complete immutable native candidate.",
                          "Güncelleyici indirme yanıtı eksiksiz değişmez yerel adayı tanımlamadı."), quietUpdate_); return;
            }
            const QString ready = t("Signed native release ready to install: ", "İmzalı yerel yayın yüklemeye hazır: ") + version
                + t(". Download did not activate it; current version and history are unchanged.",
                    ". İndirme etkinleştirmedi; geçerli sürüm ve geçmiş değişmedi.");
            append(ready); refreshStatus();
            if (config_.value("notify_updates").toBool() && property("integrationRoot").toString().isEmpty())
                QMessageBox::information(this, t("Native download ready", "Yerel indirme hazır"), ready);
        } else {
            if (!object.value("version").isString() || !object.value("install_path").isString()) {
                failure(t("Updater activation response is missing required fields.", "Güncelleyici etkinleştirme yanıtında alanlar eksik."), quietUpdate_); return;
            }
            const QString installed = QFileInfo(object.value("install_path").toString()).canonicalFilePath();
            const QString current = QFileInfo(config_.value("install_root").toString() + "/current").canonicalFilePath();
            if (installed.isEmpty() || installed != current ||
                !isNativeElf(installed + "/bin/c4-launcher") || !isNativeElf(installed + "/bin/c4-updater")) {
                failure(t("Activated release is missing its native launcher/updater or differs from current.",
                          "Etkin yayın yerel başlatıcı/güncelleyici içermiyor veya geçerli sürümden farklı.")); return;
            }
            record(updateAction_, object.value("version").toString());
            updater_->setText(installed + "/bin/c4-updater");
            game_->setText(installed + "/bin/citroen-c4-robot");
            if (!saveSettings()) return;
            if (property("integrationRoot").toString().isEmpty())
                QTimer::singleShot(0, this, [this] { if (!busy_) probeGraphics(); });
            append(t("Activation complete. New launches use the managed version; running processes, settings and original assets are preserved.",
                     "Etkinleştirme tamamlandı. Yeni başlatmalar yönetilen sürümü kullanır; çalışan süreçler, ayarlar ve özgün varlıklar korunur."));
        }
    }
    void restartLauncher() { runUpdate("--verify-current"); }
    void launchVerifiedLauncher(const QString &activated) {
        const QString root = config_.value("install_root").toString();
        const QString versions = QFileInfo(root + "/versions").canonicalFilePath();
        const QString executable = activated + "/bin/c4-launcher";
        if (versions.isEmpty() || !activated.startsWith(versions + "/") || !isNativeElf(executable)) {
            failure(t("Install a verified managed native release before restarting its launcher.",
                      "Başlatıcıyı yeniden başlatmadan önce doğrulanmış yönetilen yerel yayın yükleyin.")); return;
        }
        // Signature, inventory, modes and hashes were just reverified by the native updater.
        // Relaunch the immutable versioned path, never the old AppImage mount or current symlink.
        const QString integrationRoot = property("integrationRoot").toString();
        if (!integrationRoot.isEmpty()) {
            setBusy(true); setProperty("restartRunning", true);
            restartProcess_.setProgram(executable);
            restartProcess_.setProcessEnvironment(nativeEnvironment(executable));
            restartProcess_.setArguments({"--integration-restart", integrationRoot});
            restartProcess_.setWorkingDirectory(activated);
            restartProcess_.start();
        } else {
            QProcess process;
            process.setProgram(executable); process.setWorkingDirectory(activated);
            process.setProcessEnvironment(nativeEnvironment(executable));
            if (!process.startDetached()) failure(process.errorString());
            else QCoreApplication::quit();
        }
    }
    void createUi() {
        setWindowTitle(t("Citroën C4 Robot • Native Launcher", "Citroën C4 Robot • Yerel Başlatıcı"));
        resize(1050, 790);
        auto *central = new QWidget;
        auto *layout = new QVBoxLayout(central);
        layout->setContentsMargins(18, 16, 18, 16);
        auto *heading = label(t("C4 ROBOT  /  NATIVE LINUX", "C4 ROBOT  /  YEREL LINUX"));
        heading->setStyleSheet("font-size: 23px; font-weight: 600; color: #f2c66d; padding: 6px;");
        layout->addWidget(heading);
        tabs_ = new QTabWidget;
        tabs_->setAccessibleName(t("Launcher sections", "Başlatıcı bölümleri"));
        layout->addWidget(tabs_, 1);
        QVBoxLayout *p;
        page(t("Home", "Ana Sayfa"), p);
        artwork_ = new QLabel;
        artwork_->setAccessibleName(t("Locally imported original C4 Robot title artwork", "Yerel aktarılmış özgün C4 Robot başlık görseli"));
        artwork_->setMaximumHeight(130); artwork_->hide(); p->addWidget(artwork_);
        p->addWidget(label(t("Native reconstruction — original menus and game runtime; compatibility verification is in progress.",
                            "Yerel yeniden oluşturma — özgün menüler ve oyun uygulaması; uyumluluk doğrulaması sürüyor.")));
        p->addWidget(label(t("Import your own installer or original installation directory locally and launch the native Linux game. Neither the original Windows executable, Windows DLLs nor Wine are used at runtime. Original assets are never packaged in native updates.",
                            "Kendi kurulum dosyanızı veya özgün kurulum dizininizi yerel olarak aktarın ve yerel Linux oyununu başlatın. Özgün Windows uygulaması, Windows DLL'leri ve Wine çalışma sırasında kullanılmaz. Özgün varlıklar yerel güncellemelere eklenmez.")));
        assetStatus_ = label(""); p->addWidget(assetStatus_);
        versionStatus_ = label(""); p->addWidget(versionStatus_);
        launch_ = button(t("Launch native game", "Yerel oyunu başlat"), p, [this] { launchGame(); });
        launch_->setMinimumHeight(48);
        p->addStretch();
        page(t("Installation", "Kurulum"), p);
        p->addWidget(label(t("Choose your original installer or original application directory, not the managed destination. Import and repair copy into a fresh versioned directory; original catalog and hash/tree checks must succeed before activation. Originals and previous active assets are preserved.",
                            "Yönetilen hedefi değil, özgün kurulum dosyanızı veya uygulama dizininizi seçin. Aktarma ve onarım yeni sürümlü dizine kopyalar; etkinleştirme için özgün katalog ile özet/dizin denetimleri başarılı olmalıdır. Özgün kaynak ve önceki etkin varlıklar korunur.")));
        auto *form = new QFormLayout; p->addLayout(form);
        sourceKind_ = new QComboBox;
        sourceKind_->addItem(t("Original installer", "Özgün kurulum dosyası"), "installer");
        sourceKind_->addItem(t("Original installation directory", "Özgün kurulum dizini"), "directory");
        sourceKind_->setObjectName("importSource");
        sourceKind_->setAccessibleName(t("Import source", "Aktarma kaynağı"));
        sourceKind_->setCurrentIndex(config_.value("source_kind").toString() == "directory" ? 1 : 0);
        form->addRow(t("Import source", "Aktarma kaynağı"), sourceKind_);
        installer_ = pathField(form, t("Original installer", "Özgün kurulum dosyası"), config_.value("installer").toString(), false, "Installer (*.exe);;All files (*)");
        originalDirectory_ = pathField(form, t("Original installation application directory", "Özgün kurulum uygulama dizini"),
                                       config_.value("source_directory").toString(), true);
        originalDirectory_->setObjectName("originalDirectory");
        originalDirectory_->setToolTip(t("Read-only original source containing C4_Robot.exe and game resources, or its parent containing app/. No native asset manifest is required.",
                                        "C4_Robot.exe ve oyun kaynaklarını içeren salt okunur özgün kaynak veya app/ içeren üst dizini. Yerel varlık bildirimi gerekmez."));
        const auto sourceChanged = [this] {
            const bool directory = sourceKind_->currentData().toString() == "directory";
            installer_->setEnabled(!directory);
            originalDirectory_->setEnabled(directory);
        };
        connect(sourceKind_, &QComboBox::currentIndexChanged, this, sourceChanged);
        sourceChanged();
        managedRoot_ = pathField(form, t("Managed asset root", "Yönetilen varlık kökü"), config_.value("managed_root").toString(), true);
        shortcut_ = checkbox(form, t("Create user application-menu shortcut after validated import", "Doğrulanmış aktarma sonrası kullanıcı menüsü kısayolu oluştur"), "shortcut");
        auto *imports = new QHBoxLayout; p->addLayout(imports);
        import_ = button(t("Import and validate", "Aktar ve doğrula"), imports, [this] { importAssets(); });
        repair_ = button(t("Repair via fresh import", "Yeni aktarma ile onar"), imports, [this] { importAssets(); });
        p->addWidget(label(t("Release packages include the native extractor, Python runtime and original-file checksum catalog. Only installer input needs innoextract. Directory input validates the supported original catalog without claiming installer provenance. Hash progress is indeterminate, not an invented percentage. Original assets remain local.",
                            "Yayın paketleri yerel çıkarıcı, Python ve özgün dosya özet kataloğunu içerir. Yalnızca kurulum dosyası innoextract gerektirir. Dizin kaynağı kurulum dosyası kökeni iddia etmeden desteklenen özgün kataloğu doğrular. Özet ilerlemesi belirsizdir; uydurma yüzde gösterilmez. Özgün varlıklar yerel kalır.")));
        p->addStretch();
        page(t("Graphics", "Grafikler"), p);
        auto *graphicsScroll = new QScrollArea;
        graphicsScroll->setWidgetResizable(true);
        auto *graphicsContent = new QWidget;
        auto *graphicsLayout = new QVBoxLayout(graphicsContent);
        graphicsScroll->setWidget(graphicsContent);
        p->addWidget(graphicsScroll);
        p = graphicsLayout;
        form = new QFormLayout; p->addLayout(form);
        qualityControls_ = std::make_unique<QualityControls>(*form, config_, turkish_, [this] {
            if (qualityControls_ && game_ && probeGraphics_) probeGraphics();
        });
        width_ = number(form, t("Window width", "Pencere genişliği"), "width", 320, 7680);
        height_ = number(form, t("Window height", "Pencere yüksekliği"), "height", 240, 4320);
        fullscreen_ = checkbox(form, t("Fullscreen (display desktop size)", "Tam ekran (masaüstü ekran boyutu)"), "fullscreen");
        vsync_ = checkbox(form, t("VSync (driver/context permitting)", "VSync (sürücü/bağlam izin verirse)"), "vsync");
        limit_ = number(form, t("Frame limit (0 = unlimited)", "Kare sınırı (0 = sınırsız)"), "frame_limit", 0, 1000);
        p->addWidget(label(t("Reported GPU capabilities do NOT mean a renderer is implemented. Qt reports current screen geometry/refresh only, not an exhaustive display-mode list. OpenGL probe availability is not a game-renderer test.",
                            "Bildirilen GPU yetenekleri çizicinin uygulandığı anlamına GELMEZ. Qt yalnızca geçerli ekran boyutu ve yenilemeyi bildirir; tüm ekran kiplerini listelemez. OpenGL yoklaması oyun çizicisi testi değildir.")));
        hardware_ = new QPlainTextEdit; hardware_->setReadOnly(true); hardware_->setAccessibleName(t("Actual hardware capability report", "Gerçek donanım yetenek raporu")); p->addWidget(hardware_, 1);
        button(t("Refresh actual hardware report", "Gerçek donanım raporunu yenile"), p, [this] {
            hardware_->setPlainText(QString::fromUtf8(QJsonDocument(probeHardware()).toJson()));
        });
        probeGraphics_ = button(t("Probe native graphics pipelines", "Yerel grafik işleme hatlarını yokla"), p, [this] { probeGraphics(); });
        page(t("Updates", "Güncellemeler"), p);
        p->addWidget(label(t("Native project files only. Signed manifest verification, package hashes and safe versioned activation are enforced by c4-updater. No repository or signing key is trusted by default. Configure them in Settings.",
                            "Yalnızca yerel proje dosyaları. İmzalı bildirim doğrulaması, paket özetleri ve güvenli sürümlü etkinleştirme c4-updater tarafından uygulanır. Varsayılan güvenilen depo veya anahtar yoktur. Ayarlar'da yapılandırın.")));
        auto *updates = new QHBoxLayout; p->addLayout(updates);
        check_ = button(t("Check signed release", "İmzalı yayını denetle"), updates, [this] { runUpdate("--check"); });
        downloadUpdate_ = button(t("Download signed update", "İmzalı güncellemeyi indir"), updates, [this] { runUpdate("--download"); });
        installUpdate_ = button(t("Install signed update", "İmzalı güncellemeyi yükle"), updates, [this] { runUpdate("--install"); });
        rollback_ = button(t("Rollback native version", "Yerel sürümü geri al"), updates, [this] { runUpdate("--rollback"); });
        restart_ = button(t("Restart activated launcher", "Etkin başlatıcıyı yeniden başlat"), p, [this] { restartLauncher(); });
        notes_ = new QPlainTextEdit; notes_->setReadOnly(true); notes_->setAccessibleName(t("Release notes", "Sürüm notları"));
        notes_->setPlaceholderText(t("Release notes appear only after a successful real signed release check.", "Sürüm notları yalnızca gerçek imzalı yayın denetimi sonrası görünür.")); p->addWidget(notes_, 1);
        history_ = new QPlainTextEdit; history_->setReadOnly(true); history_->setAccessibleName(t("Update history and installed versions", "Güncelleme geçmişi ve yüklü sürümler")); p->addWidget(history_, 1);
        page(t("Settings", "Ayarlar"), p);
        form = new QFormLayout; p->addLayout(form);
        game_ = pathField(form, t("Native game ELF", "Yerel oyun ELF dosyası"), config_.value("executable").toString());
        connect(game_, &QLineEdit::textChanged, this, [this] { invalidateGraphics(); });
        preferManaged_ = checkbox(form, t("Prefer activated managed native version", "Etkin yönetilen yerel sürümü tercih et"), "prefer_managed");
        connect(preferManaged_, &QCheckBox::toggled, this, [this] { invalidateGraphics(); });
        updater_ = pathField(form, t("Native updater ELF", "Yerel güncelleyici ELF dosyası"), config_.value("updater").toString());
        repository_ = new QLineEdit(config_.value("repository").toString());
        repository_->setAccessibleName(t("Release repository owner/repo", "Yayın deposu sahip/depo"));
        form->addRow(t("Release repository (owner/repo)", "Yayın deposu (sahip/depo)"), repository_);
        key_ = pathField(form, t("Pinned Ed25519 public PEM key", "Sabitlenmiş Ed25519 PEM açık anahtarı"), config_.value("trusted_key").toString());
        updateRoot_ = pathField(form, t("Native versioned install root", "Yerel sürümlü kurulum kökü"), config_.value("install_root").toString(), true);
        offlineRelease_ = pathField(form, t("Offline signed release directory", "Çevrimdışı imzalı yayın dizini"),
                                   config_.value("offline_release").toString(), true);
        form->addRow(label(t("Optional offline release uses the same pinned signature, hashes and safe updater activation as HTTPS releases. Leave empty for GitHub.",
                             "Çevrimdışı yayın HTTPS ile aynı sabitlenmiş imza, özet ve güvenli etkinleştirmeyi kullanır. GitHub için boş bırakın.")));
        channel_ = new QComboBox; channel_->addItem(t("Stable", "Kararlı"), "stable"); channel_->addItem(t("Experimental", "Deneysel"), "experimental");
        channel_->setCurrentIndex(config_.value("channel").toString() == "experimental" ? 1 : 0);
        channel_->setAccessibleName(t("Release channel", "Yayın kanalı")); form->addRow(t("Release channel", "Yayın kanalı"), channel_);
        language_ = new QComboBox; language_->addItem("English", "en"); language_->addItem("Türkçe", "tr"); language_->setCurrentIndex(turkish_ ? 1 : 0);
        language_->setAccessibleName(t("Launcher language", "Başlatıcı dili")); form->addRow(t("Launcher language (after restart)", "Başlatıcı dili (yeniden başlatma sonrası)"), language_);
        startup_ = checkbox(form, t("Check updates at startup", "Başlangıçta güncellemeleri denetle"), "startup_check");
        background_ = checkbox(form, t("Check in background every hour while launcher runs", "Başlatıcı açıkken arka planda saatte bir denetle"), "background_check");
        downloads_ = checkbox(form, t("Download available signed updates in background without installing", "Mevcut imzalı güncellemeleri yüklemeden arka planda indir"), "background_downloads");
        notify_ = checkbox(form, t("Notify when signed update is available", "İmzalı güncelleme mevcutken bildir"), "notify_updates");
        automatic_ = checkbox(form, t("Automatically install verified native updates", "Doğrulanmış yerel güncellemeleri otomatik yükle"), "auto_install");
        p->addWidget(label(t("English/Turkish launcher translations. Original Turkish help is confirmed; original English game content is not verified. Settings are separate atomic XDG JSON. No setting enables unimplemented gameplay or rendering.",
                            "İngilizce/Türkçe başlatıcı çevirileri. Özgün Türkçe yardım doğrulandı; özgün İngilizce oyun içeriği doğrulanmadı. Ayarlar ayrı atomik XDG JSON'dur. Hiçbir ayar uygulanmamış oynanış veya çizimi etkinleştirmez.")));
        p->addStretch();
        auto *controls = new QHBoxLayout;
        layout->addLayout(controls);
        save_ = button(t("Save settings", "Ayarları kaydet"), controls, [this] {
            if (saveSettings()) append(t("Settings saved. Language changes apply after restart.", "Ayarlar kaydedildi. Dil değişiklikleri yeniden başlatma sonrası uygulanır."));
        });
        cancel_ = button(t("Cancel operation", "İşlemi iptal et"), controls, [this] {
            cancelled_ = true;
            if (importProcess_.state() != QProcess::NotRunning) importProcess_.terminate();
            if (updateProcess_.state() != QProcess::NotRunning) updateProcess_.terminate();
            if (nativeProcess_.state() != QProcess::NotRunning) nativeProcess_.terminate();
            if (restartProcess_.state() != QProcess::NotRunning) restartProcess_.terminate();
            if (graphicsProcess_.state() != QProcess::NotRunning) graphicsProcess_.terminate();
            append(t("Cancellation requested. Validation may finish reading, but activation is suppressed.",
                     "İptal istendi. Doğrulama okumayı tamamlayabilir, ancak etkinleştirme engellenir."));
        });
        cancel_->setEnabled(false);
        progress_ = new QProgressBar; progress_->setRange(0, 1); progress_->setValue(0); progress_->setTextVisible(false);
        progress_->setAccessibleName(t("Operation busy indicator", "İşlem meşgul göstergesi")); controls->addWidget(progress_, 1);
        log_ = new QPlainTextEdit; log_->setReadOnly(true); log_->setMaximumBlockCount(2000); log_->setMaximumHeight(140);
        log_->setAccessibleName(t("Real operation log", "Gerçek işlem günlüğü")); layout->addWidget(log_);
        setCentralWidget(central);
        setStyleSheet("QMainWindow,QWidget {background:#171c24;color:#e8edf4;} "
                      "QTabWidget::pane {border:1px solid #364150;} QTabBar::tab {padding:11px 18px;background:#222a35;} "
                      "QTabBar::tab:selected {background:#364150;color:#f2c66d;} "
                      "QLineEdit,QSpinBox,QComboBox,QPlainTextEdit {background:#10151c;border:1px solid #455363;padding:5px;selection-background-color:#866627;} "
                      "QPushButton {background:#344252;border:1px solid #586b80;border-radius:4px;padding:8px 14px;} "
                      "QPushButton:hover {background:#455b72;} QPushButton:focus {border:2px solid #f2c66d;} "
                      "QWidget:disabled {color:#8593a3;} QCheckBox {spacing:8px;} "
                      "QProgressBar {background:#10151c;border:1px solid #455363;height:12px;} QProgressBar::chunk {background:#f2c66d;}");
        refreshStatus();
    }
protected:
    void closeEvent(QCloseEvent *event) override {
        if (busy_) {
            QMessageBox::information(this, t("Operation running", "İşlem sürüyor"),
                t("Cancel the operation and wait for it to finish before closing.", "Kapatmadan önce işlemi iptal edin ve bitmesini bekleyin."));
            event->ignore();
        } else event->accept();
    }
public:
    ~Launcher() override {
        graphicsTimeout_.stop(); backgroundTimer_.stop();
        disconnect(&validation_, nullptr, this, nullptr);
        for (auto *process : {&importProcess_, &updateProcess_, &nativeProcess_, &restartProcess_, &graphicsProcess_})
            disconnect(process, nullptr, this, nullptr);
    }
    explicit Launcher(bool smoke = false) {
        readConfig(); turkish_ = config_.value("language").toString() == "tr";
        createUi();
        if (!loadError_.isEmpty()) append(loadError_);
        importProcess_.setProcessChannelMode(QProcess::MergedChannels);
        connect(&importProcess_, &QProcess::readyReadStandardOutput, this, [this] {
            const auto output = importProcess_.readAllStandardOutput();
            if (!output.isEmpty()) setProperty("importPhase", "output");
            append(QString::fromUtf8(output));
        });
        connect(&importProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) { setProperty("importRunning", false); setProperty("importPhase", "idle"); setBusy(false); failure(importProcess_.errorString()); }
        });
        connect(&importProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
            append(QString::fromUtf8(importProcess_.readAllStandardOutput()));
            setProperty("importRunning", false); setProperty("importPhase", "idle");
            if (cancelled_ || code != 0 || status != QProcess::NormalExit) {
                setBusy(false);
                failure(t("Import failed or was cancelled. Previous assets are unchanged. Any incomplete staged directory is not activated.",
                          "Aktarma başarısız veya iptal edildi. Önceki varlıklar değişmedi. Eksik hazırlık dizini etkinleştirilmez.")); return;
            }
            append(t("Source acquisition complete. Verifying every manifest size/hash and tree completeness…",
                     "Kaynak alma tamamlandı. Her bildirim boyutu/özeti ve dizin bütünlüğü doğrulanıyor…"));
            setProperty("importPhase", "validating");
            validateAsync(stagedAssets_, [this] {
                QJsonObject next = config_; next["assets"] = stagedAssets_;
                const QString error = writeConfig(next);
                setBusy(false);
                setProperty("importPhase", "idle");
                if (!error.isEmpty()) { failure(error); return; }
                config_ = next;
                showValidatedArtwork(stagedAssets_);
                append(t("Assets validated and atomically activated. Previous directories preserved.",
                         "Varlıklar doğrulandı ve atomik etkinleştirildi. Önceki dizinler korundu."));
                refreshStatus(); if (shortcut_->isChecked()) makeShortcut();
            });
        });
        connect(&updateProcess_, &QProcess::readyReadStandardOutput, this, [this] {
            const QByteArray bytes = updateProcess_.readAllStandardOutput();
            if (updateOutput_.size() + bytes.size() > 8 * 1024 * 1024) {
                cancelled_ = true; updateProcess_.terminate(); append(t("Updater output exceeded safety limit.", "Güncelleyici çıktısı güvenlik sınırını aştı."));
            } else updateOutput_ += bytes;
        });
        connect(&updateProcess_, &QProcess::readyReadStandardError, this, [this] { consumeUpdateError(); });
        connect(&updateProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                consumeUpdateError(true); backgroundDownload_ = false;
                setBusy(property("nativeRunning").toBool()); failure(updateProcess_.errorString(), quietUpdate_);
            }
        });
        connect(&updateProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &Launcher::updateFinished);
        const auto observeChild = [this](QProcess &process, const char *running, const char *code, const char *normal) {
            connect(&process, &QProcess::readyReadStandardError, this, [this, &process] { append(QString::fromUtf8(process.readAllStandardError())); });
            connect(&process, &QProcess::errorOccurred, this, [this, &process, running](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) { setProperty(running, false); setBusy(false); failure(process.errorString()); }
            });
            connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                    [this, running, code, normal](int exitCode, QProcess::ExitStatus status) {
                setProperty(running, false); setProperty(code, exitCode);
                setProperty(normal, status == QProcess::NormalExit); setBusy(false);
                if (exitCode != 0 || status != QProcess::NormalExit)
                    failure(t("Native child failed; inspect the real operation log.", "Yerel alt süreç başarısız; gerçek işlem günlüğünü inceleyin."));
            });
        };
        observeChild(nativeProcess_, "nativeRunning", "nativeExitCode", "nativeNormalExit");
        observeChild(restartProcess_, "restartRunning", "restartExitCode", "restartNormalExit");
        graphicsTimeout_.setSingleShot(true);
        connect(&graphicsTimeout_, &QTimer::timeout, this, [this] {
            graphicsError_ = t("Native graphics initialization timed out.", "Yerel grafik başlatması zaman aşımına uğradı.");
            const unsigned generation = graphicsGeneration_;
            graphicsProcess_.terminate();
            QTimer::singleShot(3000, this, [this, generation] {
                if (graphicsGeneration_ == generation && graphicsProcess_.state() != QProcess::NotRunning) graphicsProcess_.kill();
            });
        });
        connect(&graphicsProcess_, &QProcess::readyReadStandardOutput, this, [this] {
            const auto bytes = graphicsProcess_.readAllStandardOutput();
            if (graphicsOutput_.size() + bytes.size() > 2 * 1024 * 1024) {
                graphicsError_ = t("Native graphics capability output exceeded 2 MiB.",
                                   "Yerel grafik yetenek çıktısı 2 MiB sınırını aştı.");
                graphicsProcess_.terminate();
            } else graphicsOutput_ += bytes;
        });
        connect(&graphicsProcess_, &QProcess::readyReadStandardError, this, [this] {
            append(QString::fromUtf8(graphicsProcess_.readAllStandardError()));
        });
        connect(&graphicsProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                graphicsError_ = graphicsProcess_.errorString();
                graphicsProbeFinished(-1, QProcess::CrashExit);
            }
        });
        connect(&graphicsProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &Launcher::graphicsProbeFinished);
        connect(&backgroundTimer_, &QTimer::timeout, this, [this] { if (!busy_) runUpdate("--check", true); });
        if (!smoke) {
            QTimer::singleShot(0, this, [this] { hardware_->setPlainText(QString::fromUtf8(QJsonDocument(probeHardware()).toJson())); });
            if (config_.value("background_check").toBool()) backgroundTimer_.start(60 * 60 * 1000);
            if (config_.value("startup_check").toBool()) QTimer::singleShot(100, this, [this] { runUpdate("--check", true); });
            QTimer::singleShot(200, this, [this] { if (!busy_) probeGraphics(); });
        }
    }
    void restartProof(const QString &root) {
        const auto loadedConfig = config_;
        const QString gameField = game_->text();
        const QString selectedGame = QFileInfo(nativeGame()).canonicalFilePath();
        const QString evidenceRoot = root + "/restarted-native";
        if (!QDir().mkpath(evidenceRoot) || !grab().save(root + "/restarted.png", "PNG")) {
            QCoreApplication::exit(1); return;
        }
        setProperty("integrationRoot", evidenceRoot);
        setProperty("integrationFrameCount", 150);
        connect(&nativeProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, root, loadedConfig, gameField, selectedGame](int code, QProcess::ExitStatus status) {
            const QJsonObject native{{"executable", property("nativeExecutable").toString()},
                {"normal_exit", status == QProcess::NormalExit}, {"exit_code", code},
                {"frames", property("nativeFrames").toInt()},
                {"diagnostics", property("nativeDiagnosticsPath").toString()},
                {"capture", property("nativeCapturePath").toString()}};
            const QString error = saveJson(root + "/restarted.json",
                {{"executable", QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath()},
                 {"loaded_config", loadedConfig}, {"config", config_}, {"game_field", gameField},
                 {"selected_game", selectedGame}, {"native", native}});
            QCoreApplication::exit(code == 0 && status == QProcess::NormalExit && error.isEmpty() ? 0 : 1);
        });
        auto *completion = new QTimer(this);
        connect(completion, &QTimer::timeout, this, [this, completion] {
            if (!property("operationError").toString().isEmpty() && !busy_) {
                completion->stop(); QCoreApplication::exit(1);
            }
        });
        completion->start(10);
        // Use the same restored Home button, ELF guard, validation and child dispatch as ordinary Play.
        launch_->click();
        if (!property("nativeRunning").toBool()) QCoreApplication::exit(1);
    }
    bool smoke(const QString &screenshot) {
        // Exercise actual controls, atomic config and translations without importing or contacting a server.
        for (int index = 0; index < tabs_->count(); ++index) { tabs_->setCurrentIndex(index); QApplication::processEvents(); }
        width_->setValue(1366); height_->setValue(768); limit_->setValue(75); vsync_->setChecked(false);
        if (!saveSettings(false)) return false;
        QFile file(configPath()); if (!file.open(QIODevice::ReadOnly)) return false;
        const auto saved = QJsonDocument::fromJson(file.readAll()).object();
        if (saved.value("width").toInt() != 1366 || saved.value("height").toInt() != 768 || saved.value("vsync").toBool(true) || saved.value("frame_limit").toInt() != 75) return false;
        const QString bundledGame = nativeBinary("citroen-c4-robot"), bundledUpdater = nativeBinary("c4-updater");
        if (!isNativeElf(bundledGame) || !isNativeElf(bundledUpdater) ||
            saved.contains("executable") || saved.contains("updater")) return false;
        QJsonObject legacy = saved;
        const QString obsolete = "/tmp/.mount_C4Obsolete-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + "/usr/bin/";
        legacy["executable"] = obsolete + "citroen-c4-robot"; legacy["updater"] = obsolete + "c4-updater";
        if (!saveJson(configPath(), legacy).isEmpty()) return false;
        Launcher migrated(true);
        if (migrated.game_->text() != bundledGame || migrated.updater_->text() != bundledUpdater ||
            migrated.nativeGame() != bundledGame || !migrated.saveSettings(false)) return false;
        QFile migratedFile(configPath());
        if (!migratedFile.open(QIODevice::ReadOnly)) return false;
        const auto migratedSaved = QJsonDocument::fromJson(migratedFile.readAll()).object();
        if (migratedSaved.contains("executable") || migratedSaved.contains("updater")) return false;
        const QString custom = QCoreApplication::applicationFilePath();
        legacy["executable"] = custom; legacy["updater"] = custom; legacy["prefer_managed"] = false;
        if (!saveJson(configPath(), legacy).isEmpty()) return false;
        Launcher customized(true);
        if (customized.nativeGame() != custom || customized.updater_->text() != custom ||
            !customized.saveSettings(false)) return false;
        QFile customFile(configPath());
        if (!customFile.open(QIODevice::ReadOnly)) return false;
        const auto customSaved = QJsonDocument::fromJson(customFile.readAll()).object();
        if (customSaved.value("executable").toString() != custom || customSaved.value("updater").toString() != custom) return false;
        for (const auto &refused : {obsolete + "custom-game", configPath()}) {
            legacy["executable"] = refused;
            if (!saveJson(configPath(), legacy).isEmpty()) return false;
            Launcher invalid(true);
            invalid.setProperty("integrationRoot", QFileInfo(configPath()).absolutePath());
            if (invalid.nativeGame() != refused) return false;
            invalid.launch_->click();
            if (invalid.property("operationError").toString().isEmpty() || invalid.property("nativeRunning").toBool() ||
                !invalid.property("nativeExecutable").toString().isEmpty()) return false;
        }
        if (!saveSettings(false)) return false;
        if (!isNativeElf(QCoreApplication::applicationFilePath()) || validateAssets(QFileInfo(configPath()).absolutePath()).isEmpty()) return false;
        QTemporaryDir fixture;
        if (!fixture.isValid()) return false;
        const QByteArray payload("native launcher smoke fixture\n");
        QFile payloadFile(fixture.path() + "/payload.bin");
        if (!payloadFile.open(QIODevice::WriteOnly) || payloadFile.write(payload) != payload.size()) return false;
        payloadFile.close();
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        const QJsonObject manifest{{"schema_version", 1}, {"installer_sha256", hash},
            {"installer_size", payload.size()}, {"inno_data_version", "synthetic-smoke-fixture"},
            {"files", QJsonArray{QJsonObject{{"path", "payload.bin"}, {"size", payload.size()}, {"sha256", hash}}}}};
        if (!saveJson(fixture.path() + "/asset-manifest.json", manifest).isEmpty() || !validateAssets(fixture.path()).isEmpty()) return false;
        if (!payloadFile.open(QIODevice::Append) || payloadFile.write("x", 1) != 1) return false;
        payloadFile.close();
        if (validateAssets(fixture.path()).isEmpty()) return false;
        hardware_->setPlainText(QString::fromUtf8(QJsonDocument(probeHardware()).toJson()));
        language_->setCurrentIndex(1); if (!saveSettings(false)) return false;
        Launcher translated(true);
        if (translated.tabs_->tabText(0) != QString::fromUtf8("Ana Sayfa") || translated.width_->value() != 1366) return false;
        tabs_->setCurrentIndex(0); QApplication::processEvents();
        if (!screenshot.isEmpty() && !grab().save(screenshot, "PNG")) return false;
        return true;
    }
};

int main(int argc, char **argv) {
    bool smoke = false, diagnostics = false, missingValue = false;
    QString screenshot, restartRoot;
    IntegrationOptions integration;
    for (int index = 1; index < argc; ++index) {
        const QByteArray argument(argv[index]);
        const auto value = [&]() -> QString {
            if (index + 1 >= argc || argv[index + 1][0] == '-') { missingValue = true; return {}; }
            return QString::fromLocal8Bit(argv[++index]);
        };
        if (argument == "--smoke") {
            smoke = true;
            if (index + 1 < argc && argv[index + 1][0] != '-') screenshot = value();
        } else if (argument == "--diagnostics") diagnostics = true;
        else if (argument == "--integration") integration.root = value();
        else if (argument == "--installer") integration.installer = value();
        else if (argument == "--directory") integration.directory = value();
        else if (argument == "--first-release") integration.firstRelease = value();
        else if (argument == "--second-release") integration.secondRelease = value();
        else if (argument == "--trusted-key") integration.key = value();
        else if (argument == "--integration-restart") restartRoot = value();
        else if (argument == "--help") {
            std::puts("c4-launcher [--diagnostics | --smoke [PNG_PATH]]\n"
                      "c4-launcher --integration /tmp/c4-launcher-integration-NAME (--installer ORIGINAL_EXE | --directory ORIGINAL_APP_DIR)\n"
                      "  [--first-release SIGNED_DIR --second-release NEWER_SIGNED_DIR --trusted-key PEM]\n"
                      "Integration requires a fresh guarded temp root and a real display (private Xvfb is supported).\n"
                      "Artifacts are retained in ROOT; signed update/restart checks require all three release options.\n"
                      "--smoke: isolated offscreen widget/config check, not proof of a live desktop.");
            return 0;
        } else { std::fprintf(stderr, "Unknown argument: %s\n", argv[index]); return 2; }
    }
    if (missingValue) { std::fputs("Missing launcher option value; see --help.\n", stderr); return 2; }
    const bool integrationMode = !integration.root.isEmpty();
    const bool restartMode = !restartRoot.isEmpty();
    const bool anyRelease = !integration.firstRelease.isEmpty() || !integration.secondRelease.isEmpty() || !integration.key.isEmpty();
    const bool anySource = !integration.installer.isEmpty() || !integration.directory.isEmpty();
    const bool oneSource = integration.installer.isEmpty() != integration.directory.isEmpty();
    if ((integrationMode && (smoke || diagnostics || restartMode || !oneSource)) ||
        (restartMode && (smoke || diagnostics)) ||
        (anyRelease && (!integrationMode || integration.firstRelease.isEmpty() || integration.secondRelease.isEmpty() || integration.key.isEmpty())) ||
        (!integrationMode && anySource)) {
        std::fputs("Choose one launcher mode; integration requires exactly one installer/directory source and either zero or all three signed-release options.\n", stderr); return 2;
    }
    if (integrationMode || restartMode) {
        QString &root = integrationMode ? integration.root : restartRoot;
        const QString temp = QFileInfo(QDir::tempPath()).canonicalFilePath();
        const QString prefix = temp + "/c4-launcher-integration-";
        root = QDir::cleanPath(root);
        if (!QDir::isAbsolutePath(root) || !root.startsWith(prefix) || root.size() <= prefix.size() ||
            root.mid(prefix.size()).contains('/') || QFileInfo(root).isSymLink()) {
            std::fputs("Integration root must be a direct, non-symlink c4-launcher-integration-NAME child of the system temp directory.\n", stderr); return 2;
        }
        if (integrationMode) {
            if (QFileInfo::exists(root) || !QDir().mkdir(root) ||
                !QFile::setPermissions(root, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner) ||
                !saveJson(root + "/scenario-owner.json", {{"schema", 1}, {"root", root}}).isEmpty()) {
                std::fputs("Integration root must not already exist and must be writable.\n", stderr); return 2;
            }
        } else {
            const QString expected = root + "/config";
            if (!QFileInfo(root + "/scenario-owner.json").isFile() ||
                qEnvironmentVariable("XDG_CONFIG_HOME") != expected ||
                qEnvironmentVariable("XDG_DATA_HOME") != root + "/data" ||
                qEnvironmentVariable("HOME") != root + "/home") {
                std::fputs("Integration restart requires the originating isolated environment and marker.\n", stderr); return 2;
            }
        }
        for (const auto &directory : {"home", "config", "data", "cache"})
            if (!QDir().mkpath(root + "/" + directory)) { std::fputs("Cannot create isolated XDG roots.\n", stderr); return 2; }
        qputenv("HOME", (root + "/home").toUtf8());
        qputenv("XDG_CONFIG_HOME", (root + "/config").toUtf8());
        qputenv("XDG_DATA_HOME", (root + "/data").toUtf8());
        qputenv("XDG_CACHE_HOME", (root + "/cache").toUtf8());
        if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
            std::fputs("Real shown-widget integration needs a display. Run under xvfb-run; offscreen smoke is not this scenario.\n", stderr); return 2;
        }
        if (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen") {
            std::fputs("Integration refuses offscreen: use a private Xvfb display and QT_QPA_PLATFORM=xcb.\n", stderr); return 2;
        }
    }
    QTemporaryDir temporary;
    if (smoke) {
        if (!temporary.isValid()) { std::fputs("Cannot create isolated smoke directory\n", stderr); return 1; }
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("XDG_CONFIG_HOME", (temporary.path() + "/config").toUtf8());
        qputenv("XDG_DATA_HOME", (temporary.path() + "/data").toUtf8());
    }
    if (diagnostics && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") &&
        qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    application.setOrganizationName("CitroenC4Robot");
    application.setApplicationName("c4-launcher");
    if (diagnostics) {
        const auto bytes = QJsonDocument(probeHardware()).toJson();
        std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout); return 0;
    }
    Launcher launcher(smoke || integrationMode || restartMode);
    if (integrationMode) launcher.setProperty("integrationRoot", integration.root);
    launcher.show();
    if (integrationMode) QTimer::singleShot(100, &launcher, [&] { startIntegration(launcher, integration); });
    else if (restartMode) QTimer::singleShot(100, &launcher, [&] {
        launcher.restartProof(restartRoot);
    });
    else if (smoke) QTimer::singleShot(50, &launcher, [&] {
        const bool success = launcher.smoke(screenshot);
        std::puts(success ? "launcher smoke passed: real offscreen widgets/config/localization; no live-desktop claim" : "launcher smoke failed");
        application.exit(success ? 0 : 1);
    });
    return application.exec();
}
