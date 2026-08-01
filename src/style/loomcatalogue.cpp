#include <loom/loomcatalogue.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "loomstylecompiler.h"
#include "loomtokenregistry.h"

namespace loom {

const char *version();

namespace {

// The prefix/scale pairing mirrors parseUtility()'s dispatch order. It is the
// one place in the catalogue that restates the parser rather than reading its
// tables, because the prefixes are written as literals in the parse chain --
// tst_catalogue asserts every class produced here round-trips through the
// parser, so a divergence fails the build rather than shipping bad completions.
QList<StyleUtilityFamily> families(const LoomTokenRegistry *registry)
{
    const QStringList colors = registry->colorKeys();
    const QStringList space = registry->spaceKeys();
    const QStringList radius = registry->radiusKeys();

    QList<StyleUtilityFamily> out;
    auto add =
        [&out](const QString &prefix, const QString &scale, const QStringList &values) {
            out.append(StyleUtilityFamily{prefix, scale, values});
        };

    add(QStringLiteral("bg-"), QStringLiteral("color"), colors);
    add(QStringLiteral("border-"), QStringLiteral("color"), colors);
    add(QStringLiteral("ring-"), QStringLiteral("color"), colors);
    add(QStringLiteral("from-"), QStringLiteral("color"), colors);
    add(QStringLiteral("via-"), QStringLiteral("color"), colors);
    add(QStringLiteral("to-"), QStringLiteral("color"), colors);
    // text- resolves size keys before colors, so both land under one prefix.
    QStringList textValues = registry->textSizeKeys();
    textValues.append(colors);
    textValues.sort();
    add(QStringLiteral("text-"), QStringLiteral("text size or color"), textValues);
    add(QStringLiteral("font-"), QStringLiteral("font weight"),
        registry->fontWeightKeys());
    add(QStringLiteral("font-"), QStringLiteral("font family"),
        registry->fontFamilyKeys());
    add(QStringLiteral("tracking-"), QStringLiteral("tracking"),
        registry->trackingKeys());
    add(QStringLiteral("opacity-"), QStringLiteral("opacity"), registry->opacityKeys());
    add(QStringLiteral("duration-"), QStringLiteral("duration"),
        registry->durationKeys());
    add(QStringLiteral("ease-"), QStringLiteral("easing"), registry->easingKeys());
    add(QStringLiteral("shadow-"), QStringLiteral("shadow"), registry->shadowKeys());
    add(QStringLiteral("rounded-"), QStringLiteral("radius"), radius);

    // Per-corner and per-edge radius: rounded-tl-lg, rounded-t-lg, ...
    for (const auto &side : {"tl", "tr", "br", "bl", "t", "r", "b", "l"}) {
        add(QStringLiteral("rounded-") + QLatin1String(side) + QLatin1Char('-'),
            QStringLiteral("radius"), radius);
    }

    add(QStringLiteral("gap-"), QStringLiteral("spacing"), space);
    add(QStringLiteral("w-"), QStringLiteral("spacing"), space);
    add(QStringLiteral("h-"), QStringLiteral("spacing"), space);
    add(QStringLiteral("size-"), QStringLiteral("spacing"), space);
    add(QStringLiteral("translate-x-"), QStringLiteral("spacing"), space);
    add(QStringLiteral("translate-y-"), QStringLiteral("spacing"), space);

    for (const auto &side : {"", "x", "y", "t", "r", "b", "l"}) {
        add(QStringLiteral("p") + QLatin1String(side) + QLatin1Char('-'),
            QStringLiteral("spacing"), space);
        add(QStringLiteral("m") + QLatin1String(side) + QLatin1Char('-'),
            QStringLiteral("spacing"), space);
    }

    // Layout size constraints. The anchor and alignment classes take no value,
    // so valuelessClasses() enumerates those.
    for (const auto &prefix : {"min-w-", "max-w-", "min-h-", "max-h-"})
        add(QLatin1String(prefix), QStringLiteral("spacing"), space);

    return out;
}

} // namespace

StyleCatalogue styleCatalogue()
{
    const auto *registry = LoomTokenRegistry::instance();

    StyleCatalogue catalogue;
    catalogue.version = QString::fromLatin1(version());
    catalogue.theme = registry->theme();
    catalogue.variants = LoomStyleCompiler::variantNames();
    catalogue.families = families(registry);
    // Spans take a cell count and aspect a ratio; neither comes from a token
    // scale, so neither can be enumerated.
    catalogue.numericPrefixes = {
        QStringLiteral("border-"),     QStringLiteral("col-span-"),
        QStringLiteral("row-span-"),   QStringLiteral("aspect-"),
        QStringLiteral("ring-"),       QStringLiteral("rotate-"),
        QStringLiteral("scale-"),      QStringLiteral("z-"),
        QStringLiteral("brightness-"), QStringLiteral("contrast-"),
        QStringLiteral("saturate-")};

    catalogue.classes = LoomStyleCompiler::valuelessClasses();
    for (const QString &recipe : registry->styleRecipeKeys())
        catalogue.classes.append(QLatin1Char('@') + recipe);
    for (const StyleUtilityFamily &family : catalogue.families) {
        for (const QString &value : family.values)
            catalogue.classes.append(family.prefix + value);
    }
    catalogue.classes.sort();
    catalogue.classes.removeDuplicates();
    return catalogue;
}

QByteArray styleCatalogueJson()
{
    const StyleCatalogue catalogue = styleCatalogue();

    QJsonArray families;
    for (const StyleUtilityFamily &family : catalogue.families) {
        families.append(
            QJsonObject{
                {QStringLiteral("prefix"), family.prefix},
                {QStringLiteral("scale"), family.scale},
                {QStringLiteral("values"), QJsonArray::fromStringList(family.values)},
            });
    }

    const QJsonObject root{
        {QStringLiteral("version"), catalogue.version},
        {QStringLiteral("theme"), catalogue.theme},
        {QStringLiteral("variants"), QJsonArray::fromStringList(catalogue.variants)},
        {QStringLiteral("classes"), QJsonArray::fromStringList(catalogue.classes)},
        {QStringLiteral("families"), families},
        {QStringLiteral("numericPrefixes"),
         QJsonArray::fromStringList(catalogue.numericPrefixes)},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QStringList unknownStyleClasses(const QString &style)
{
    return LoomStyleCompiler::unknownClasses(style);
}

} // namespace loom
