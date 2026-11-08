#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <array>
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;

namespace c4::rendering::quality { struct GraphicSettings; }

class QualityControls final : public QObject {
public:
    QualityControls(QFormLayout&, const QJsonObject& saved, bool turkish,
                    std::function<void()> refreshRequested);
    QJsonObject save() const;
    QStringList arguments() const;
    void setCapabilities(const QJsonObject&);
    QString renderer() const;

private:
    struct Override {
        const char* key;
        QComboBox* control;
        QLabel* description;
        QString explanation;
    };
    QString text(const char* english, const char* turkish) const;
    void updateEffectivePreset();
    c4::rendering::quality::GraphicSettings settings() const;
    void clearOverrides();
    void applyPreset();
    void updateDenoisingAvailability();

    bool turkish_;
    bool enhancedSupported_{}, ultraSupported_{};
    bool backendOverride_{}, applyingPreset_{};
    bool temporalDenoiseSupported_{}, highDenoiseSupported_{};
    QComboBox* renderer_;
    QComboBox* preset_;
    QLabel* effective_;
    QLabel* capabilities_;
    std::array<Override, 10> overrides_;
    QCheckBox* exposureOverride_;
    QDoubleSpinBox* exposure_;
    QLabel* exposureDescription_;
    std::array<QCheckBox*, 3> rt_;
    QLabel* rtDescription_;
};
