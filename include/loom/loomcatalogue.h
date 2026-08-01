#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// The `Lo.style` vocabulary, enumerated for tooling: editor completion data,
// documentation generators, and the class checker behind `loomstyle --check`.
//
// Everything here is derived from the parser's own tables and the live token
// registry, never from a second hand-maintained list -- a utility or token
// added to Loom shows up here without another edit. The catalogue therefore
// reflects the *current* registry contents, so a config that defines extra
// colors or spacing keys widens it; dump it after loading the same config the
// application uses.
namespace loom {

// A family of classes formed by a fixed prefix plus a token key, e.g. `bg-`
// over the color scale. `prefix + value` is a valid class for every value.
struct StyleUtilityFamily {
    QString prefix;
    QString scale;      // token scale the values come from, for display
    QStringList values; // concrete keys, sorted
};

struct StyleCatalogue {
    QString version;      // loom version the catalogue was produced by
    QString theme;        // active theme at the time the catalogue was produced
    QStringList variants; // viewport/container, interaction, control, environment, group
    QStringList classes;  // every enumerable class, without variant prefixes
    QList<StyleUtilityFamily> families;
    // Prefixes that additionally accept a bare number, which cannot be
    // enumerated: `border-2`, `border-0.5`. Completion should offer the
    // families above and let anything numeric through.
    QStringList numericPrefixes;
};

StyleCatalogue styleCatalogue();

// The same data as JSON, the form editor integrations consume.
QByteArray styleCatalogueJson();

// Classes in `style` that no utility recognises, in order of appearance.
// Empty means every class applies. Does not warn or cache.
QStringList unknownStyleClasses(const QString &style);

} // namespace loom
