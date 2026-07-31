#include <QtTest>

#include "style/loomstylecompiler.h"

using LoomStyleCompiler::compile;

class CompilerTests : public QObject {
    Q_OBJECT

private slots:
    void init();
    void singleUtilities_data();
    void singleUtilities();
    void sideExpansion();
    void cornerExpansion();
    void variants();
    void textDisambiguation();
    void unknownClassesWarnAndSkip();
    void cacheSharesInstances();
    void transitions();
    void flagsAreAggregated();
    void alphaModifier();
    void alphaModifierRejections();
};

void CompilerTests::init()
{
    LoomStyleCompiler::clearCache();
}

void CompilerTests::singleUtilities_data()
{
    QTest::addColumn<QString>("style");
    QTest::addColumn<int>("utility");
    QTest::addColumn<QString>("key");

    QTest::newRow("bg palette")
        << "bg-blue-500" << int(LoomUtility::BgColor) << "blue-500";
    QTest::newRow("bg semantic")
        << "bg-surface" << int(LoomUtility::BgColor) << "surface";
    QTest::newRow("bg white") << "bg-white" << int(LoomUtility::BgColor) << "white";
    QTest::newRow("text color")
        << "text-red-600" << int(LoomUtility::TextColor) << "red-600";
    QTest::newRow("text size 2xl") << "text-2xl" << int(LoomUtility::TextSize) << "2xl";
    QTest::newRow("font weight") << "font-bold" << int(LoomUtility::FontWeight) << "bold";
    QTest::newRow("tracking") << "tracking-wide" << int(LoomUtility::Tracking) << "wide";
    QTest::newRow("rounded bare") << "rounded" << int(LoomUtility::Radius) << "base";
    QTest::newRow("rounded lg") << "rounded-lg" << int(LoomUtility::Radius) << "lg";
    QTest::newRow("rounded full") << "rounded-full" << int(LoomUtility::Radius) << "full";
    QTest::newRow("opacity") << "opacity-50" << int(LoomUtility::Opacity) << "50";
    QTest::newRow("shadow bare") << "shadow" << int(LoomUtility::Shadow) << "base";
    QTest::newRow("shadow md") << "shadow-md" << int(LoomUtility::Shadow) << "md";
    QTest::newRow("gap") << "gap-2" << int(LoomUtility::Gap) << "2";
    QTest::newRow("width") << "w-64" << int(LoomUtility::Width) << "64";
    QTest::newRow("height half key") << "h-0.5" << int(LoomUtility::Height) << "0.5";
    QTest::newRow("border color")
        << "border-slate-200" << int(LoomUtility::BorderColor) << "slate-200";
}

void CompilerTests::singleUtilities()
{
    QFETCH(QString, style);
    QFETCH(int, utility);
    QFETCH(QString, key);

    const auto compiled = compile(style);
    QCOMPARE(compiled->rules.size(), 1);
    QCOMPARE(int(compiled->rules.first().utility), utility);
    QCOMPARE(compiled->rules.first().key, key);
}

void CompilerTests::sideExpansion()
{
    const auto all = compile(QStringLiteral("p-4"));
    QCOMPARE(all->rules.size(), 4);
    for (const auto &rule : all->rules)
        QCOMPARE(rule.key, QStringLiteral("4"));

    const auto horizontal = compile(QStringLiteral("px-6"));
    QCOMPARE(horizontal->rules.size(), 2);
    QCOMPARE(horizontal->rules.at(0).utility, LoomUtility::PaddingRight);
    QCOMPARE(horizontal->rules.at(1).utility, LoomUtility::PaddingLeft);

    const auto top = compile(QStringLiteral("mt-2"));
    QCOMPARE(top->rules.size(), 1);
    QCOMPARE(top->rules.first().utility, LoomUtility::MarginTop);

    const auto size = compile(QStringLiteral("size-8"));
    QCOMPARE(size->rules.size(), 2);
    QCOMPARE(size->rules.at(0).utility, LoomUtility::Width);
    QCOMPARE(size->rules.at(1).utility, LoomUtility::Height);

    const auto border = compile(QStringLiteral("border-2"));
    QCOMPARE(border->rules.size(), 1);
    QCOMPARE(border->rules.first().utility, LoomUtility::BorderWidth);
    QCOMPARE(border->rules.first().literal, 2.0);
}

void CompilerTests::cornerExpansion()
{
    const auto top = compile(QStringLiteral("rounded-t-lg"));
    QCOMPARE(top->rules.size(), 2);
    QCOMPARE(top->rules.at(0).utility, LoomUtility::RadiusTopLeft);
    QCOMPARE(top->rules.at(1).utility, LoomUtility::RadiusTopRight);
    QCOMPARE(top->rules.at(0).key, QStringLiteral("lg"));

    const auto corner = compile(QStringLiteral("rounded-br"));
    QCOMPARE(corner->rules.size(), 1);
    QCOMPARE(corner->rules.first().utility, LoomUtility::RadiusBottomRight);
    QCOMPARE(corner->rules.first().key, QStringLiteral("base"));
}

void CompilerTests::variants()
{
    const auto compiled =
        compile(QStringLiteral("bg-white md:bg-black hover:dark:bg-blue-500"));
    QCOMPARE(compiled->rules.size(), 3);

    QCOMPARE(compiled->rules.at(0).minBreakpoint, quint8(0));
    QCOMPARE(compiled->rules.at(0).stateMask, quint8(0));
    QCOMPARE(compiled->rules.at(0).variantCount, quint8(0));

    QCOMPARE(compiled->rules.at(1).minBreakpoint, quint8(2));
    QCOMPARE(compiled->rules.at(1).variantCount, quint8(1));

    QCOMPARE(compiled->rules.at(2).stateMask, quint8(LoomHoverState | LoomDarkState));
    QCOMPARE(compiled->rules.at(2).variantCount, quint8(2));

    // Prefix order does not matter.
    const auto swapped = compile(QStringLiteral("dark:hover:bg-blue-500"));
    QCOMPARE(swapped->rules.first().stateMask, quint8(LoomHoverState | LoomDarkState));
}

void CompilerTests::textDisambiguation()
{
    // "xl" is a size key; sizes win over colors.
    QCOMPARE(
        compile(QStringLiteral("text-xl"))->rules.first().utility, LoomUtility::TextSize);
    QCOMPARE(
        compile(QStringLiteral("text-emerald-300"))->rules.first().utility,
        LoomUtility::TextColor);
    QCOMPARE(
        compile(QStringLiteral("text-foreground"))->rules.first().utility,
        LoomUtility::TextColor);
}

void CompilerTests::unknownClassesWarnAndSkip()
{
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("unknown utility class")));
    const auto compiled = compile(QStringLiteral("bg-blue-500 florp-9000"));
    QCOMPARE(compiled->rules.size(), 1);

    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("unknown utility class")));
    const auto badVariant = compile(QStringLiteral("landscape:bg-blue-500"));
    QCOMPARE(badVariant->rules.size(), 0);

    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("unknown utility class")));
    const auto badKey = compile(QStringLiteral("bg-blurple-500"));
    QCOMPARE(badKey->rules.size(), 0);
}

void CompilerTests::cacheSharesInstances()
{
    const auto first = compile(QStringLiteral("p-4 bg-blue-500"));
    const auto second = compile(QStringLiteral("p-4 bg-blue-500"));
    QCOMPARE(first.get(), second.get());

    const auto different = compile(QStringLiteral("p-4 bg-blue-600"));
    QVERIFY(first.get() != different.get());

    LoomStyleCompiler::clearCache();
    const auto recompiled = compile(QStringLiteral("p-4 bg-blue-500"));
    QVERIFY(first.get() != recompiled.get());
}

void CompilerTests::transitions()
{
    const auto compiled = compile(
        QStringLiteral("transition-colors duration-300 ease-out dark:transition-none"));
    QCOMPARE(compiled->rules.size(), 4);

    QCOMPARE(compiled->rules.at(0).utility, LoomUtility::TransitionMode);
    QCOMPARE(
        LoomTransitionMode(quint8(compiled->rules.at(0).literal)),
        LoomTransitionMode::Colors);

    QCOMPARE(compiled->rules.at(1).utility, LoomUtility::TransitionDuration);
    QCOMPARE(compiled->rules.at(1).key, QStringLiteral("300"));

    QCOMPARE(compiled->rules.at(2).utility, LoomUtility::TransitionEase);
    QCOMPARE(compiled->rules.at(2).key, QStringLiteral("out"));

    QCOMPARE(compiled->rules.at(3).utility, LoomUtility::TransitionMode);
    QCOMPARE(
        LoomTransitionMode(quint8(compiled->rules.at(3).literal)),
        LoomTransitionMode::None);
    QCOMPARE(compiled->rules.at(3).stateMask, quint8(LoomDarkState));

    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("unknown utility class")));
    QCOMPARE(compile(QStringLiteral("duration-42"))->rules.size(), 0);
}

void CompilerTests::flagsAreAggregated()
{
    const auto plain = compile(QStringLiteral("bg-white p-4"));
    QCOMPARE(plain->usedStates, quint8(0));
    QVERIFY(!plain->usesBreakpoints);
    QVERIFY(!plain->usesParentSize);

    const auto rich = compile(QStringLiteral("hover:bg-white md:p-4 w-full"));
    QCOMPARE(rich->usedStates, quint8(LoomHoverState));
    QVERIFY(rich->usesBreakpoints);
    QVERIFY(rich->usesParentSize);
}

void CompilerTests::alphaModifier()
{
    // Tailwind's bg-surface/70.
    const auto compiled = compile(QStringLiteral("bg-surface/70"));
    QCOMPARE(compiled->rules.size(), 1);
    QCOMPARE(compiled->rules.first().utility, LoomUtility::BgColor);
    // The key is still the plain token, so a theme switch re-resolves it.
    QCOMPARE(compiled->rules.first().key, QStringLiteral("surface"));
    QCOMPARE(int(compiled->rules.first().alphaPercent), 70);

    // Every colour family, and composed with variants.
    const auto text = compile(QStringLiteral("text-red-600/50"));
    QCOMPARE(text->rules.first().utility, LoomUtility::TextColor);
    QCOMPARE(int(text->rules.first().alphaPercent), 50);

    const auto border = compile(QStringLiteral("border-outline/25"));
    QCOMPARE(border->rules.first().utility, LoomUtility::BorderColor);
    QCOMPARE(int(border->rules.first().alphaPercent), 25);

    const auto hovered = compile(QStringLiteral("hover:bg-surface/40"));
    QCOMPARE(hovered->rules.size(), 1);
    QCOMPARE(int(hovered->rules.first().alphaPercent), 40);

    // Absent modifier is fully opaque, not zero.
    QCOMPARE(int(compile(QStringLiteral("bg-surface"))->rules.first().alphaPercent), 100);
    QCOMPARE(int(compile(QStringLiteral("bg-surface/0"))->rules.first().alphaPercent), 0);
    QCOMPARE(
        int(compile(QStringLiteral("bg-surface/100"))->rules.first().alphaPercent), 100);
}

void CompilerTests::alphaModifierRejections()
{
    // Out of range, non-numeric, and families with no alpha to modify are
    // reported as unknown rather than silently dropping the modifier.
    for (const QString &klass :
         {QStringLiteral("bg-surface/101"), QStringLiteral("bg-surface/-5"),
          QStringLiteral("bg-surface/half"), QStringLiteral("bg-surface/"),
          QStringLiteral("w-full/70"), QStringLiteral("p-4/70")}) {
        QTest::ignoreMessage(
            QtWarningMsg, QRegularExpression(QStringLiteral("unknown utility class")));
        QCOMPARE(compile(klass)->rules.size(), 0);
    }
}

QTEST_MAIN(CompilerTests)
#include "tst_compiler.moc"
