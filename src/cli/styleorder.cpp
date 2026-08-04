#include "styleorder.h"

#include <QMap>
#include <QStringList>
#include <algorithm>
#include <limits>

#include "style/loomstylecompiler.h"

namespace {

// Everything a rule resolves to, in the order the engine will apply it. Two
// class strings mean the same thing exactly when these match, element for
// element -- including order, because order is what decides ties.
// The condition slot a rule competes in: what it writes, under what
// conditions. Only one rule can win each of these.
QString slotKey(const LoomStyleRule &rule)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14")
        .arg(int(rule.utility))
        .arg(rule.stateMask)
        .arg(rule.stateNotMask)
        .arg(rule.minWidth)
        .arg(rule.maxWidth)
        .arg(rule.containerMinWidth)
        .arg(rule.containerMaxWidth)
        .arg(rule.groupStateMask)
        .arg(rule.groupStateNotMask)
        .arg(rule.customMask)
        .arg(rule.customNotMask)
        .arg(rule.groupCustomMask)
        .arg(rule.groupCustomNotMask)
        .arg(rule.containerName, rule.groupName, rule.themeName);
}

QString ruleSignature(const LoomStyleRule &rule)
{
    return QStringLiteral(
               "%1/%2/%3/%4/%5/%6/%7/%8/%9/%10/%11/%12/%13/%14/%15/%16/%17/%18")
        .arg(int(rule.utility))
        .arg(rule.stateMask)
        .arg(rule.stateNotMask)
        .arg(rule.specificity)
        .arg(rule.literal, 0, 'g', 17)
        .arg(rule.alphaPercent)
        .arg(int(rule.flag))
        .arg(int(rule.negative))
        .arg(rule.fraction, 0, 'g', 17)
        .arg(rule.minWidth)
        .arg(rule.maxWidth)
        .arg(rule.containerMinWidth)
        .arg(rule.containerMaxWidth)
        .arg(rule.groupStateMask)
        .arg(rule.key, rule.containerName, rule.groupName, rule.themeName);
}

// What a class string actually resolves to, independent of how it is written.
//
// Comparing the rule vectors element for element is too strict: the engine
// picks a winner per condition slot by specificity and then by position, so
// two orderings can produce different vectors and identical results. What
// matters is which rule wins each slot -- so group by slot, keep the winner,
// and emit the winners in a canonical order.
QString styleSignature(const QString &style)
{
    const auto compiled = LoomStyleCompiler::compile(style);

    struct Winner {
        quint64 specificity = 0;
        qsizetype position = -1;
        QString signature;
    };
    QMap<QString, Winner> winners;

    for (qsizetype i = 0; i < compiled->rules.size(); ++i) {
        const auto &rule = compiled->rules.at(i);
        const QString slot = slotKey(rule);
        Winner &winner = winners[slot];
        // Later wins at equal specificity, which is the rule compile() states.
        if (winner.position < 0 || rule.specificity > winner.specificity
            || (rule.specificity == winner.specificity && i > winner.position)) {
            winner = Winner{rule.specificity, i, ruleSignature(rule)};
        }
    }

    QString signature;
    for (auto it = winners.cbegin(); it != winners.cend(); ++it) {
        signature += it.key();
        signature += QLatin1Char('=');
        signature += it.value().signature;
        signature += QLatin1Char(';');
    }
    return signature;
}

struct Ranked {
    QString name;
    quint64 specificity = 0;
    int utility = std::numeric_limits<int>::max();
};

Ranked rank(const QString &name)
{
    Ranked ranked;
    ranked.name = name;
    const auto compiled = LoomStyleCompiler::compile(name);
    if (compiled->rules.isEmpty())
        return ranked;
    // Specificity first, so the string reads in the order the engine resolves
    // it -- unconditional classes, then responsive, then stateful. It is the
    // same axis loomSpecificity() already ranks on, so there is one notion of
    // order in the system rather than two that can disagree.
    ranked.specificity = compiled->rules.first().specificity;
    // Then what it writes, which groups a family together. The lowest utility
    // of the rules, so a shorthand sorts beside the longhands it covers.
    for (const auto &rule : compiled->rules)
        ranked.utility = std::min(ranked.utility, int(rule.utility));
    return ranked;
}

} // namespace

namespace loom::styleorder {

QString canonicalOrder(const QString &style)
{
    const QStringList classes = style.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (classes.size() < 2)
        return style;

    QList<Ranked> ranked;
    ranked.reserve(classes.size());
    for (const QString &name : classes) {
        // An unknown class has no rules and no rank. Sorting around one would
        // move it somewhere arbitrary in a string its author is still editing,
        // which is exactly when they are looking at it.
        const Ranked entry = rank(name);
        if (entry.utility == std::numeric_limits<int>::max())
            return style;
        ranked.append(entry);
    }

    // Stable, so classes that rank equal -- which is every pair writing the
    // same thing under the same conditions -- keep the order that decides
    // which of them wins.
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) {
        if (a.specificity != b.specificity)
            return a.specificity < b.specificity;
        return a.utility < b.utility;
    });

    QStringList sorted;
    sorted.reserve(ranked.size());
    for (const Ranked &entry : ranked)
        sorted.append(entry.name);

    const QString candidate = sorted.join(QLatin1Char(' '));
    if (candidate == style)
        return style;

    // The check that makes this safe to run over someone's source. Ranking
    // cannot see every overlap -- `size-4` writes what `w-8` writes and they
    // are different utilities -- so rather than reason about which pairs are
    // safe, compile both and compare. A string that would change meaning is
    // returned untouched.
    if (styleSignature(candidate) != styleSignature(style))
        return style;
    return candidate;
}

} // namespace loom::styleorder
