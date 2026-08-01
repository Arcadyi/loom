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
                << "config: breakpoints only accepts numeric sm/md/lg/xl above"
                << "zero, not" << it.key();
        }
    }

    // The tiers are min-width and cumulative, so a threshold below the one
    // before it can never be the widest match: its classes would be shadowed by
    // the narrower tier and appear to do nothing. Cheap to detect, and
    // impossible to diagnose from the styling alone.
    static const char *const names[] = {"sm", "md", "lg", "xl"};
    for (int i = 1; i < 4; ++i) {
        const int previous = registry->breakpoint(QLatin1String(names[i - 1]));
        const int current = registry->breakpoint(QLatin1String(names[i]));
        if (current <= previous) {
            qCWarning(lcLoomConfig).noquote()
                << "config: breakpoint" << names[i] << "(" << current
                << "px) is not wider than" << names[i - 1] << "(" << previous
                << "px); the narrower tier will shadow it";
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

namespace {

// `baseFilePath` is what a relative `iconRoot` resolves against. It is the
// config's own path for a config read off disk, but not for one that arrived
// over the dev-server wire: that document is staged into a cache directory,
// and resolving against the staging location pointed every relative icon at a
// path nothing can open.
bool applyConfigDocument(const QJsonObject &root, const QString &baseFilePath, bool reset)
{
    static const QStringList knownKeys = {
        QStringLiteral("colors"),       QStringLiteral("space"),
        QStringLiteral("breakpoints"),  QStringLiteral("themes"),
        QStringLiteral("defaultTheme"), QStringLiteral("iconRoot"),
    };
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (!knownKeys.contains(it.key()))
            qCWarning(lcLoomConfig).noquote()
                << "config: unknown key" << it.key() << "- expected one of"
                << knownKeys.join(QLatin1String(", "));
    }

    auto *registry = LoomTokenRegistry::instance();
    // Captured before the reset, restored after the file's themes are back.
    const QString previousTheme = registry->theme();
    // Only once the document has parsed: a reload that reset first and then hit
    // a syntax error would leave the application with the built-in tokens and
    // none of its own, which is a worse outcome than keeping the last good set.
    if (reset)
        registry->resetToDefaults();
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
        const QString directory = QFileInfo(baseFilePath).absolutePath();
        const QUrl base = directory.startsWith(QLatin1Char(':'))
            ? QUrl(QLatin1String("qrc") + directory + QLatin1Char('/'))
            : QUrl::fromLocalFile(directory + QLatin1Char('/'));
        setLoomIconRoot(base.resolved(QUrl(iconRoot)));
    } else if (reset) {
        // A reload replaces rather than merges, so a key deleted from the file
        // has to stop taking effect. Leaving the previous root live made
        // iconRoot the one setting a reload could not clear.
        setLoomIconRoot(QUrl());
    }

    const QString defaultTheme = root.value(QLatin1String("defaultTheme")).toString();
    if (reset && registry->themeNames().contains(previousTheme)) {
        // On a reload the session wins over the file's defaultTheme. Someone
        // who switched to dark to look at it, then saved the design file, meant
        // to restyle dark -- not to be thrown back to the default. Only when
        // the theme they were on no longer exists does defaultTheme apply.
        registry->setTheme(previousTheme);
    } else if (!defaultTheme.isEmpty()) {
        registry->setTheme(defaultTheme);
    }
    return true;
}

// Parse guard shared by both entry points. A document that does not parse must
// change nothing at all, so the reset only happens past this point.
bool parseConfig(const QByteArray &json, const QString &label, QJsonObject *root)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull() || !document.isObject()) {
        qCWarning(lcLoomConfig).noquote()
            << "config:" << label << "is not a JSON object:" << parseError.errorString()
            << "at offset" << parseError.offset;
        return false;
    }
    *root = document.object();
    return true;
}

bool applyConfigFile(const QString &filePath, bool reset)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcLoomConfig).noquote()
            << "config: cannot open" << filePath << ":" << file.errorString();
        return false;
    }
    QJsonObject root;
    if (!parseConfig(file.readAll(), filePath, &root))
        return false;
    return applyConfigDocument(root, filePath, reset);
}

} // namespace

bool loomLoadConfigFile(const QString &filePath)
{
    return applyConfigFile(filePath, /*reset=*/false);
}

bool loomReloadConfigFile(const QString &filePath)
{
    return applyConfigFile(filePath, /*reset=*/true);
}

bool loomReloadConfigData(const QByteArray &json, const QString &basePath)
{
    QJsonObject root;
    if (!parseConfig(json, basePath, &root))
        return false;
    return applyConfigDocument(root, basePath, /*reset=*/true);
}
