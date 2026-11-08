#pragma once

#include "c4_runtime_policy_data.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <algorithm>
#include <stdexcept>

namespace c4_update {
namespace runtime_policy_detail {

inline void requirePolicy(bool valid) {
    if (!valid) throw std::runtime_error("Invalid embedded runtime policy");
}

inline bool asciiLetter(QChar ch) {
    return (ch >= u'A' && ch <= u'Z') || (ch >= u'a' && ch <= u'z');
}

inline bool asciiDigit(QChar ch) {
    return ch >= u'0' && ch <= u'9';
}

inline bool safeComponent(QStringView value, qsizetype maximum) {
    if (value.isEmpty() || value.size() > maximum ||
        (!asciiLetter(value.front()) && !asciiDigit(value.front()))) return false;
    return std::all_of(value.begin(), value.end(), [](QChar ch) {
        return asciiLetter(ch) || asciiDigit(ch) || ch == u'+' || ch == u'.' || ch == u'_' || ch == u'-';
    });
}

inline bool moduleName(QStringView value) {
    if (value.isEmpty() || value.size() > 96 || (!asciiLetter(value.front()) && value.front() != u'_')) return false;
    return std::all_of(value.begin(), value.end(), [](QChar ch) {
        return asciiLetter(ch) || asciiDigit(ch) || ch == u'_';
    });
}

inline bool pluginPath(QStringView path) {
    if (!path.startsWith(u"plugins/") || path.size() > 208) return false;
    const auto slash = path.indexOf(u'/', 8);
    if (slash < 0 || !safeComponent(path.sliced(8, slash - 8), 96)) return false;
    const auto name = path.sliced(slash + 1);
    return name.size() <= 96 && name.startsWith(u"lib") && name.endsWith(u".so") &&
           safeComponent(name.sliced(3, name.size() - 6), 90);
}

inline QStringList stringArray(const QJsonObject &object, const char *key) {
    const auto value = object.value(QLatin1StringView(key));
    requirePolicy(value.isArray());
    const auto array = value.toArray();
    QStringList strings;
    strings.reserve(array.size());
    for (const auto &entry : array) {
        requirePolicy(entry.isString());
        strings.append(entry.toString());
    }
    std::sort(strings.begin(), strings.end());
    requirePolicy(std::adjacent_find(strings.begin(), strings.end()) == strings.end());
    return strings;
}

inline bool contains(const QStringList &values, QStringView value) {
    const auto found = std::lower_bound(values.begin(), values.end(), value,
        [](const QString &entry, QStringView sought) { return QStringView(entry).compare(sought) < 0; });
    return found != values.end() && QStringView(*found) == value;
}

struct Policy {
    QStringList libraryBases;
    QStringList licenseNames;
    QSet<QString> exactPaths;
};

inline Policy parsePolicy() {
    requirePolicy(sizeof(C4_RUNTIME_POLICY_JSON) > 1 && sizeof(C4_RUNTIME_POLICY_JSON) <= 1024 * 1024);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(QByteArray::fromRawData(
        C4_RUNTIME_POLICY_JSON, static_cast<qsizetype>(sizeof(C4_RUNTIME_POLICY_JSON) - 1)), &error);
    requirePolicy(error.error == QJsonParseError::NoError && document.isObject());
    const auto object = document.object();
    const auto schema = object.value(QStringLiteral("schema_version"));
    requirePolicy(schema.isDouble() && schema.toDouble() == 1.0);

    Policy policy;
    policy.libraryBases = stringArray(object, "library_bases");
    for (const auto &base : policy.libraryBases) {
        requirePolicy(safeComponent(base, 90) && !base.startsWith(QLatin1StringView("lib")) &&
                      !base.contains(QLatin1StringView(".so")));
    }
    policy.licenseNames = stringArray(object, "license_names");
    for (const auto &name : policy.licenseNames) requirePolicy(safeComponent(name, 96));
    const auto plugins = stringArray(object, "plugins");
    for (const auto &plugin : plugins) {
        requirePolicy(pluginPath(plugin));
        policy.exactPaths.insert(plugin);
    }
    const auto modules = stringArray(object, "python_modules");
    for (const auto &module : modules) requirePolicy(moduleName(module));

    const auto minors = object.value(QStringLiteral("python_minors"));
    requirePolicy(minors.isArray());
    unsigned seenMinors = 0;
    for (const auto &value : minors.toArray()) {
        requirePolicy(value.isDouble());
        const int minor = value.toInt(-1);
        requirePolicy(minor >= 11 && minor <= 14 && value.toDouble() == minor);
        const unsigned bit = 1u << (minor - 11);
        requirePolicy((seenMinors & bit) == 0);
        seenMinors |= bit;
        const auto version = QString::number(minor);
        policy.exactPaths.insert(QStringLiteral("runtime/lib/python3") + version + QStringLiteral(".zip"));
        const auto prefix = QStringLiteral("runtime/lib/python3.") + version + QStringLiteral("/lib-dynload/");
        for (const auto &module : modules) {
            policy.exactPaths.insert(prefix + module + QStringLiteral(".so"));
            policy.exactPaths.insert(prefix + module + QStringLiteral(".cpython-3") + version +
                                     QStringLiteral("-x86_64-linux-gnu.so"));
        }
    }
    policy.exactPaths.insert(QStringLiteral("tools/innoextract"));
    policy.exactPaths.insert(QStringLiteral("runtime/bin/python3"));
    policy.exactPaths.insert(QStringLiteral("bin/qt.conf"));
    policy.exactPaths.insert(QStringLiteral("share/citroen-c4-robot/runtime-manifest.json"));
    return policy;
}

inline bool libraryPath(QStringView path, const QStringList &bases) {
    if (!path.startsWith(u"lib/lib")) return false;
    const auto name = path.sliced(4);
    if (name.size() > 96) return false;
    const auto suffix = name.indexOf(u".so", 3);
    if (suffix < 0 || !contains(bases, name.sliced(3, suffix - 3))) return false;
    auto index = suffix + 3;
    unsigned components = 0;
    while (index < name.size()) {
        if (name[index++] != u'.' || ++components > 4) return false;
        const auto start = index;
        while (index < name.size() && asciiDigit(name[index])) ++index;
        if (index - start < 1 || index - start > 4) return false;
    }
    return true;
}

inline bool licensePath(QStringView path, const QStringList &names) {
    constexpr QStringView prefix = u"share/licenses/citroen-c4-robot/";
    if (!path.startsWith(prefix)) return false;
    const auto slash = path.indexOf(u'/', prefix.size());
    if (slash < 0) return false;
    const auto package = path.sliced(prefix.size(), slash - prefix.size());
    const auto lowerAlnum = [](QChar ch) { return (ch >= u'a' && ch <= u'z') || asciiDigit(ch); };
    return !package.isEmpty() && package.size() <= 96 && lowerAlnum(package.front()) &&
           !package.contains(u"..") &&
           std::all_of(package.begin(), package.end(), [&](QChar ch) {
               return lowerAlnum(ch) || ch == u'+' || ch == u'.' || ch == u'-';
           }) && contains(names, path.sliced(slash + 1));
}

} // namespace runtime_policy_detail

// The caller validates archive path safety and authenticates the exact file inventory.
// This policy authorizes names only; license files must remain nonexecutable.
inline bool authorizedRuntimePath(const QString &path) {
    static const auto policy = runtime_policy_detail::parsePolicy();
    if (path.isEmpty() || path.size() > 512) return false;
    return policy.exactPaths.contains(path) ||
           runtime_policy_detail::libraryPath(path, policy.libraryBases) ||
           runtime_policy_detail::licensePath(path, policy.licenseNames);
}

} // namespace c4_update
