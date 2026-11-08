#include "quality_controls.hpp"
#include "../../src/rendering/quality/GraphicSettings.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonArray>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QWidget>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace {
using namespace c4::rendering::quality;
constexpr const char* presetNames[]{"original", "enhanced", "ultra", "custom"};

std::optional<ProfileDefaults> profileDefaults(Preset preset) {
    switch (preset) {
    case Preset::Original: return ProfileDefaults{RendererBackend::OpenGL, 0};
    case Preset::Enhanced: return ProfileDefaults{RendererBackend::Vulkan, 0};
    case Preset::Ultra: return ProfileDefaults{RendererBackend::Vulkan, 7};
    case Preset::Custom: return std::nullopt;
    }
    return std::nullopt;
}

bool supportsFullQuality(const QJsonObject& caps, Preset preset) {
    const bool ultra = preset == Preset::Ultra;
    return caps.value("sample_counts").toArray().contains(ultra ? 8 : 4) &&
        caps.value("fxaa").toBool() && caps.value("ssao").toBool() &&
        caps.value("pbr").toBool() && caps.value("internal_hdr").toBool() &&
        caps.value("bloom").toBool() && caps.value("trilinear").toBool() &&
        caps.value("max_anisotropy").toDouble(1.) >= (ultra ? 8. : 4.) &&
        caps.value("max_shadow_cascades").toInt() >= (ultra ? 4 : 2) &&
        caps.value("max_shadow_size").toInt() >= (ultra ? 2048 : 1024);
}

bool hasOverrides(const GraphicSettings& state) {
    return state.backend_override || state.texture_filtering || state.denoising || state.samples ||
        state.post_aa || state.ssao || state.shadows || state.pbr || state.internal_hdr ||
        state.bloom || state.exposure_ev || state.tone_mapping;
}

QLabel* description(const QString& value, const QString& name) {
    auto* label = new QLabel(value);
    label->setObjectName(name);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

void addRow(QFormLayout& form, const QString& title, QWidget* control, QLabel* note) {
    auto* row = new QWidget;
    auto* layout = new QVBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(control);
    layout->addWidget(note);
    auto* label = new QLabel(title);
    label->setBuddy(control);
    control->setAccessibleName(title);
    form.addRow(label, row);
}

void enableItem(QComboBox* combo, int index, bool enabled, const QString& reason) {
    auto* item = static_cast<QStandardItemModel*>(combo->model())->item(index);
    item->setEnabled(enabled);
    item->setToolTip(enabled ? QString{} : reason);
}
}

QString QualityControls::text(const char* english, const char* turkish) const {
    return QString::fromUtf8(turkish_ ? turkish : english);
}

QualityControls::QualityControls(QFormLayout& form, const QJsonObject& saved, bool turkish,
                                 std::function<void()> refreshRequested)
    : turkish_(turkish) {
    const auto quality = saved.value("graphics").toObject();
    renderer_ = new QComboBox;
    renderer_->setObjectName("qualityRenderer");
    renderer_->addItem("OpenGL", "opengl");
    renderer_->addItem("Vulkan", "vulkan");
    renderer_->setCurrentIndex(saved.value("renderer").toString() == "vulkan" ? 1 : 0);
    capabilities_ = description({}, "qualityCapabilities");
    addRow(form, text("Renderer", "İşleyici"), renderer_, capabilities_);

    preset_ = new QComboBox;
    preset_->setObjectName("qualityPreset");
    preset_->addItem(text("Original", "Orijinal"), static_cast<int>(Preset::Original));
    preset_->addItem(text("Enhanced", "Geliştirilmiş"), static_cast<int>(Preset::Enhanced));
    preset_->addItem("Ultra", static_cast<int>(Preset::Ultra));
    preset_->addItem(text("Custom", "Özel"), static_cast<int>(Preset::Custom));
    const auto storedPreset = quality.value("preset").toString("original");
    for (int i = 0; i != 4; ++i)
        if (storedPreset == presetNames[i]) preset_->setCurrentIndex(i);
    const auto defaults = profileDefaults(static_cast<Preset>(preset_->currentData().toInt()));
    if (!saved.contains("renderer") && defaults)
        renderer_->setCurrentIndex(defaults->backend == RendererBackend::Vulkan ? 1 : 0);
    effective_ = description({}, "qualityEffectivePreset");
    addRow(form, text("Requested quality preset", "İstenen kalite önayarı"), preset_, effective_);
    form.addRow(description(text(
        "Original uses OpenGL with RT off and original effects. Enhanced uses Vulkan raster with anisotropic filtering, 4× MSAA, FXAA, SSAO, low shadows, PBR, internal HDR and bloom. Ultra uses Vulkan with all three RT requests, high denoising, 8× MSAA, denser SSAO and high shadows. Custom preserves the renderer and RT requests. Full presets require initialized native capabilities; native rendering reports actual fallbacks.",
        "Orijinal, OpenGL ve orijinal efektleri kullanır; ışın izleme kapalıdır. Geliştirilmiş; Vulkan raster, anizotropik süzme, 4× MSAA, FXAA, SSAO, düşük gölgeler, PBR, dahili HDR ve parlama kullanır. Ultra; üç ışın izleme isteği, yüksek gürültü giderme, 8× MSAA, daha yoğun SSAO ve yüksek gölgelerle Vulkan kullanır. Özel, işleyiciyi ve ışın izleme isteklerini korur. Tam önayarlar, başlatılmış yerel yetenekler gerektirir; yerel işleyici gerçek geri dönüşleri bildirir."), "qualityPresetDescription"));

    auto addOverride = [&](int index, const char* key, const QString& title,
                           const QString& explanation,
                           std::initializer_list<std::pair<QString, QVariant>> choices) {
        auto* combo = new QComboBox;
        combo->setObjectName(QString("quality_") + key);
        combo->addItem(text("Preset / inherit", "Önayar / devral"));
        for (const auto& choice : choices) combo->addItem(choice.first, choice.second);
        if (quality.contains(key)) {
            const int selected = combo->findData(quality.value(key).toVariant());
            if (selected >= 1) combo->setCurrentIndex(selected);
        }
        auto* note = description(explanation, combo->objectName() + "Description");
        overrides_[index] = {key, combo, note, explanation};
        addRow(form, title, combo, note);
    };
    const auto off = text("Off", "Kapalı"), on = text("On", "Açık");
    addOverride(0, "samples", "MSAA", text("Multisample geometry antialiasing; only actual supported sample counts are selectable.", "Çok örnekli geometri kenar yumuşatma; yalnızca gerçekten desteklenen örnek sayıları seçilebilir."),
                {{off, 1}, {"2×", 2}, {"4×", 4}, {"8×", 8}});
    addOverride(1, "post_aa", text("Post-process antialiasing", "Son işlem kenar yumuşatma"), text("FXAA smooths the rendered image. No TAA or upscaling is provided.", "FXAA, işlenmiş görüntüyü yumuşatır. TAA veya görüntü yükseltme sunulmaz."),
                {{off, "off"}, {"FXAA", "fxaa"}});
    addOverride(2, "ssao", "SSAO", text("Screen-space ambient occlusion adds contact shading.", "Ekran uzayı çevresel perdeleme, temas gölgelendirmesi ekler."), {{off, false}, {on, true}});
    addOverride(3, "shadows", text("Raster shadows", "Raster gölgeler"), text("Cascaded raster shadow maps; independent of ray-traced shadows.", "Kademeli raster gölge haritaları; ışın izlemeli gölgelerden bağımsızdır."),
                {{off, "off"}, {text("Low", "Düşük"), "low"}, {text("High", "Yüksek"), "high"}});
    addOverride(4, "pbr", "PBR", text("Physically based material shading.", "Fiziksel tabanlı malzeme gölgelendirmesi."), {{off, false}, {on, true}});
    addOverride(5, "internal_hdr", text("Internal HDR", "Dahili HDR"), text("Floating-point internal lighting, tone-mapped to SDR. This is not HDR10 display output.", "Kayan noktalı dahili aydınlatma, SDR'ye ton eşlenir. Bu, HDR10 ekran çıkışı değildir."), {{off, false}, {on, true}});
    addOverride(6, "bloom", text("Bloom", "Parlama"), text("Bright-light glow; requests internal HDR unless it is explicitly Off.", "Parlak ışık halesi; dahili HDR açıkça Kapalı değilse onu ister."), {{off, false}, {on, true}});
    addOverride(7, "tone_mapping", text("Tone mapping", "Ton eşleme"), text("Reinhard or ACES maps internal HDR to SDR. An override requests internal HDR unless it is explicitly Off.", "Reinhard veya ACES, dahili HDR'yi SDR'ye eşler. Bir geçersiz kılma, dahili HDR açıkça Kapalı değilse onu ister."),
                {{"Reinhard", "reinhard"}, {"ACES", "aces"}});

    auto* upscaling = new QComboBox;
    upscaling->setObjectName("quality_upscaling");
    upscaling->addItem(text("Unavailable", "Kullanılamıyor"));
    upscaling->setEnabled(false);
    const auto upscalingReason = text(
        "Unavailable: no native upscaling pipeline is implemented. MSAA and FXAA are antialiasing, not upscaling; no upscaling setting or argument is emitted.",
        "Kullanılamıyor: yerel görüntü yükseltme hattı uygulanmadı. MSAA ve FXAA kenar yumuşatmadır, görüntü yükseltme değildir; yükseltme ayarı veya argümanı üretilmez.");
    upscaling->setToolTip(upscalingReason);
    addRow(form, text("Upscaling", "Görüntü yükseltme"), upscaling,
           description(upscalingReason, "qualityUpscalingDescription"));

    addOverride(8, "texture_filtering", text("Texture filtering", "Doku süzme"), text(
        "Original, trilinear or anisotropic material sampling. Only filtering supported by initialized native samplers is selectable.",
        "Orijinal, üç doğrusal veya anizotropik malzeme örnekleme. Yalnızca başlatılmış yerel örnekleyicilerin desteklediği süzme seçilebilir."),
        {{text("Original", "Orijinal"), "original"}, {text("Trilinear", "Üç doğrusal"), "trilinear"},
         {text("Anisotropic", "Anizotropik"), "anisotropic"}});
    addOverride(9, "denoising", text("RT denoising", "Işın izleme gürültü giderme"), text(
        "Off keeps raw RT; Standard uses temporal denoising, and High adds three spatial filtering passes with motion rejection. Requires a Vulkan RT request and initialized native denoising support.",
        "Kapalı, ham ışın izlemeyi korur; Standart, zamansal gürültü giderme kullanır; Yüksek, hareket reddiyle üç uzamsal süzme geçişi ekler. Bir Vulkan ışın izleme isteği ve başlatılmış yerel gürültü giderme desteği gerektirir."),
        {{off, "off"}, {text("Standard", "Standart"), "standard"}, {text("High", "Yüksek"), "high"}});

    auto* exposureRow = new QWidget;
    auto* exposureLayout = new QHBoxLayout(exposureRow);
    exposureLayout->setContentsMargins(0, 0, 0, 0);
    exposureOverride_ = new QCheckBox(text("Override preset", "Önayarı geçersiz kıl"));
    exposureOverride_->setObjectName("quality_exposure_override");
    exposureOverride_->setAccessibleName(text("Override exposure", "Pozlamayı geçersiz kıl"));
    exposure_ = new QDoubleSpinBox;
    exposure_->setObjectName("quality_exposure_ev");
    exposure_->setAccessibleName(text("Exposure EV", "Pozlama EV"));
    exposure_->setRange(-10., 10.);
    exposure_->setDecimals(9);
    exposure_->setSingleStep(.25);
    exposure_->setSuffix(" EV");
    const auto storedExposure = quality.value("exposure_ev");
    if (storedExposure.isDouble() && std::isfinite(storedExposure.toDouble()) &&
        storedExposure.toDouble() >= -10. && storedExposure.toDouble() <= 10.) {
        exposureOverride_->setChecked(true);
        exposure_->setValue(storedExposure.toDouble());
    }
    exposureLayout->addWidget(exposureOverride_);
    exposureLayout->addWidget(exposure_);
    exposureDescription_ = description({}, "qualityExposureDescription");
    addRow(form, text("Exposure", "Pozlama"), exposureRow, exposureDescription_);

    auto* rtRow = new QWidget;
    auto* rtLayout = new QHBoxLayout(rtRow);
    rtLayout->setContentsMargins(0, 0, 0, 0);
    const char* rtNames[]{"rt_reflections", "rt_shadows", "rt_gi"};
    const QString rtTitles[]{text("Reflections", "Yansımalar"), text("Shadows", "Gölgeler"), text("Global illumination", "Küresel aydınlatma")};
    const int storedMask = saved.value("rt_effects").toInt(defaults ? static_cast<int>(defaults->rt_effects) : 0);
    const int mask = storedMask >= 0 && storedMask <= 7 ? storedMask : 0;
    for (int i = 0; i != 3; ++i) {
        rt_[i] = new QCheckBox(rtTitles[i]);
        rt_[i]->setObjectName(rtNames[i]);
        rt_[i]->setAccessibleName(text("Ray tracing: ", "Işın izleme: ") + rtTitles[i]);
        rt_[i]->setChecked(mask & (1 << i));
        rtLayout->addWidget(rt_[i]);
    }
    rtDescription_ = description({}, "qualityRTDescription");
    addRow(form, text("Ray tracing (independent)", "Işın izleme (bağımsız)"), rtRow, rtDescription_);

    setCapabilities({});
    updateEffectivePreset();
    connect(preset_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { applyPreset(); });
    connect(preset_, qOverload<int>(&QComboBox::activated), this, [this] {
        if (hasOverrides(settings())) applyPreset();
    });
    for (const auto& entry : overrides_)
        connect(entry.control, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateEffectivePreset(); });
    connect(exposureOverride_, &QCheckBox::toggled, this, [this](bool enabled) {
        exposure_->setEnabled(enabled && exposureOverride_->isEnabled());
        updateEffectivePreset();
    });
    for (auto* control : rt_)
        connect(control, &QCheckBox::toggled, this, [this] {
            if (!applyingPreset_) backendOverride_ = true;
            updateDenoisingAvailability();
            updateEffectivePreset();
        });
    connect(renderer_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, refresh = std::move(refreshRequested)] {
                if (!applyingPreset_) backendOverride_ = true;
                setCapabilities({});
                if (refresh) refresh();
            });
    backendOverride_ = quality.value("backend_override").toBool() ||
        (defaults && (renderer() != (defaults->backend == RendererBackend::Vulkan ? "vulkan" : "opengl") ||
                      static_cast<unsigned>(mask) != defaults->rt_effects));
    updateEffectivePreset();
}

QString QualityControls::renderer() const { return renderer_->currentData().toString(); }

GraphicSettings QualityControls::settings() const {
    GraphicSettings result;
    result.preset = static_cast<Preset>(preset_->currentData().toInt());
    result.backend_override = backendOverride_;
    const auto data = [this](int index) { return overrides_[index].control->currentData(); };
    if (data(0).isValid()) result.samples = data(0).toUInt();
    if (data(1).isValid()) result.post_aa = data(1).toString() == "fxaa" ? PostAA::Fxaa : PostAA::Off;
    if (data(2).isValid()) result.ssao = data(2).toBool();
    if (data(3).isValid()) {
        const auto shadows = data(3).toString();
        result.shadows = shadows == "high" ? ShadowQuality::High : shadows == "low" ? ShadowQuality::Low : ShadowQuality::Off;
    }
    if (data(4).isValid()) result.pbr = data(4).toBool();
    if (data(5).isValid()) result.internal_hdr = data(5).toBool();
    if (data(6).isValid()) result.bloom = data(6).toBool();
    if (data(7).isValid()) result.tone_mapping = data(7).toString() == "aces" ? ToneMapping::Aces : ToneMapping::Reinhard;
    if (data(8).isValid()) {
        const auto filtering = data(8).toString();
        result.texture_filtering = filtering == "anisotropic" ? TextureFiltering::Anisotropic :
            filtering == "trilinear" ? TextureFiltering::Trilinear : TextureFiltering::Original;
    }
    if (data(9).isValid()) {
        const auto denoising = data(9).toString();
        result.denoising = denoising == "high" ? Denoising::High :
            denoising == "standard" ? Denoising::Standard : Denoising::Off;
    }
    if (exposureOverride_->isChecked()) result.exposure_ev = static_cast<float>(exposure_->value());
    return result;
}

QJsonObject QualityControls::save() const {
    const auto state = settings();
    QJsonObject quality{{"preset", presetNames[static_cast<unsigned>(state.preset)]}};
    for (const auto& entry : overrides_) {
        const auto data = entry.control->currentData();
        if (data.isValid()) quality.insert(entry.key, QJsonValue::fromVariant(data));
    }
    if (state.exposure_ev) quality.insert("exposure_ev", exposure_->value());
    if (state.backend_override) quality.insert("backend_override", true);
    int mask = 0;
    for (int i = 0; i != 3; ++i)
        if (rt_[i]->isChecked()) mask |= 1 << i;
    return {{"renderer", renderer()}, {"graphics", quality}, {"rt_effects", mask}};
}

void QualityControls::clearOverrides() {
    for (const auto& entry : overrides_) {
        const QSignalBlocker block(entry.control);
        entry.control->setCurrentIndex(0);
    }
    exposureOverride_->setChecked(false);
    updateEffectivePreset();
}

void QualityControls::applyPreset() {
    applyingPreset_ = true;
    clearOverrides();
    const auto defaults = profileDefaults(static_cast<Preset>(preset_->currentData().toInt()));
    if (defaults) {
        for (int i = 0; i != 3; ++i) rt_[i]->setChecked(defaults->rt_effects & (1u << i));
        renderer_->setCurrentIndex(defaults->backend == RendererBackend::Vulkan ? 1 : 0);
    }
    backendOverride_ = false;
    applyingPreset_ = false;
    updateEffectivePreset();
}

void QualityControls::updateEffectivePreset() {
    const auto state = settings();
    const bool overrides = hasOverrides(state);
    const bool confirmed = state.preset == Preset::Original || state.preset == Preset::Custom ||
        (state.preset == Preset::Enhanced ? enhancedSupported_ : ultraSupported_);
    const auto effective = overrides || state.preset == Preset::Custom ? QString("custom") :
        confirmed ? QString::fromLatin1(presetNames[static_cast<unsigned>(state.preset)]) : QString("unconfirmed");
    effective_->setProperty("requestedPreset", QString::fromLatin1(presetNames[static_cast<unsigned>(state.preset)]));
    effective_->setProperty("effectivePreset", effective);
    effective_->setProperty("capabilityConfirmed", confirmed);
    effective_->setText(overrides
        ? text("Configured preset: Custom. Unspecified quality fields inherit from %1; renderer and RT overrides are retained. Native rendering reports actual fallbacks.",
               "Yapılandırılan önayar: Özel. Belirtilmeyen kalite alanları %1 önayarından devralınır; işleyici ve ışın izleme geçersiz kılmaları korunur. Yerel işleyici gerçek geri dönüşleri bildirir.").arg(preset_->currentText())
        : confirmed
            ? text("Configured preset: %1. Native rendering reports the actual effective profile and any capability fallback.",
                   "Yapılandırılan önayar: %1. Yerel işleyici gerçek etkin profili ve olası yetenek geri dönüşlerini bildirir.").arg(preset_->currentText())
            : text("Requested preset: %1. Full bundle capabilities are unavailable or unconfirmed; native rendering will report the actual fallback.",
                   "İstenen önayar: %1. Tam paket yetenekleri kullanılamıyor veya doğrulanmadı; yerel işleyici gerçek geri dönüşü bildirecek.").arg(preset_->currentText()));
}

void QualityControls::updateDenoisingAvailability() {
    const bool requested = renderer() == "vulkan" &&
        (rt_[0]->isChecked() || rt_[1]->isChecked() || rt_[2]->isChecked());
    const bool standard = requested && temporalDenoiseSupported_;
    const bool high = requested && highDenoiseSupported_;
    const auto& entry = overrides_[9];
    const auto reason = requested
        ? text("Unavailable: the native query has not confirmed this RT denoising pipeline.",
               "Kullanılamıyor: yerel sorgu, bu ışın izleme gürültü giderme hattını doğrulamadı.")
        : text("Unavailable: denoising requires a nonzero Vulkan RT request; it does not denoise raster rendering.",
               "Kullanılamıyor: gürültü giderme, sıfır dışı bir Vulkan ışın izleme isteği gerektirir; raster işleme gürültüsünü gidermez.");
    enableItem(entry.control, 1, standard || high, reason);
    enableItem(entry.control, 2, standard, reason);
    enableItem(entry.control, 3, high, reason);
    entry.control->setEnabled(standard || high || entry.control->currentIndex() != 0);
    entry.control->setToolTip(standard || high ? entry.explanation : reason);
    entry.description->setText(entry.explanation + (standard || high ? QString{} : "\n" + reason));
}

QStringList QualityControls::arguments() const {
    const auto state = settings();
    QStringList args{"--quality", presetNames[static_cast<unsigned>(state.preset)]};
    const auto add = [&args](const char* flag, const QString& value) { args << flag << value; };
    const auto toggle = [](bool enabled) { return enabled ? QString("on") : QString("off"); };
    if (state.samples) add("--msaa", QString::number(*state.samples));
    if (state.post_aa) add("--post-aa", *state.post_aa == PostAA::Fxaa ? "fxaa" : "off");
    if (state.ssao) add("--ssao", toggle(*state.ssao));
    if (state.shadows) add("--shadows", *state.shadows == ShadowQuality::High ? "high" : *state.shadows == ShadowQuality::Low ? "low" : "off");
    if (state.pbr) add("--pbr", toggle(*state.pbr));
    if (state.internal_hdr) add("--internal-hdr", toggle(*state.internal_hdr));
    if (state.bloom) add("--bloom", toggle(*state.bloom));
    if (state.exposure_ev) add("--exposure-ev", QString::number(exposure_->value(), 'g', 12));
    if (state.tone_mapping) add("--tone-map", *state.tone_mapping == ToneMapping::Aces ? "aces" : "reinhard");
    if (state.texture_filtering) {
        const auto filtering = *state.texture_filtering;
        add("--texture-filter", filtering == TextureFiltering::Anisotropic ? "anisotropic" :
            filtering == TextureFiltering::Trilinear ? "trilinear" : "original");
    }
    if (state.denoising) {
        const auto denoising = *state.denoising;
        add("--denoising", denoising == Denoising::High ? "high" :
            denoising == Denoising::Standard ? "standard" : "off");
    }
    if (state.backend_override || state.preset == Preset::Custom) {
        add("--renderer", renderer());
        const char* rtFlags[]{"--rt-reflections", "--rt-shadows", "--rt-gi"};
        for (int i = 0; i != 3; ++i)
            add(rtFlags[i], toggle(renderer() == "vulkan" && rt_[i]->isChecked()));
    }
    return args;
}

void QualityControls::setCapabilities(const QJsonObject& report) {
    const auto valid = [](const QJsonObject& value, const QString& backend) {
        return value.value("schema_version").toInt() == 1 &&
            value.value("renderer").toString() == backend &&
            value.value("capabilities").isObject();
    };
    const auto backends = report.value("backends").toObject();
    const auto vulkan = backends.value("vulkan").toObject();
    const bool vulkanAvailable = valid(report, "vulkan") ||
        (vulkan.value("available").toBool() && valid(vulkan.value("report").toObject(), "vulkan"));
    const auto vulkanReport = valid(report, "vulkan") ? report : vulkan.value("report").toObject();
    const auto vulkanCaps = vulkanAvailable ? vulkanReport.value("capabilities").toObject() : QJsonObject{};
    const auto vulkanRT = vulkanAvailable ? vulkanReport.value("ray_tracing").toObject() : QJsonObject{};
    enhancedSupported_ = vulkanAvailable && supportsFullQuality(vulkanCaps, Preset::Enhanced);
    ultraSupported_ = vulkanAvailable && supportsFullQuality(vulkanCaps, Preset::Ultra) &&
        vulkanRT.value("pipelines_ready").toBool() && vulkanRT.value("supported_mask").toInt() == 7 &&
        vulkanCaps.value("temporal_denoise").toBool() && vulkanCaps.value("high_quality_denoise").toBool();
    auto rendererReason = text("Vulkan is unavailable until its native backend initializes successfully.",
                               "Vulkan, yerel işleyicisi başarıyla başlatılana kadar kullanılamaz.");
    const auto vulkanError = vulkan.value("error").toString();
    if (!vulkanError.isEmpty()) rendererReason += "\n" + text("Native error: ", "Yerel hata: ") + vulkanError;
    enableItem(renderer_, 0, true, {});
    enableItem(renderer_, 1, vulkanAvailable, rendererReason);
    const bool initialized = valid(report, renderer());
    const auto caps = initialized ? report.value("capabilities").toObject() : QJsonObject{};
    capabilities_->setProperty("nativeCapabilities", report);
    const auto failure = backends.value(renderer()).toObject().value("error").toString();
    const auto unavailable = initialized
        ? text("Unavailable: the initialized backend does not support this feature.",
               "Kullanılamıyor: başlatılmış işleyici bu özelliği desteklemiyor.")
        : text("Unavailable: awaiting a successful native capability query.",
               "Kullanılamıyor: başarılı bir yerel yetenek sorgusu bekleniyor.");
    capabilities_->setText(initialized
        ? text("Capabilities below come from the initialized native %1 renderer, not an extension-only hardware probe.",
               "Aşağıdaki yetenekler, yalnızca donanım uzantı sorgusundan değil, başlatılmış yerel %1 işleyicisinden gelir.").arg(renderer_->currentText())
        : unavailable + (failure.isEmpty() ? QString{} : "\n" + text("Native error: ", "Yerel hata: ") + failure));

    unsigned sampleMask = 0;
    for (const auto& value : caps.value("sample_counts").toArray()) {
        const int count = value.toInt();
        if (count == 1 || count == 2 || count == 4 || count == 8) sampleMask |= static_cast<unsigned>(count);
    }
    const bool hdr = caps.value("internal_hdr").toBool();
    const bool lowShadows = caps.value("max_shadow_cascades").toInt() >= 1 && caps.value("max_shadow_size").toInt() >= 256;
    const bool highShadows = caps.value("max_shadow_cascades").toInt() >= 4 && caps.value("max_shadow_size").toInt() >= 2048;
    const double maxAnisotropy = caps.value("max_anisotropy").toDouble(1.);
    const bool trilinear = caps.value("trilinear").toBool();
    const bool anisotropic = trilinear && std::isfinite(maxAnisotropy) && maxAnisotropy > 1.;
    temporalDenoiseSupported_ = initialized && caps.value("temporal_denoise").toBool();
    highDenoiseSupported_ = initialized && caps.value("high_quality_denoise").toBool();
    const std::array<bool, 9> supported{{
        bool(sampleMask & (2u | 4u | 8u)), caps.value("fxaa").toBool(),
        caps.value("ssao").toBool(), lowShadows, caps.value("pbr").toBool(),
        hdr, hdr && caps.value("bloom").toBool(), hdr, trilinear || anisotropic
    }};
    bool enhancement = false;
    for (int i = 0; i != static_cast<int>(overrides_.size()); ++i) {
        if (i == 9) continue;
        const auto& entry = overrides_[i];
        enhancement = enhancement || supported[i];
        for (int item = 1; item != entry.control->count(); ++item) {
            bool enabled = supported[i];
            if (i == 0) enabled = supported[i] && (sampleMask & entry.control->itemData(item).toUInt());
            if (i == 3 && item == 3) enabled = highShadows;
            if (i == 8 && item == 2) enabled = trilinear;
            if (i == 8 && item == 3) enabled = anisotropic;
            enableItem(entry.control, item, enabled, unavailable);
        }
        entry.control->setEnabled(supported[i] || entry.control->currentIndex() != 0);
        entry.control->setToolTip(supported[i] ? entry.explanation : unavailable);
        entry.description->setText(entry.explanation + (supported[i] ? QString{} : "\n" + unavailable));
    }
    enableItem(preset_, 0, true, {});
    enableItem(preset_, 1, enhancedSupported_, text(
        "Unavailable: Enhanced requires the full initialized Vulkan raster feature set, including 4× MSAA and 4× anisotropic filtering.",
        "Kullanılamıyor: Geliştirilmiş, 4× MSAA ve 4× anizotropik süzme dahil tam başlatılmış Vulkan raster özelliklerini gerektirir."));
    enableItem(preset_, 2, ultraSupported_, text(
        "Unavailable: Ultra requires the full Vulkan quality feature set, high denoising and initialized support for all three RT effects.",
        "Kullanılamıyor: Ultra, tam Vulkan kalite özelliklerini, yüksek gürültü gidermeyi ve üç ışın izleme efektinin tümü için başlatılmış desteği gerektirir."));
    exposureOverride_->setEnabled(hdr);
    exposureOverride_->setToolTip(hdr ? QString{} : unavailable);
    exposure_->setEnabled(hdr && exposureOverride_->isChecked());
    exposureDescription_->setText(text(
        "Exposure compensation from −10 to +10 EV. A nonzero override requests internal HDR unless it is explicitly Off.",
        "−10 ile +10 EV arasında pozlama telafisi. Sıfır dışı bir geçersiz kılma, dahili HDR açıkça Kapalı değilse onu ister.") +
        (hdr ? QString{} : "\n" + unavailable));
    const auto rt = initialized ? report.value("ray_tracing").toObject() : QJsonObject{};
    const int supportedMask = rt.value("supported_mask").toInt(0);
    const int rtMask = renderer() == "vulkan" && rt.value("pipelines_ready").toBool() &&
        supportedMask >= 0 && supportedMask <= 7 ? supportedMask : 0;
    enableItem(preset_, 3, enhancement || rtMask != 0, unavailable);
    const auto rtUnavailable = text(
        "Unavailable: the native query has not confirmed an initialized RT pipeline for this effect.",
        "Kullanılamıyor: yerel sorgu, bu efekt için başlatılmış bir ışın izleme hattını doğrulamadı.");
    const auto rtFailure = rt.value("fallback_reason").toString();
    const auto rtReason = rtUnavailable + (rtFailure.isEmpty() ? QString{} : "\n" +
        text("Native reason: ", "Yerel neden: ") + rtFailure);
    for (int i = 0; i != 3; ++i) {
        const bool available = rtMask & (1 << i);
        rt_[i]->setEnabled(available);
        rt_[i]->setToolTip(available ? QString{} : rtReason);
    }
    rtDescription_->setText(text(
        "Presets set the default RT requests; each effect can also be overridden independently. Only native-query-confirmed initialized RT effects are selectable. Backend changes retain requests; OpenGL launches with RT off.",
        "Önayarlar, varsayılan ışın izleme isteklerini belirler; her efekt bağımsız olarak da geçersiz kılınabilir. Yalnızca yerel sorguyla başlatıldığı doğrulanan ışın izleme efektleri seçilebilir. İşleyici değişiklikleri istekleri korur; OpenGL, ışın izleme kapalı olarak başlar.") +
        (rtMask == 7 ? QString{} : "\n" + rtReason));
    updateDenoisingAvailability();
    updateEffectivePreset();
}
