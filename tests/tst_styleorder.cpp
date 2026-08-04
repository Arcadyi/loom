#include <QtTest>

#include "styleorder.h"

#include <QMap>

#include "style/loomstylecompiler.h"

using loom::styleorder::canonicalOrder;

class StyleOrderTests final : public QObject {
    Q_OBJECT

private slots:
    void unconditionalClassesComeBeforeConditionalOnes();
    void orderingIsIdempotent();
    void orderingNeverChangesWhatAStringCompilesTo();
    void ambiguousAndUnknownStringsAreLeftAlone();
};

// The string should read in the order the engine resolves it: the plain
// appearance first, then what a width changes, then what a state changes.
// Specificity is the axis loomSpecificity() already ranks on, so there is one
// notion of order in the system rather than two that can disagree.
void StyleOrderTests::unconditionalClassesComeBeforeConditionalOnes()
{
    QCOMPARE(
        canonicalOrder(QStringLiteral("hover:bg-blue-600 md:p-6 bg-surface")),
        QStringLiteral("bg-surface md:p-6 hover:bg-blue-600"));

    // The same with an extra unconditional class, which is the shape a real
    // string has.
    QCOMPARE(
        canonicalOrder(
            QStringLiteral("hover:bg-blue-600 md:p-6 bg-surface rounded-lg")),
        QStringLiteral("bg-surface rounded-lg md:p-6 hover:bg-blue-600"));
}

void StyleOrderTests::orderingIsIdempotent()
{
    const QStringList inputs{
        QStringLiteral("hover:bg-blue-600 md:p-6 bg-surface rounded-lg"),
        QStringLiteral("p-4 px-6 text-sm"),
        QStringLiteral("bg-surface"),
        QStringLiteral(""),
    };
    for (const QString &input : inputs) {
        const QString once = canonicalOrder(input);
        QCOMPARE(canonicalOrder(once), once);
    }
}

// The property that makes this safe to run over someone's source. compile()
// gives later classes the win at equal specificity, so reordering can change
// what a string paints -- and a formatter that silently repaints the UI is
// worse than no formatter.
void StyleOrderTests::orderingNeverChangesWhatAStringCompilesTo()
{
    const QStringList inputs{
        QStringLiteral("hover:bg-blue-600 md:p-6 bg-surface rounded-lg"),
        QStringLiteral("p-4 p-6"),
        QStringLiteral("p-4 px-6"),
        QStringLiteral("px-6 p-4"),
        QStringLiteral("size-4 w-8"),
        QStringLiteral("w-8 size-4"),
        QStringLiteral("bg-red-500 hover:bg-blue-500 md:bg-green-500 bg-slate-100"),
        QStringLiteral("text-sm font-bold italic underline tracking-wide"),
    };

    // compile() keeps rules in source order and the engine picks a winner per
    // condition slot at apply time, by specificity and then by position. So
    // comparing the rule vectors element for element would fail on reorderings
    // that mean exactly the same thing -- what has to match is who *wins*.
    //
    // Computed here with a deliberately coarser key than the implementation
    // uses, and a different formulation, so this is a check on that code
    // rather than a restatement of it.
    const auto winners = [](const QString &style) {
        const auto compiled = LoomStyleCompiler::compile(style);
        QMap<QString, QString> result;
        QMap<QString, quint64> best;
        for (qsizetype i = 0; i < compiled->rules.size(); ++i) {
            const auto &rule = compiled->rules.at(i);
            const QString slot = QStringLiteral("%1|%2|%3|%4")
                                     .arg(int(rule.utility))
                                     .arg(rule.stateMask)
                                     .arg(rule.minWidth)
                                     .arg(rule.maxWidth);
            if (!best.contains(slot) || rule.specificity >= best.value(slot)) {
                best[slot] = rule.specificity;
                result[slot] = QStringLiteral("%1/%2/%3")
                                   .arg(rule.key)
                                   .arg(rule.literal, 0, 'g', 17)
                                   .arg(rule.alphaPercent);
            }
        }
        return result;
    };

    for (const QString &input : inputs) {
        const QString sorted = canonicalOrder(input);
        QCOMPARE(
            LoomStyleCompiler::compile(sorted)->rules.size(),
            LoomStyleCompiler::compile(input)->rules.size());
        QCOMPARE(winners(sorted), winners(input));
    }
}

void StyleOrderTests::ambiguousAndUnknownStringsAreLeftAlone()
{
    // Ordering by rank would put p-4 first, which would hand the horizontal
    // sides to px-6 instead of the other way round. Rather than reason about
    // which overlaps are safe, the sorter compiles both and keeps the original
    // when they differ.
    QCOMPARE(
        canonicalOrder(QStringLiteral("px-6 p-4")), QStringLiteral("px-6 p-4"));

    // An unknown class has no rank. Moving it somewhere arbitrary in a string
    // its author is still editing is the worst possible moment to do it.
    QCOMPARE(
        canonicalOrder(QStringLiteral("hover:bg-blue-600 bg-nonsense bg-surface")),
        QStringLiteral("hover:bg-blue-600 bg-nonsense bg-surface"));

    // Nothing to do.
    QCOMPARE(canonicalOrder(QStringLiteral("bg-surface")), QStringLiteral("bg-surface"));
    QCOMPARE(canonicalOrder(QString()), QString());
}

QTEST_MAIN(StyleOrderTests)
#include "tst_styleorder.moc"
