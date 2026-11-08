#pragma once

#include <QString>

class QMainWindow;

struct IntegrationOptions {
    QString root, installer, directory, firstRelease, secondRelease, key;
};

void startIntegration(QMainWindow &window, const IntegrationOptions &options);
