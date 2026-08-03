#include "loomconfigloader.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointF>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>

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

template <typename Add>
void loadNumberScale(
    const QJsonObject &values, const QString &label, double minimum, double maximum,
    Add add)
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!it->isDouble() || it->toDouble() < minimum || it->toDouble() > maximum) {
            qCWarning(lcLoomConfig).noquote()
                << "config:" << label << "entry" << it.key() << "must be between"
                << minimum << "and" << maximum;
            continue;
        }
        add(it.key(), it->toDouble());
    }
}

void loadTextSizes(LoomTokenRegistry *registry, const QJsonObject &values)
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const QJsonObject value = it->toObject();
        const double size = value.value(QLatin1String("size")).toDouble(-1);
        const double lineHeight = value.value(QLatin1String("lineHeight")).toDouble(-1);
        if (size <= 0 || lineHeight <= 0) {
            qCWarning(lcLoomConfig).noquote()
                << "config: textSizes entry" << it.key()
                << "needs positive size and lineHeight values";
            continue;
        }
        registry->addTextSize(it.key(), LoomTextStyle{size, lineHeight});
    }
}

void loadFontFamilies(LoomTokenRegistry *registry, const QJsonObject &values)
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        QStringList families;
        if (it->isString())
            families.append(it->toString());
        else if (it->isArray()) {
            for (const auto &entry : it->toArray()) {
                if (entry.isString() && !entry.toString().trimmed().isEmpty())
                    families.append(entry.toString().trimmed());
            }
        }
        if (families.isEmpty()) {
            qCWarning(lcLoomConfig).noquote()
                << "config: fontFamilies entry" << it.key()
                << "must be a string or non-empty string array";
            continue;
        }
        registry->addFontFamily(it.key(), families);
    }
}

LoomShadow parseShadow(const QString &key, const QJsonValue &entry, bool *ok)
{
    const QJsonObject value = entry.toObject();
    static const QStringList fields{
        QStringLiteral("color"), QStringLiteral("offsetX"), QStringLiteral("offsetY"),
        QStringLiteral("blur"), QStringLiteral("spread")};
    const QColor color = QColor::fromString(
        value.value(QLatin1String("color")).toString(QStringLiteral("#40000000")));
    const auto number = [&value](const char *name) {
        return value.value(QLatin1String(name)).toDouble(0);
    };
    bool knownFields = true;
    bool numericFields = true;
    for (auto it = value.constBegin(); it != value.constEnd(); ++it) {
        knownFields = knownFields && fields.contains(it.key());
        numericFields = numericFields
            && (it.key() == QLatin1String("color")
                    ? it->isString()
                    : it->isDouble() && std::isfinite(it->toDouble()));
    }
    if (!entry.isObject() || !knownFields || !numericFields || !color.isValid()
        || number("blur") < 0) {
        qCWarning(lcLoomConfig).noquote() << "config: shadows entry" << key
                                          << "needs a valid color and non-negative blur";
        *ok = false;
        return {};
    }
    *ok = true;
    return LoomShadow{
        color, number("offsetX"), number("offsetY"), number("blur"), number("spread")};
}

void loadShadows(LoomTokenRegistry *registry, const QJsonObject &values)
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        bool ok = false;
        const LoomShadow shadow = parseShadow(it.key(), it.value(), &ok);
        if (ok)
            registry->addShadow(it.key(), shadow);
    }
}

QEasingCurve parseEasing(const QString &key, const QJsonValue &entry, bool *ok)
{
    const QJsonArray points = entry.toArray();
    if (points.size() != 4
        || !std::all_of(
            points.cbegin(), points.cend(), [](const auto &v) { return v.isDouble(); })
        || points.at(0).toDouble() < 0 || points.at(0).toDouble() > 1
        || points.at(2).toDouble() < 0 || points.at(2).toDouble() > 1) {
        qCWarning(lcLoomConfig).noquote()
            << "config: easings entry" << key
            << "must be [x1, y1, x2, y2] with x coordinates in 0..1";
        *ok = false;
        return {};
    }
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(
        QPointF(points.at(0).toDouble(), points.at(1).toDouble()),
        QPointF(points.at(2).toDouble(), points.at(3).toDouble()), QPointF(1, 1));
    *ok = true;
    return curve;
}

void loadEasings(LoomTokenRegistry *registry, const QJsonObject &values)
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        bool ok = false;
        const QEasingCurve curve = parseEasing(it.key(), it.value(), &ok);
        if (ok)
            registry->addEasing(it.key(), curve);
    }
}

void loadBreakpoints(LoomTokenRegistry *registry, const QJsonObject &breakpoints)
{
    for (auto it = breakpoints.constBegin(); it != breakpoints.constEnd(); ++it) {
        if (!it->isDouble() || !registry->setBreakpoint(it.key(), it->toInt())) {
            qCWarning(lcLoomConfig).noquote()
                << "config: breakpoints entries must be positive pixel values, not"
                << it.key();
        }
    }

    // The tiers are min-width and cumulative, so a threshold below the one
    // before it can never be the widest match: its classes would be shadowed by
    // the narrower tier and appear to do nothing. Cheap to detect, and
    // impossible to diagnose from the styling alone.
    QStringList names;
    for (const QString &name :
         {QStringLiteral("sm"), QStringLiteral("md"), QStringLiteral("lg"),
          QStringLiteral("xl"), QStringLiteral("2xl")}) {
        if (registry->hasBreakpoint(name))
            names.append(name);
    }
    for (int i = 1; i < names.size(); ++i) {
        const int previous = registry->breakpoint(names.at(i - 1));
        const int current = registry->breakpoint(names.at(i));
        if (current <= previous) {
            qCWarning(lcLoomConfig).noquote()
                << "config: breakpoint" << names.at(i) << "(" << current
                << "px) is not wider than" << names.at(i - 1) << "(" << previous
                << "px); the narrower tier will shadow it";
        }
    }
}

void loadContainers(LoomTokenRegistry *registry, const QJsonObject &containers)
{
    for (auto it = containers.constBegin(); it != containers.constEnd(); ++it) {
        if (!it->isDouble() || !registry->setContainer(it.key(), it->toInt())) {
            qCWarning(lcLoomConfig).noquote()
                << "config: containers entries must be positive pixel values, not"
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

            const QJsonObject tokens = theme.value(QLatin1String("tokens")).toObject();
            QHash<QString, QString> refs;
            std::optional<bool> dark;
            if (theme.contains(QLatin1String("dark")))
                dark = theme.value(QLatin1String("dark")).toBool();
            const QJsonObject colors = tokens.value(QLatin1String("colors")).toObject();
            for (auto it = colors.constBegin(); it != colors.constEnd(); ++it)
                refs.insert(it.key(), it->toString());

            if (registry->defineTheme(name, base, refs, dark)) {
                LoomTokenRegistry::ThemeOverrides overrides;
                const auto copyNumbers = [](const QJsonObject &object, auto *target) {
                    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
                        if (it->isDouble())
                            target->insert(it.key(), it->toDouble());
                    }
                };
                copyNumbers(
                    tokens.value(QLatin1String("space")).toObject(), &overrides.space);
                copyNumbers(
                    tokens.value(QLatin1String("tracking")).toObject(),
                    &overrides.tracking);
                copyNumbers(
                    tokens.value(QLatin1String("radius")).toObject(), &overrides.radius);
                copyNumbers(
                    tokens.value(QLatin1String("opacity")).toObject(),
                    &overrides.opacity);

                const QJsonObject textSizes =
                    tokens.value(QLatin1String("textSizes")).toObject();
                for (auto it = textSizes.constBegin(); it != textSizes.constEnd(); ++it) {
                    const auto value = it->toObject();
                    const double size = value.value(QLatin1String("size")).toDouble(-1);
                    const double lineHeight =
                        value.value(QLatin1String("lineHeight")).toDouble(-1);
                    if (size > 0 && lineHeight > 0)
                        overrides.textSizes.insert(
                            it.key(), LoomTextStyle{size, lineHeight});
                }
                const QJsonObject weights =
                    tokens.value(QLatin1String("fontWeights")).toObject();
                for (auto it = weights.constBegin(); it != weights.constEnd(); ++it) {
                    if (it->isDouble() && it->toInt() >= 1 && it->toInt() <= 1000)
                        overrides.fontWeights.insert(it.key(), it->toInt());
                }
                const QJsonObject families =
                    tokens.value(QLatin1String("fontFamilies")).toObject();
                for (auto it = families.constBegin(); it != families.constEnd(); ++it) {
                    QStringList family;
                    if (it->isString())
                        family.append(it->toString());
                    else {
                        for (const auto &entry : it->toArray())
                            if (entry.isString())
                                family.append(entry.toString());
                    }
                    if (!family.isEmpty())
                        overrides.fontFamilies.insert(it.key(), family);
                }
                const QJsonObject shadows =
                    tokens.value(QLatin1String("shadows")).toObject();
                for (auto it = shadows.constBegin(); it != shadows.constEnd(); ++it) {
                    bool ok = false;
                    const auto shadow = parseShadow(it.key(), it.value(), &ok);
                    if (ok)
                        overrides.shadows.insert(it.key(), shadow);
                }
                const QJsonObject durations =
                    tokens.value(QLatin1String("durations")).toObject();
                for (auto it = durations.constBegin(); it != durations.constEnd(); ++it) {
                    if (it->isDouble() && it->toInt() >= 0)
                        overrides.durations.insert(it.key(), it->toInt());
                }
                const QJsonObject easings =
                    tokens.value(QLatin1String("easings")).toObject();
                for (auto it = easings.constBegin(); it != easings.constEnd(); ++it) {
                    bool ok = false;
                    const auto easing = parseEasing(it.key(), it.value(), &ok);
                    if (ok)
                        overrides.easing.insert(it.key(), easing);
                }
                registry->defineTheme(name, {}, overrides, dark);
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

bool invalidConfig(const QString &label, const QString &path, const QString &message)
{
    qCWarning(lcLoomConfig).noquote() << "config:" << label << path << message;
    return false;
}

bool validateTokenObject(
    const QJsonObject &tokens, const QString &label, const QString &prefix,
    bool themeTokens)
{
    static const QStringList known{
        QStringLiteral("colors"),       QStringLiteral("space"),
        QStringLiteral("textSizes"),    QStringLiteral("fontWeights"),
        QStringLiteral("fontFamilies"), QStringLiteral("tracking"),
        QStringLiteral("radius"),       QStringLiteral("shadows"),
        QStringLiteral("opacity"),      QStringLiteral("durations"),
        QStringLiteral("easings"),      QStringLiteral("breakpoints"),
        QStringLiteral("containers"),
    };
    for (auto family = tokens.constBegin(); family != tokens.constEnd(); ++family) {
        const QString path = prefix + QLatin1Char('.') + family.key();
        if (!known.contains(family.key()))
            return invalidConfig(
                label, path, QStringLiteral("is an unknown token family"));
        if (!family->isObject())
            return invalidConfig(label, path, QStringLiteral("must be an object"));
        if (themeTokens
            && (family.key() == QLatin1String("breakpoints")
                || family.key() == QLatin1String("containers"))) {
            return invalidConfig(
                label, path,
                QStringLiteral("cannot be theme-specific because it changes layout"));
        }
    }

    const QJsonObject colors = tokens.value(QLatin1String("colors")).toObject();
    for (auto it = colors.constBegin(); it != colors.constEnd(); ++it) {
        if (it->isObject() && !themeTokens) {
            const QJsonObject shades = it->toObject();
            for (auto shade = shades.constBegin(); shade != shades.constEnd(); ++shade) {
                if (!shade->isString()
                    || !QColor::fromString(shade->toString()).isValid())
                    return invalidConfig(
                        label,
                        prefix + QStringLiteral(".colors.") + it.key() + QLatin1Char('.')
                            + shade.key(),
                        QStringLiteral("must be a valid color"));
            }
        } else if (
            !it->isString()
            || (!themeTokens && !QColor::fromString(it->toString()).isValid())) {
            return invalidConfig(
                label, prefix + QStringLiteral(".colors.") + it.key(),
                themeTokens ? QStringLiteral("must be a color or palette reference")
                            : QStringLiteral("must be a valid color"));
        }
    }

    const auto validateNumbers = [&](const char *family, double minimum, double maximum,
                                     bool integer) {
        const QJsonObject values = tokens.value(QLatin1String(family)).toObject();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            const double value = it->toDouble(std::numeric_limits<double>::quiet_NaN());
            if (!it->isDouble() || !std::isfinite(value) || value < minimum
                || value > maximum || (integer && std::floor(value) != value)) {
                return invalidConfig(
                    label,
                    prefix + QLatin1Char('.') + QLatin1String(family) + QLatin1Char('.')
                        + it.key(),
                    QStringLiteral("has an invalid numeric value"));
            }
        }
        return true;
    };
    if (!validateNumbers("space", 0, 1000000, false)
        || !validateNumbers("fontWeights", 1, 1000, true)
        || !validateNumbers("tracking", -10, 10, false)
        || !validateNumbers("radius", 0, 1000000, false)
        || !validateNumbers("opacity", 0, 1, false)
        || !validateNumbers("durations", 0, 3600000, true)
        || !validateNumbers("breakpoints", 1, std::numeric_limits<int>::max(), true)
        || !validateNumbers("containers", 1, std::numeric_limits<int>::max(), true))
        return false;

    const QJsonObject textSizes = tokens.value(QLatin1String("textSizes")).toObject();
    for (auto it = textSizes.constBegin(); it != textSizes.constEnd(); ++it) {
        const QJsonObject value = it->toObject();
        if (!it->isObject() || value.size() != 2
            || value.value(QLatin1String("size")).toDouble(-1) <= 0
            || value.value(QLatin1String("lineHeight")).toDouble(-1) <= 0) {
            return invalidConfig(
                label, prefix + QStringLiteral(".textSizes.") + it.key(),
                QStringLiteral("must contain positive size and lineHeight values"));
        }
    }
    const QJsonObject families = tokens.value(QLatin1String("fontFamilies")).toObject();
    for (auto it = families.constBegin(); it != families.constEnd(); ++it) {
        bool valid = it->isString() && !it->toString().trimmed().isEmpty();
        if (it->isArray()) {
            const QJsonArray entries = it->toArray();
            valid = !entries.isEmpty()
                && std::all_of(entries.cbegin(), entries.cend(), [](const auto &entry) {
                       return entry.isString() && !entry.toString().trimmed().isEmpty();
                   });
        }
        if (!valid)
            return invalidConfig(
                label, prefix + QStringLiteral(".fontFamilies.") + it.key(),
                QStringLiteral("must be a string or non-empty string array"));
    }
    const QJsonObject shadows = tokens.value(QLatin1String("shadows")).toObject();
    for (auto it = shadows.constBegin(); it != shadows.constEnd(); ++it) {
        bool ok = false;
        parseShadow(it.key(), it.value(), &ok);
        if (!ok)
            return invalidConfig(
                label, prefix + QStringLiteral(".shadows.") + it.key(),
                QStringLiteral("is invalid"));
    }
    const QJsonObject easings = tokens.value(QLatin1String("easings")).toObject();
    for (auto it = easings.constBegin(); it != easings.constEnd(); ++it) {
        bool ok = false;
        parseEasing(it.key(), it.value(), &ok);
        if (!ok)
            return invalidConfig(
                label, prefix + QStringLiteral(".easings.") + it.key(),
                QStringLiteral("is invalid"));
    }
    return true;
}

bool validateConfigShape(const QJsonObject &root, const QString &label)
{
    // Version first: a v1 document naturally contains keys that are unknown in
    // v2. Reporting the first of those hides the actionable migration command.
    const QJsonValue schemaVersion = root.value(QLatin1String("schemaVersion"));
    if (!schemaVersion.isDouble() || schemaVersion.toDouble() != 2.0)
        return invalidConfig(
            label, QStringLiteral("schemaVersion"),
            QStringLiteral("must be 2; run 'loom migrate --to 2 --apply'"));
    static const QStringList rootKeys{
        QStringLiteral("$schema"), QStringLiteral("schemaVersion"),
        QStringLiteral("tokens"),  QStringLiteral("themes"),
        QStringLiteral("theme"),   QStringLiteral("styles"),
        QStringLiteral("lint"),    QStringLiteral("iconRoot"),
        QStringLiteral("states"),
    };
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (!rootKeys.contains(it.key()))
            return invalidConfig(
                label, it.key(), QStringLiteral("is an unknown top-level key"));
    }
    if (root.contains(QLatin1String("$schema"))
        && !root.value(QLatin1String("$schema")).isString())
        return invalidConfig(
            label, QStringLiteral("$schema"), QStringLiteral("must be a string"));
    for (const auto &key : {"tokens", "themes", "theme", "styles", "lint", "states"}) {
        const QJsonValue value = root.value(QLatin1String(key));
        if (!value.isUndefined() && !value.isObject())
            return invalidConfig(
                label, QLatin1String(key), QStringLiteral("must be an object"));
    }
    if (root.contains(QLatin1String("iconRoot"))
        && !root.value(QLatin1String("iconRoot")).isString())
        return invalidConfig(
            label, QStringLiteral("iconRoot"), QStringLiteral("must be a string"));
    if (!validateTokenObject(
            root.value(QLatin1String("tokens")).toObject(), label,
            QStringLiteral("tokens"), false))
        return false;

    const QJsonObject themes = root.value(QLatin1String("themes")).toObject();
    for (auto it = themes.constBegin(); it != themes.constEnd(); ++it) {
        const QJsonObject theme = it->toObject();
        if (it.key().isEmpty() || !it->isObject())
            return invalidConfig(
                label, QStringLiteral("themes.") + it.key(),
                QStringLiteral("must be an object"));
        static const QStringList themeKeys{
            QStringLiteral("extends"), QStringLiteral("dark"), QStringLiteral("tokens")};
        for (auto value = theme.constBegin(); value != theme.constEnd(); ++value) {
            if (!themeKeys.contains(value.key()))
                return invalidConfig(
                    label,
                    QStringLiteral("themes.") + it.key() + QLatin1Char('.') + value.key(),
                    QStringLiteral("is unknown"));
        }
        if ((theme.contains(QLatin1String("extends"))
             && (!theme.value(QLatin1String("extends")).isString()
                 || theme.value(QLatin1String("extends")).toString().isEmpty()))
            || (theme.contains(QLatin1String("dark"))
                && !theme.value(QLatin1String("dark")).isBool())
            || (theme.contains(QLatin1String("tokens"))
                && !theme.value(QLatin1String("tokens")).isObject())) {
            return invalidConfig(
                label, QStringLiteral("themes.") + it.key(),
                QStringLiteral("has invalid extends, dark, or tokens fields"));
        }
        if (!validateTokenObject(
                theme.value(QLatin1String("tokens")).toObject(), label,
                QStringLiteral("themes.") + it.key() + QStringLiteral(".tokens"), true))
            return false;
    }
    for (auto it = themes.constBegin(); it != themes.constEnd(); ++it) {
        QSet<QString> seen{it.key()};
        QString base = it->toObject().value(QLatin1String("extends")).toString();
        while (!base.isEmpty() && base != QLatin1String("light")
               && base != QLatin1String("dark")) {
            if (!themes.contains(base) || seen.contains(base))
                return invalidConfig(
                    label,
                    QStringLiteral("themes.") + it.key() + QStringLiteral(".extends"),
                    QStringLiteral("must name an existing, acyclic theme"));
            seen.insert(base);
            base =
                themes.value(base).toObject().value(QLatin1String("extends")).toString();
        }
    }
    const QJsonObject styles = root.value(QLatin1String("styles")).toObject();
    for (auto it = styles.constBegin(); it != styles.constEnd(); ++it) {
        if (it.key().isEmpty() || !it->isString())
            return invalidConfig(
                label, QStringLiteral("styles.") + it.key(),
                QStringLiteral("must be a named string recipe"));
    }
    const QJsonObject states = root.value(QLatin1String("states")).toObject();
    if (states.size() > LoomTokenRegistry::MaxCustomStates)
        return invalidConfig(
            label, QStringLiteral("states"),
            QStringLiteral("declares more than %1 states")
                .arg(LoomTokenRegistry::MaxCustomStates));
    for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
        const QString name = it.key();
        if (!it->isString())
            return invalidConfig(
                label, QStringLiteral("states.") + name,
                QStringLiteral("must be a description string"));
        // The same shape as the variant vocabulary it joins: lower-case,
        // digits and dashes. Anything else could not be written as a prefix.
        static const QRegularExpression shape(QStringLiteral("\\A[a-z][a-z0-9-]*\\z"));
        if (!shape.match(name).hasMatch())
            return invalidConfig(
                label, QStringLiteral("states.") + name,
                QStringLiteral("must be lower-case letters, digits and dashes, "
                               "starting with a letter"));
        // A hard error rather than a precedence rule. parseVariant() asks the
        // built-in table first, so a state named `hover` would compile to the
        // built-in and never match the application's value -- the config would
        // be quietly meaningless, which is worse than being rejected.
        if (LoomStyleCompiler::isBuiltinVariant(name))
            return invalidConfig(
                label, QStringLiteral("states.") + name,
                QStringLiteral("collides with a built-in variant"));
        // These spell a *composed* variant. Declaring `not-found` would make
        // `not-found:` ambiguous with the negation of a state called `found`.
        for (const auto &reserved : {"not-", "group-", "theme-", "max-", "min-"}) {
            if (name.startsWith(QLatin1String(reserved)))
                return invalidConfig(
                    label, QStringLiteral("states.") + name,
                    QStringLiteral("must not start with the reserved prefix '%1'")
                        .arg(QLatin1String(reserved)));
        }
    }

    const QJsonObject theme = root.value(QLatin1String("theme")).toObject();
    for (auto it = theme.constBegin(); it != theme.constEnd(); ++it) {
        if ((it.key() != QLatin1String("default") && it.key() != QLatin1String("light")
             && it.key() != QLatin1String("dark"))
            || !it->isString()) {
            return invalidConfig(
                label, QStringLiteral("theme.") + it.key(),
                QStringLiteral("is unknown or is not a string"));
        }
        const QString name = it->toString();
        if (name.isEmpty()
            || (it.key() != QLatin1String("default")
                && name == QLatin1String("system"))) {
            return invalidConfig(
                label, QStringLiteral("theme.") + it.key(),
                QStringLiteral("must name a concrete theme"));
        }
        if (name != QLatin1String("system") && name != QLatin1String("light")
            && name != QLatin1String("dark") && !themes.contains(name)) {
            return invalidConfig(
                label, QStringLiteral("theme.") + it.key(),
                QStringLiteral("names an unknown theme"));
        }
    }
    const QJsonObject lint = root.value(QLatin1String("lint")).toObject();
    for (auto it = lint.constBegin(); it != lint.constEnd(); ++it) {
        if (it.key() != QLatin1String("arbitraryValues"))
            return invalidConfig(
                label, QStringLiteral("lint.") + it.key(),
                QStringLiteral("is an unknown lint setting"));
    }
    const QString policy =
        lint.value(QLatin1String("arbitraryValues")).toString(QStringLiteral("warn"));
    if (policy != QLatin1String("allow") && policy != QLatin1String("warn")
        && policy != QLatin1String("deny"))
        return invalidConfig(
            label, QStringLiteral("lint.arbitraryValues"),
            QStringLiteral("must be allow, warn, or deny"));
    return true;
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
    if (!validateConfigShape(root, baseFilePath))
        return false;
    auto *registry = LoomTokenRegistry::instance();
    // Captured before the reset, restored after the file's themes are back.
    const QString previousTheme = registry->theme();
    const auto previousThemeMode = registry->themeMode();
    // Only once the document has parsed: a reload that reset first and then hit
    // a syntax error would leave the application with the built-in tokens and
    // none of its own, which is a worse outcome than keeping the last good set.
    if (reset)
        registry->resetToDefaults();
    const QJsonObject tokens = root.value(QLatin1String("tokens")).toObject();
    // Colors first: theme entries may reference config-defined palette keys.
    loadColors(registry, tokens.value(QLatin1String("colors")).toObject());
    loadSpace(registry, tokens.value(QLatin1String("space")).toObject());
    loadTextSizes(registry, tokens.value(QLatin1String("textSizes")).toObject());
    loadNumberScale(
        tokens.value(QLatin1String("fontWeights")).toObject(),
        QStringLiteral("fontWeights"), 1, 1000,
        [registry](const QString &key, double value) {
            registry->addFontWeight(key, int(value));
        });
    loadFontFamilies(registry, tokens.value(QLatin1String("fontFamilies")).toObject());
    loadNumberScale(
        tokens.value(QLatin1String("tracking")).toObject(), QStringLiteral("tracking"),
        -10, 10, [registry](const QString &key, double value) {
            registry->addTracking(key, value);
        });
    loadNumberScale(
        tokens.value(QLatin1String("radius")).toObject(), QStringLiteral("radius"), 0,
        1000000, [registry](const QString &key, double value) {
            registry->addRadius(key, value);
        });
    loadShadows(registry, tokens.value(QLatin1String("shadows")).toObject());
    loadNumberScale(
        tokens.value(QLatin1String("opacity")).toObject(), QStringLiteral("opacity"), 0,
        1, [registry](const QString &key, double value) {
            registry->addOpacity(key, value);
        });
    loadNumberScale(
        tokens.value(QLatin1String("durations")).toObject(), QStringLiteral("durations"),
        0, 3600000, [registry](const QString &key, double value) {
            registry->addDuration(key, int(value));
        });
    loadEasings(registry, tokens.value(QLatin1String("easings")).toObject());
    loadBreakpoints(registry, tokens.value(QLatin1String("breakpoints")).toObject());
    loadContainers(registry, tokens.value(QLatin1String("containers")).toObject());
    loadThemes(registry, root.value(QLatin1String("themes")).toObject());
    const QJsonObject styles = root.value(QLatin1String("styles")).toObject();
    for (auto it = styles.constBegin(); it != styles.constEnd(); ++it) {
        if (!it->isString() || it.key().isEmpty()) {
            qCWarning(lcLoomConfig).noquote()
                << "config: style recipe" << it.key() << "must be a string";
            continue;
        }
        registry->setStyleRecipe(it.key(), it->toString());
    }
    // Order matters: a state's bit is its index, so iterating the QJsonObject
    // (which sorts keys) rather than the source order keeps the assignment
    // stable across reloads. Nothing persists a bit, but a stable order makes
    // the catalogue and the inspector read the same way twice.
    const QJsonObject customStates = root.value(QLatin1String("states")).toObject();
    for (auto it = customStates.constBegin(); it != customStates.constEnd(); ++it) {
        if (!registry->setCustomState(it.key(), it->toString())) {
            qCWarning(lcLoomConfig).noquote()
                << "config: state" << it.key() << "was not registered";
        }
    }
    registry->setArbitraryValuePolicy(root.value(QLatin1String("lint"))
                                          .toObject()
                                          .value(QLatin1String("arbitraryValues"))
                                          .toString(QStringLiteral("warn")));

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

    const QJsonObject themeConfig = root.value(QLatin1String("theme")).toObject();
    registry->setSystemThemes(
        themeConfig.value(QLatin1String("light")).toString(QStringLiteral("light")),
        themeConfig.value(QLatin1String("dark")).toString(QStringLiteral("dark")));
    const QString configuredTheme =
        themeConfig.value(QLatin1String("default")).toString(QStringLiteral("light"));
    if (reset && previousThemeMode == LoomTokenRegistry::ThemeMode::System) {
        registry->setThemeMode(LoomTokenRegistry::ThemeMode::System);
    } else if (reset && registry->themeNames().contains(previousTheme)) {
        // On a reload the session wins over theme.default. Someone
        // who switched to dark to look at it, then saved the design file, meant
        // to restyle dark -- not to be thrown back to the default. Only when
        // the theme they were on no longer exists does theme.default apply.
        registry->setTheme(previousTheme);
    } else if (configuredTheme == QLatin1String("system")) {
        registry->setThemeMode(LoomTokenRegistry::ThemeMode::System);
    } else if (!configuredTheme.isEmpty()) {
        registry->setTheme(configuredTheme);
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
