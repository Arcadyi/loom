#include <QtTest>

#include "style/loomstylecompiler.h"
#include "tokens/loomtokenregistry.h"

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
    void specificityAxes();
    void textDisambiguation();
    void unknownClassesWarnAndSkip();
    void cacheSharesInstances();
    void transitions();
    void flagsAreAggregated();
    void alphaModifier();
    void alphaModifierRejections();
    void borderWidthRejections();
    void visibilityUtilitiesKeepTheirDistinctSemantics();
    void layoutClasses();
    void layoutRejections();
    void aspectRatioTakesASlash();
    void expandedTypography();
    void arbitraryValuesFractionsAndNegatives();
    void transformsRingsGradientsAndFilters();
    void expandedVariants();
    void styleRecipes();
    void specificityDepthSaturatesRatherThanWrapping();
    void declaredStatesCompileAndRankAsStates();
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
    QCOMPARE(compiled->rules.at(0).stateMask, quint32(0));
    QCOMPARE(compiled->rules.at(0).specificity, quint64(0));

    QCOMPARE(compiled->rules.at(1).minBreakpoint, quint8(2));
    QCOMPARE(compiled->rules.at(1).specificity, loomSpecificity(2, 0));

    QCOMPARE(compiled->rules.at(2).stateMask, quint32(LoomHoverState | LoomDarkState));
    QCOMPARE(
        compiled->rules.at(2).specificity,
        loomSpecificity(0, LoomHoverState | LoomDarkState));

    // Prefix order does not matter.
    const auto swapped = compile(QStringLiteral("dark:hover:bg-blue-500"));
    QCOMPARE(swapped->rules.first().stateMask, quint32(LoomHoverState | LoomDarkState));
}

void CompilerTests::specificityAxes()
{
    // The regression this ranking exists for: a state variant and a breakpoint
    // variant both used to count as "one prefix", so whichever was written last
    // won. With `md:` last, no `hover:` rule could ever apply above 768px.
    QVERIFY(loomSpecificity(0, LoomHoverState) > loomSpecificity(4, 0));

    // A state plus a breakpoint still beats the state alone.
    QVERIFY(loomSpecificity(2, LoomHoverState) > loomSpecificity(0, LoomHoverState));

    // Two states beat one, and any variant beats none.
    QVERIFY(
        loomSpecificity(0, LoomHoverState | LoomDarkState)
        > loomSpecificity(0, LoomHoverState));
    QVERIFY(loomSpecificity(1, 0) > loomSpecificity(0, 0));

    // Rules carry the rank the ordering above is computed from.
    const auto compiled =
        compile(QStringLiteral("bg-white hover:bg-black md:bg-blue-500"));
    QCOMPARE(compiled->rules.size(), 3);
    QVERIFY(compiled->rules.at(1).specificity > compiled->rules.at(2).specificity);

    // Responsive specificity is derived from exact constraints, not from the
    // old four-tier enum. Container and arbitrary variants must both beat the
    // base rule, and a narrower min-width condition wins when both match.
    const auto responsive = compile(QStringLiteral(
        "bg-white @md:bg-black min-[900px]:bg-blue-500 "
        "min-[1200px]:bg-red-500"));
    QCOMPARE(responsive->rules.size(), 4);
    QVERIFY(responsive->rules.at(1).specificity > responsive->rules.at(0).specificity);
    QVERIFY(responsive->rules.at(2).specificity > responsive->rules.at(0).specificity);
    QVERIFY(responsive->rules.at(3).specificity > responsive->rules.at(2).specificity);

    const auto grouped =
        compile(QStringLiteral("hover:bg-black hover:group-hover:bg-blue-500"));
    QCOMPARE(grouped->rules.size(), 2);
    QVERIFY(grouped->rules.at(1).specificity > grouped->rules.at(0).specificity);
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

// `border-{n}` went straight to QString::toDouble, which also accepts "nan",
// "inf" and negatives -- each of which would have been written into the
// target's border width verbatim.
void CompilerTests::borderWidthRejections()
{
    for (const QString &klass :
         {QStringLiteral("border--3"), QStringLiteral("border-nan"),
          QStringLiteral("border-inf")}) {
        QCOMPARE(LoomStyleCompiler::unknownClasses(klass), QStringList{klass});
    }

    // Valid widths still compile, including a zero and a fractional one.
    for (const QString &klass :
         {QStringLiteral("border-0"), QStringLiteral("border-2"),
          QStringLiteral("border-1.5")}) {
        QVERIFY2(LoomStyleCompiler::unknownClasses(klass).isEmpty(), qPrintable(klass));
    }
}

void CompilerTests::visibilityUtilitiesKeepTheirDistinctSemantics()
{
    const auto hidden = compile(QStringLiteral("hidden"));
    QCOMPARE(hidden->rules.size(), 1);
    QCOMPARE(hidden->rules.first().utility, LoomUtility::Visible);
    QCOMPARE(hidden->rules.first().flag, false);

    const auto invisible = compile(QStringLiteral("invisible"));
    QCOMPARE(invisible->rules.size(), 1);
    QCOMPARE(invisible->rules.first().utility, LoomUtility::Opacity);
    QCOMPARE(invisible->rules.first().literal, 0.0);
    QVERIFY(invisible->rules.first().arbitrary);
}

void CompilerTests::layoutClasses()
{
    // Each of these is one rule. `fill` expands to two *writes* inside a
    // layout, but that happens at apply time, where the parent is known --
    // the compiler cannot see it.
    const auto expect = [](const char *klass, LoomUtility utility) {
        const auto compiled = compile(QLatin1String(klass));
        QCOMPARE(compiled->rules.size(), 1);
        QCOMPARE(compiled->rules.first().utility, utility);
        QVERIFY(compiled->usesLayout);
    };
    expect("fill", LoomUtility::AnchorFill);
    expect("fill-x", LoomUtility::AnchorFillX);
    expect("fill-y", LoomUtility::AnchorFillY);
    expect("center", LoomUtility::AnchorCenter);
    expect("center-x", LoomUtility::AnchorCenterX);
    expect("pin-t", LoomUtility::AnchorPinTop);
    expect("pin-l", LoomUtility::AnchorPinLeft);
    expect("self-center", LoomUtility::LayoutAlignment);
    expect("min-w-16", LoomUtility::LayoutMinWidth);
    expect("max-h-64", LoomUtility::LayoutMaxHeight);
    expect("col-span-2", LoomUtility::LayoutColumnSpan);
    expect("aspect-square", LoomUtility::AspectRatio);

    QCOMPARE(
        compile(QStringLiteral("self-center"))->rules.first().literal,
        double(Qt::AlignCenter));
    QCOMPARE(compile(QStringLiteral("col-span-3"))->rules.first().literal, 3.0);
    QCOMPARE(
        compile(QStringLiteral("min-w-16"))->rules.first().key, QStringLiteral("16"));

    // Only aspect-* needs the item's own width tracked.
    QVERIFY(compile(QStringLiteral("aspect-video"))->usesAspect);
    QVERIFY(!compile(QStringLiteral("fill"))->usesAspect);
}

void CompilerTests::layoutRejections()
{
    for (const QString &klass :
         {QStringLiteral("pin-x"), QStringLiteral("self-middle"),
          QStringLiteral("min-w-nope"), QStringLiteral("col-span-0"),
          QStringLiteral("col-span-x"), QStringLiteral("fill-z"),
          QStringLiteral("aspect-0/9"), QStringLiteral("aspect-16/0")}) {
        QCOMPARE(LoomStyleCompiler::unknownClasses(klass), QStringList{klass});
    }
}

// The colour-opacity modifier used to be split off before any utility matcher
// ran, so a slash could never be part of a class name: `aspect-16/9` parsed as
// `aspect-16` with alpha 9 and was rejected. The whole name is tried first now.
void CompilerTests::aspectRatioTakesASlash()
{
    const auto compiled = compile(QStringLiteral("aspect-16/9"));
    QCOMPARE(compiled->rules.size(), 1);
    QCOMPARE(compiled->rules.first().utility, LoomUtility::AspectRatio);
    QVERIFY(qAbs(compiled->rules.first().literal - 16.0 / 9.0) < 1e-9);

    // And the modifier still works, still only on colours, and still rejects
    // an out-of-range value.
    QCOMPARE(compile(QStringLiteral("bg-surface/70"))->rules.first().alphaPercent, 70);
    QVERIFY(LoomStyleCompiler::unknownClasses(QStringLiteral("bg-surface/70")).isEmpty());
    for (const QString &klass :
         {QStringLiteral("w-full/70"), QStringLiteral("p-4/70"),
          QStringLiteral("bg-surface/101"), QStringLiteral("bg-surface/half")}) {
        QCOMPARE(LoomStyleCompiler::unknownClasses(klass), QStringList{klass});
    }
}

void CompilerTests::unknownClassesWarnAndSkip()
{
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("unknown utility class")));
    const auto compiled = compile(QStringLiteral("bg-blue-500 florp-9000"));
    QCOMPARE(compiled->rules.size(), 1);

    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("unknown utility class")));
    const auto badVariant = compile(QStringLiteral("sideways:bg-blue-500"));
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

    const auto whitespace = compile(QStringLiteral("p-4\tbg-blue-500\nrounded-lg"));
    QCOMPARE(whitespace->rules.size(), 6);
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
    QCOMPARE(plain->usedStates, quint32(0));
    QVERIFY(!plain->usesBreakpoints);
    QVERIFY(!plain->usesParentSize);

    const auto rich = compile(QStringLiteral("hover:bg-white md:p-4 w-full"));
    QCOMPARE(rich->usedStates, quint32(LoomHoverState));
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

void CompilerTests::expandedTypography()
{
    const auto family = compile(QStringLiteral("font-sans"));
    QCOMPARE(family->rules.first().utility, LoomUtility::FontFamily);

    const auto arbitraryFamily = compile(QStringLiteral("font-[IBM_Plex_Sans]"));
    QCOMPARE(arbitraryFamily->rules.first().utility, LoomUtility::FontFamily);
    QCOMPARE(arbitraryFamily->rules.first().key, QStringLiteral("IBM Plex Sans"));
    QVERIFY(arbitraryFamily->rules.first().arbitrary);

    QCOMPARE(
        compile(QStringLiteral("text-center"))->rules.first().utility,
        LoomUtility::TextAlignment);
    const auto truncate = compile(QStringLiteral("truncate"));
    QCOMPARE(truncate->rules.size(), 2);
    QCOMPARE(truncate->rules.at(0).utility, LoomUtility::TextElide);
    QCOMPARE(truncate->rules.at(1).utility, LoomUtility::TextWrapMode);

    const auto clamp = compile(QStringLiteral("line-clamp-3"));
    QCOMPARE(clamp->rules.first().utility, LoomUtility::TextMaximumLines);
    QCOMPARE(clamp->rules.first().literal, 3.0);
    QCOMPARE(
        compile(QStringLiteral("uppercase"))->rules.first().utility,
        LoomUtility::TextCapitalization);
    QCOMPARE(
        compile(QStringLiteral("whitespace-nowrap"))->rules.first().utility,
        LoomUtility::TextWrapMode);

    const auto leading = compile(QStringLiteral("leading-[27px]"));
    QCOMPARE(leading->rules.first().utility, LoomUtility::LineHeight);
    QCOMPARE(leading->rules.first().literal, 27.0);
    QVERIFY(leading->rules.first().arbitrary);

    const auto textSize = compile(QStringLiteral("text-[17px]"));
    QCOMPARE(textSize->rules.first().utility, LoomUtility::TextSize);
    QCOMPARE(textSize->rules.first().literal, 17.0);
    QVERIFY(textSize->rules.first().arbitrary);
}

void CompilerTests::arbitraryValuesFractionsAndNegatives()
{
    const auto padding = compile(QStringLiteral("p-[13px]"));
    QCOMPARE(padding->rules.size(), 4);
    for (const auto &rule : padding->rules) {
        QCOMPARE(rule.literal, 13.0);
        QVERIFY(rule.arbitrary);
    }

    for (const QString &klass :
         {QStringLiteral("bg-[#7c5cff]"), QStringLiteral("text-[#010203]"),
          QStringLiteral("border-[#abcdef]")}) {
        const auto color = compile(klass);
        QCOMPARE(color->rules.size(), 1);
        QVERIFY2(color->rules.first().arbitrary, qPrintable(klass));
        QVERIFY(QColor::fromString(color->rules.first().key).isValid());
    }

    const auto border = compile(QStringLiteral("border-[1.5px]"));
    QCOMPARE(border->rules.first().utility, LoomUtility::BorderWidth);
    QCOMPARE(border->rules.first().literal, 1.5);
    QVERIFY(border->rules.first().arbitrary);

    const auto radius = compile(QStringLiteral("rounded-t-[7px]"));
    QCOMPARE(radius->rules.size(), 2);
    QVERIFY(radius->rules.at(0).arbitrary);
    QCOMPARE(radius->rules.at(0).literal, 7.0);

    const auto gap = compile(QStringLiteral("gap-[11px]"));
    QCOMPARE(gap->rules.first().utility, LoomUtility::Gap);
    QCOMPARE(gap->rules.first().literal, 11.0);

    const auto size = compile(QStringLiteral("size-[29px]"));
    QCOMPARE(size->rules.size(), 2);
    QCOMPARE(size->rules.at(0).literal, 29.0);
    QCOMPARE(size->rules.at(1).literal, 29.0);

    const auto half = compile(QStringLiteral("w-1/2"));
    QCOMPARE(half->rules.first().utility, LoomUtility::Width);
    QCOMPARE(half->rules.first().fraction, 0.5);
    QVERIFY(half->usesParentSize);

    const auto negative = compile(QStringLiteral("-mt-4"));
    QCOMPARE(negative->rules.first().utility, LoomUtility::MarginTop);
    QVERIFY(negative->rules.first().negative);

    const auto opacity = compile(QStringLiteral("opacity-[0.42]"));
    QCOMPARE(opacity->rules.first().literal, 0.42);
    QVERIFY(opacity->rules.first().arbitrary);
}

void CompilerTests::transformsRingsGradientsAndFilters()
{
    const auto transformed = compile(QStringLiteral(
        "rotate-45 -translate-x-1/2 scale-110 origin-bottom-right cursor-pointer"));
    QCOMPARE(transformed->rules.size(), 5);
    QVERIFY(transformed->usesTranslate);
    QVERIFY(transformed->usesCursor);
    QCOMPARE(transformed->rules.at(0).utility, LoomUtility::Rotation);
    QCOMPARE(transformed->rules.at(1).utility, LoomUtility::TranslateX);
    QVERIFY(transformed->rules.at(1).negative);
    QCOMPARE(transformed->rules.at(1).fraction, 0.5);
    QCOMPARE(transformed->rules.at(2).utility, LoomUtility::Scale);
    QCOMPARE(transformed->rules.at(2).literal, 1.1);

    const auto visual = compile(QStringLiteral(
        "ring-4 ring-accent bg-linear-to-tr from-blue-500 via-violet-500 "
        "to-red-500 blur-md brightness-125 contrast-75 saturate-150"));
    QCOMPARE(visual->rules.size(), 10);
    QVERIFY(visual->usesEffects);
    QCOMPARE(visual->rules.at(0).utility, LoomUtility::RingWidth);
    QCOMPARE(visual->rules.at(1).utility, LoomUtility::RingColor);
    QCOMPARE(visual->rules.at(2).utility, LoomUtility::GradientDirection);
    QCOMPARE(visual->rules.at(6).utility, LoomUtility::FilterBlur);
    QCOMPARE(visual->rules.at(9).utility, LoomUtility::FilterSaturation);
}

void CompilerTests::expandedVariants()
{
    auto *registry = LoomTokenRegistry::instance();
    const auto viewport =
        compile(QStringLiteral("max-md:bg-red-500 min-[900px]:bg-blue-500"));
    QCOMPARE(viewport->rules.size(), 2);
    QCOMPARE(
        viewport->rules.at(0).maxWidth, registry->breakpoint(QStringLiteral("md")) - 1);
    QCOMPARE(viewport->rules.at(1).minWidth, 900);
    QVERIFY(viewport->usesBreakpoints);

    const auto container = compile(QStringLiteral("@md/sidebar:bg-blue-500"));
    QCOMPARE(
        container->rules.first().containerMinWidth,
        registry->container(QStringLiteral("md")));
    QCOMPARE(container->rules.first().containerName, QStringLiteral("sidebar"));
    QVERIFY(container->usesContainers);

    const auto group = compile(QStringLiteral("group-not-disabled/menu:bg-blue-500"));
    QCOMPARE(group->rules.first().groupName, QStringLiteral("menu"));
    QCOMPARE(group->rules.first().groupStateNotMask, quint32(LoomDisabledState));
    QVERIFY(group->usesGroups);

    const auto states = compile(
        QStringLiteral("rtl:not-disabled:focus-visible:high-contrast:bg-blue-500"));
    QCOMPARE(
        states->rules.first().stateMask,
        quint32(LoomRtlState | LoomFocusVisibleState | LoomHighContrastState));
    QCOMPARE(states->rules.first().stateNotMask, quint32(LoomDisabledState));

    const auto themed = compile(QStringLiteral("theme-dark:bg-blue-500"));
    QCOMPARE(themed->rules.first().themeName, QStringLiteral("dark"));
}

void CompilerTests::styleRecipes()
{
    auto *registry = LoomTokenRegistry::instance();
    registry->setStyleRecipe(
        QStringLiteral("chip"), QStringLiteral("px-3 rounded-full bg-accent"));
    registry->setStyleRecipe(QStringLiteral("button"), QStringLiteral("@chip text-sm"));
    LoomStyleCompiler::clearCache();

    const auto recipe = compile(QStringLiteral("hover:@button"));
    QCOMPARE(recipe->rules.size(), 5);
    for (const auto &rule : recipe->rules)
        QCOMPARE(rule.stateMask, quint32(LoomHoverState));

    registry->setStyleRecipe(QStringLiteral("a"), QStringLiteral("@b"));
    registry->setStyleRecipe(QStringLiteral("b"), QStringLiteral("@a"));
    QCOMPARE(
        LoomStyleCompiler::unknownClasses(QStringLiteral("@a")),
        QStringList{QStringLiteral("@a")});

    registry->resetToDefaults();
    LoomStyleCompiler::clearCache();
}

// loomSpecificity packs the state depth into bits 58-63 -- six bits, so 63 is
// the largest value that survives the shift. Before application-declared
// states the worst case was around 53 and the field could not overflow. A rule
// combining many states with a group and a theme now can, and an unclamped
// shift would not saturate: it would wrap, making the *most* specific rule sort
// below an unqualified one. Nothing downstream would report that; the styling
// would just silently come out wrong.
void CompilerTests::specificityDepthSaturatesRatherThanWrapping()
{
    constexpr int unbounded = std::numeric_limits<int>::max();
    const quint64 plain = loomSpecificity(0, unbounded, 0, unbounded, 0);
    const quint64 oneState =
        loomSpecificity(0, unbounded, 0, unbounded, LoomHoverState);
    // Every state bit set, plus a group and a theme: 32 + 60 well past the cap.
    const quint64 saturated =
        loomSpecificity(0, unbounded, 0, unbounded, 0xFFFFFFFFu, false, 60);

    QVERIFY(oneState > plain);
    QVERIFY2(saturated > oneState, "deep specificity wrapped instead of saturating");
    QVERIFY2(saturated > plain, "deep specificity wrapped below an unqualified rule");
}

void CompilerTests::declaredStatesCompileAndRankAsStates()
{
    auto *registry = LoomTokenRegistry::instance();
    QVERIFY(registry->setCustomState(QStringLiteral("invalid"), QString()));
    LoomStyleCompiler::clearCache();

    const auto style = compile(QStringLiteral("bg-blue-500 invalid:bg-red-500"));
    QCOMPARE(style->rules.size(), 2);
    QCOMPARE(style->usedCustomStates, 1u);
    // Sits in its own channel rather than in spare LoomState bits, so the
    // built-in mask stays empty.
    QCOMPARE(style->rules.constLast().customMask, 1u);
    QCOMPARE(style->rules.constLast().stateMask, 0u);
    // And ranks as a state: deeper than the unqualified rule it overrides.
    QVERIFY(style->rules.constLast().specificity > style->rules.constFirst().specificity);

    // The catalogue advertises all three forms, and tst_catalogue's round-trip
    // then covers them without knowing they exist.
    const QStringList variants = LoomStyleCompiler::variantNames();
    QVERIFY(variants.contains(QStringLiteral("invalid")));
    QVERIFY(variants.contains(QStringLiteral("not-invalid")));
    QVERIFY(variants.contains(QStringLiteral("group-invalid")));

    registry->resetToDefaults();
    LoomStyleCompiler::clearCache();
}

QTEST_APPLESS_MAIN(CompilerTests)
#include "tst_compiler.moc"
