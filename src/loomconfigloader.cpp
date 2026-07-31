#include "loomconfigloader.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QUrl>

#include "style/loomiconprovider.h"
#include "style/loomstylecompiler.h"
#include "tokens/loomtokenregistry.h"

Q_STATIC_LOGGING_CATEGORY(lcLoomConfig, "loom.config")

namespace {

void loadColors(LoomTokenRegistry *registry, const QJsonObject &colors)
{
    for (auto it = colors.constBegin(); it != colors.constEnd(); ++it) {
        if (it->isObject()) {
            // Nested hue: {"brand": {"500": "#7c5cff"}} -> "brand-500".
            const QJsonObject shades = it->toObject();
            for (auto shade = shades.constBegin(); shade != shades.constEnd(); ++shade) {
                const QColor color = QColor::fromString(shade->toString());
                if (!color.isValid()) {
                    qCWarning(lcLoomConfig).noquote()
                        << "config: invalid color for" << it.key() << "/" << shade.key()
                        << ":" << shade->toVariant().toString();
                    continue;
                }
                registry->addColor(it.key() + QLatin1Char('-') + shade.key(), color);
            }
        } else {
            const QColor color = QColor::fromString(it->toString());
            if (!color.isValid()) {
                qCWarning(lcLoomConfig).noquote()
                    << "config: invalid color for" << it.key() << ":"
                    << it->toVariant().toString();
                continue;
            }
            registry->addColor(it.key(), color);
        }
    }
}

void loadSpace(LoomTokenRegistry *registry, const QJsonObject &space)
{
    for (auto it = space.constBegin(); it != space.constEnd(); ++it) {
        if (!it->isDouble() || it->toDouble() < 0) {
            qCWarning(lcLoomConfig).noquote()
                << "config: space entry" << it.key()
                << "must be a non-negative number of pixels";
            continue;
        }
        registry->addSpace(it.key(), it->toDouble());
    }
}

void loadBreakpoints(LoomTokenRegistry *registry, const QJsonObject &breakpoints)
{
    for (auto it = breakpoints.constBegin(); it != breakpoints.constEnd(); ++it) {
        if (!it->isDouble() || !registry->setBreakpoint(it.key(), it->toInt())) {
            qCWarning(lcLoomConfig).noquote()
                << "config: breakpoints only accepts numeric sm/md/lg/xl, not"
                << it.key();
        }
    }
}

void loadThemes(LoomTokenRegistry *registry, const QJsonObject &themes)
{
    // Themes may extend each other in any declaration order; keep retrying
    // until a pass makes no progress, then whatever is left names an unknown
    // (or cyclic) base.
    QList<QString> pending = themes.keys();
    bool progress = true;
    while (progress && !pending.isEmpty()) {
        progress = false;
        for (qsizetype i = 0; i < pending.size();) {
            const QString name = pending.at(i);
            const QJsonObject theme = themes.value(name).toObject();
            const QString base = theme.value(QLatin1String("extends")).toString();

            QHash<QString, QString> refs;
            std::optional<bool> dark;
            for (auto it = theme.constBegin(); it != theme.constEnd(); ++it) {
                if (it.key() == QLatin1String("extends"))
                    continue;
                if (it.key() == QLatin1String("dark")) {
                    dark = it->toBool();
                    continue;
                }
                refs.insert(it.key(), it->toString());
            }

            if (registry->defineTheme(name, base, refs, dark)) {
                pending.removeAt(i);
                progress = true;
            } else {
                ++i;
            }
        }
    }
    for (const QString &name : pending)
        qCWarning(lcLoomConfig).noquote()
            << "config: theme" << name << "extends an unknown theme"
            << themes.value(name).toObject().value(QLatin1String("extends")).toString();
}

} // namespace

bool loomLoadConfigFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcLoomConfig).noquote()
            << "config: cannot open" << filePath << ":" << file.errorString();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject()) {
        qCWarning(lcLoomConfig).noquote()
            << "config:" << filePath
            << "is not a JSON object:" << parseError.errorString() << "at offset"
            << parseError.offset;
        return false;
    }

    const QJsonObject root = document.object();
    static const QStringList knownKeys = {
        QStringLiteral("colors"),       QStringLiteral("space"),
        QStringLiteral("breakpoints"),  QStringLiteral("themes"),
        QStringLiteral("defaultTheme"),  QStringLiteral("iconRoot"),
    };
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (!knownKeys.contains(it.key()))
            qCWarning(lcLoomConfig).noquote()
                << "config: unknown key" << it.key() << "- expected one of"
                << knownKeys.join(QLatin1String(", "));
    }

    auto *registry = LoomTokenRegistry::instance();
    // Colors first: theme entries may reference config-defined palette keys.
    loadColors(registry, root.value(QLatin1String("colors")).toObject());
    loadSpace(registry, root.value(QLatin1String("space")).toObject());
    loadBreakpoints(registry, root.value(QLatin1String("breakpoints")).toObject());
    loadThemes(registry, root.value(QLatin1String("themes")).toObject());

    // A compiled "bg-brand-500" from before this config knew brand-500 is
    // cached as unknown; drop the cache so the vocabulary change is visible.
    LoomStyleCompiler::clearCache();
    registry->announceConfigChange();

    const QString iconRoot = root.value(QLatin1String("iconRoot")).toString();
    if (!iconRoot.isEmpty()) {
        // Relative to the config file, which is the only location a JSON
        // document can meaningfully be relative to. A config read out of the
        // resource system has to yield a qrc: base -- QUrl::fromLocalFile
        // spells ":/x" as the malformed "file::/x", which resolves to a path
        // nothing can open.
        const QString directory = QFileInfo(filePath).absolutePath();
        const QUrl base = directory.startsWith(QLatin1Char(':'))
            ? QUrl(QLatin1String("qrc") + directory + QLatin1Char('/'))
            : QUrl::fromLocalFile(directory + QLatin1Char('/'));
        setLoomIconRoot(base.resolved(QUrl(iconRoot)));
    }

    const QString defaultTheme = root.value(QLatin1String("defaultTheme")).toString();
    if (!defaultTheme.isEmpty())
        registry->setTheme(defaultTheme);
    return true;
}
