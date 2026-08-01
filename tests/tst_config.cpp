#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <loom/loom.h>

#include "style/loomstylecompiler.h"
#include "tokens/loomtokenregistry.h"

class ConfigTests : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void customTokensAndThemes();
    void customTokensReachUtilityStrings();
    void valueEscapeHatch();
    void breakpointOverride();
    void missingFileFails();
    void malformedJsonFails();
    void badEntriesWarnAndSkip();
    void unknownExtendsWarns();
    void reloadDropsTokensTheFileNoLongerDefines();
    void reloadKeepsTheActiveTheme();
    void failedReloadKeepsThePreviousTokens();
    void reloadRecompilesLiveStyles();
    void themeSpecificTokenIsKnownBeforeThemeActivation();
    void themeColourCanAliasAnInheritedSemanticName();
    void unresolvableThemeColourWarnsRatherThanGoingInvalid();
    void reloadClearsAnIconRootTheFileNoLongerDefines();
    void nonMonotonicBreakpointsWarn();
    void schemaV2LoadsEveryTokenFamilyAndRecipes();
    void schemaV1RequiresMigration();

private:
    QString writeConfig(const char *json);
    QTemporaryDir m_dir;
};

QString ConfigTests::writeConfig(const char *json)
{
    static int counter = 0;
    const QString path = m_dir.filePath(QStringLiteral("config%1.json").arg(counter++));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    QByteArray bytes(json);
    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error == QJsonParseError::NoError && document.isObject()
        && !document.object().contains(QStringLiteral("schemaVersion"))) {
        const QJsonObject old = document.object();
        QJsonObject root{{QStringLiteral("schemaVersion"), 2}};
        QJsonObject tokens;
        for (const auto &family :
             {QStringLiteral("colors"), QStringLiteral("space"),
              QStringLiteral("breakpoints")})
            if (old.contains(family))
                tokens.insert(family, old.value(family));
        if (!tokens.isEmpty())
            root.insert(QStringLiteral("tokens"), tokens);
        QJsonObject themes;
        const QJsonObject oldThemes = old.value(QStringLiteral("themes")).toObject();
        for (auto theme = oldThemes.constBegin(); theme != oldThemes.constEnd();
             ++theme) {
            QJsonObject converted;
            QJsonObject colors;
            const QJsonObject oldTheme = theme->toObject();
            for (auto value = oldTheme.constBegin(); value != oldTheme.constEnd();
                 ++value) {
                if (value.key() == QLatin1String("extends")
                    || value.key() == QLatin1String("dark"))
                    converted.insert(value.key(), value.value());
                else
                    colors.insert(value.key(), value.value());
            }
            converted.insert(
                QStringLiteral("tokens"),
                QJsonObject{{QStringLiteral("colors"), colors}});
            themes.insert(theme.key(), converted);
        }
        if (!themes.isEmpty())
            root.insert(QStringLiteral("themes"), themes);
        root.insert(
            QStringLiteral("theme"),
            QJsonObject{
                {QStringLiteral("default"),
                 old.value(QStringLiteral("defaultTheme"))
                     .toString(QStringLiteral("light"))}});
        if (old.contains(QStringLiteral("iconRoot")))
            root.insert(
                QStringLiteral("iconRoot"), old.value(QStringLiteral("iconRoot")));
        for (auto it = old.constBegin(); it != old.constEnd(); ++it) {
            if (!QStringList{
                    QStringLiteral("colors"), QStringLiteral("space"),
                    QStringLiteral("breakpoints"), QStringLiteral("themes"),
                    QStringLiteral("defaultTheme"), QStringLiteral("iconRoot")}
                     .contains(it.key()))
                root.insert(it.key(), it.value());
        }
        bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    }
    file.write(bytes);
    return path;
}

void ConfigTests::cleanup()
{
    LoomTokenRegistry::instance()->resetToDefaults();
    LoomStyleCompiler::clearCache();
    loom::setTheme(QStringLiteral("light"));
}

// Only the palette was consulted when resolving a theme entry, so naming a
// semantic colour the theme already had produced an invalid QColor in silence.
void ConfigTests::themeColourCanAliasAnInheritedSemanticName()
{
    const QString path = writeConfig(R"({
        "themes": {
            "brandish": { "extends": "light", "accent-hover": "accent" }
        }
    })");
    QVERIFY(loom::loadConfig(path));

    auto *registry = LoomTokenRegistry::instance();
    loom::setTheme(QStringLiteral("brandish"));
    const QColor aliased = registry->color(QStringLiteral("accent-hover"));
    QVERIFY2(
        aliased.isValid(), "aliasing an inherited semantic colour produced no colour");
    QCOMPARE(aliased, registry->color(QStringLiteral("accent")));
}

void ConfigTests::unresolvableThemeColourWarnsRatherThanGoingInvalid()
{
    const QString path = writeConfig(R"({
        "themes": { "broken": { "extends": "light", "surface": "not-a-colour" } }
    })");
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("neither a palette colour")));
    QVERIFY(loom::loadConfig(path));

    // The bad entry is skipped, so the inherited value survives rather than
    // being replaced by an invalid colour.
    loom::setTheme(QStringLiteral("broken"));
    QVERIFY(LoomTokenRegistry::instance()->color(QStringLiteral("surface")).isValid());
}

// A reload replaces rather than merges. iconRoot was the one setting that did
// not: deleting the key left the previous root live for the rest of the run.
void ConfigTests::reloadClearsAnIconRootTheFileNoLongerDefines()
{
    const QString withRoot = writeConfig(R"({ "iconRoot": "icons" })");
    QVERIFY(loom::reloadConfig(withRoot));
    QVERIFY(!loom::iconRoot().isEmpty());

    const QString withoutRoot = writeConfig(R"({ "colors": { "brand": "#7c5cff" } })");
    QVERIFY(loom::reloadConfig(withoutRoot));
    QVERIFY2(
        loom::iconRoot().isEmpty(),
        "a design file that dropped iconRoot left the old root resolving");
}

// The tiers are cumulative min-widths, so a threshold narrower than the one
// before it can never be the widest match and its classes silently do nothing.
void ConfigTests::nonMonotonicBreakpointsWarn()
{
    const QString path = writeConfig(R"({ "breakpoints": { "md": 100 } })");
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("is not wider than")));
    QVERIFY(loom::loadConfig(path));

    // And a non-positive threshold is refused outright.
    const QString zero = writeConfig(R"({ "breakpoints": { "lg": 0 } })");
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("breakpoints.*invalid")));
    QVERIFY(!loom::loadConfig(zero));
    QCOMPARE(LoomTokenRegistry::instance()->breakpoint(QStringLiteral("lg")), 1024);
}

void ConfigTests::customTokensAndThemes()
{
    const QString path = writeConfig(R"({
        "colors": {"brand": {"500": "#7c5cff"}, "highlight": "#ffcc00"},
        "space": {"18": 72},
        "themes": {
            "light": {"surface": "#fefefe"},
            "oled": {"extends": "dark", "surface": "#000000"}
        },
        "defaultTheme": "oled"
    })");
    QVERIFY(loom::loadConfig(path));

    auto *registry = LoomTokenRegistry::instance();
    QCOMPARE(registry->color(QStringLiteral("brand-500")), QColor(0x7c, 0x5c, 0xff));
    QCOMPARE(registry->color(QStringLiteral("highlight")), QColor(0xff, 0xcc, 0x00));
    QCOMPARE(registry->space(QStringLiteral("18")), 72.0);

    // defaultTheme switched to the new theme, which inherits dark's flag and
    // overrides its surface.
    QCOMPARE(loom::theme(), QStringLiteral("oled"));
    QVERIFY(registry->isDark());
    QCOMPARE(registry->color(QStringLiteral("surface")), QColor(Qt::black));

    // The merge into the built-in light theme is visible after switching back.
    loom::setTheme(QStringLiteral("light"));
    QCOMPARE(registry->color(QStringLiteral("surface")), QColor(0xfe, 0xfe, 0xfe));
}

void ConfigTests::customTokensReachUtilityStrings()
{
    // Compile once before the config exists: the cached rejection must not
    // outlive the config load.
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("unknown utility class")));
    auto stale = LoomStyleCompiler::compile(QStringLiteral("bg-punch-500"));
    QCOMPARE(stale->rules.size(), 0);

    const QString path = writeConfig(R"({
        "colors": {"punch": {"500": "#123456"}},
        "space": {"18": 72}
    })");
    QVERIFY(loom::loadConfig(path));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"bg-punch-500 w-18\"\n"
        "}\n",
        QUrl());
    QScopedPointer<QQuickItem> item(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0x12, 0x34, 0x56));
    QCOMPARE(item->width(), 72.0);
}

void ConfigTests::valueEscapeHatch()
{
    const QString path = writeConfig(R"({
        "schemaVersion": 2,
        "tokens": {
            "colors": {"special": "#010203"},
            "space": {"18": 72},
            "radius": {"card": 17},
            "fontWeights": {"book": 350},
            "tracking": {"brand": 0.03}
        }
    })");
    QVERIFY(loom::loadConfig(path));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "QtObject {\n"
        "    property color special: Loom.color.value(\"special\")\n"
        "    property real custom: Loom.space.value(\"18\")\n"
        "    property real cardRadius: Loom.radius.value(\"card\")\n"
        "    property int bookWeight: Loom.text.weight(\"book\")\n"
        "    property real brandTracking: Loom.text.tracking(\"brand\")\n"
        "}\n",
        QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("special").value<QColor>(), QColor(0x01, 0x02, 0x03));
    QCOMPARE(object->property("custom").toReal(), 72.0);
    QCOMPARE(object->property("cardRadius").toReal(), 17.0);
    QCOMPARE(object->property("bookWeight").toInt(), 350);
    QCOMPARE(object->property("brandTracking").toReal(), 0.03);
}

void ConfigTests::breakpointOverride()
{
    const QString path = writeConfig(R"({"breakpoints": {"md": 800}})");
    QVERIFY(loom::loadConfig(path));
    QCOMPARE(LoomTokenRegistry::instance()->breakpoint(QStringLiteral("md")), 800);

    // Restore for other tests; overrides are process-global.
    const QString restore = writeConfig(R"({"breakpoints": {"md": 768}})");
    QVERIFY(loom::loadConfig(restore));
}

void ConfigTests::missingFileFails()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("cannot open")));
    QVERIFY(!loom::loadConfig(m_dir.filePath(QStringLiteral("nope.json"))));
}

void ConfigTests::malformedJsonFails()
{
    const QString path = writeConfig("{not json");
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("not a JSON object")));
    QVERIFY(!loom::loadConfig(path));
}

void ConfigTests::badEntriesWarnAndSkip()
{
    const QString good = writeConfig(R"({ "colors": { "kept": "#123456" } })");
    QVERIFY(loom::reloadConfig(good));
    const QString path = writeConfig(R"({
        "colors": {"broken": "#zzz"},
        "space": {"neg": -5},
        "breakpoints": {"xxl": 1600},
        "typo": {}
    })");
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("typo.*unknown top-level")));
    QVERIFY(!loom::reloadConfig(path));
    QVERIFY(LoomTokenRegistry::instance()->hasColor(QStringLiteral("kept")));
    QVERIFY(!LoomTokenRegistry::instance()->hasColor(QStringLiteral("broken")));
    QVERIFY(!LoomTokenRegistry::instance()->hasSpace(QStringLiteral("neg")));
}

void ConfigTests::unknownExtendsWarns()
{
    const QString path = writeConfig(R"({
        "themes": {"exotic": {"extends": "solarized", "surface": "#111111"}}
    })");
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("exotic.*extends.*existing")));
    QVERIFY(!loom::loadConfig(path));
    QVERIFY(
        !LoomTokenRegistry::instance()->themeNames().contains(QStringLiteral("exotic")));
}

// loadConfig only ever adds, so `loom dev` re-applying an edited design file
// would keep tokens the edit deleted. reloadConfig resets to the built-ins
// first.
void ConfigTests::reloadDropsTokensTheFileNoLongerDefines()
{
    auto *registry = LoomTokenRegistry::instance();
    const QString before = writeConfig(R"({
        "colors": {"brand": "#7c5cff", "legacy": "#ff0000"},
        "space": {"18": 72}
    })");
    QVERIFY(loom::loadConfig(before));
    QVERIFY(registry->hasColor(QStringLiteral("legacy")));
    QVERIFY(registry->hasSpace(QStringLiteral("18")));

    // The same file with "legacy" and the custom space step removed.
    const QString after = writeConfig(R"({"colors": {"brand": "#7c5cff"}})");
    QVERIFY(loom::reloadConfig(after));
    QVERIFY2(
        registry->hasColor(QStringLiteral("brand")),
        "reload dropped a token the file still defines");
    QVERIFY2(
        !registry->hasColor(QStringLiteral("legacy")),
        "reload kept a color the edited file no longer defines");
    QVERIFY2(
        !registry->hasSpace(QStringLiteral("18")),
        "reload kept a space step the edited file no longer defines");

    // The built-ins survive the reset that removed the config's own tokens.
    QVERIFY(registry->hasColor(QStringLiteral("blue-500")));
    QCOMPARE(registry->space(QStringLiteral("4")), 16.0);
}

// Resetting the registry must not throw the user back to "light" mid-session.
void ConfigTests::reloadKeepsTheActiveTheme()
{
    auto *registry = LoomTokenRegistry::instance();
    const QString path = writeConfig(R"({
        "themes": {"oled": {"extends": "dark", "surface": "#000000"}}
    })");
    QVERIFY(loom::loadConfig(path));
    loom::setTheme(QStringLiteral("oled"));
    QCOMPARE(loom::theme(), QStringLiteral("oled"));

    QVERIFY(loom::reloadConfig(path));
    QCOMPARE(loom::theme(), QStringLiteral("oled"));

    // A theme only the previous file defined is gone after a reload without it,
    // so the active theme falls back to one that still exists rather than
    // naming a theme the registry no longer has.
    const QString without = writeConfig(R"({"colors": {"brand": "#7c5cff"}})");
    QVERIFY(loom::reloadConfig(without));
    QVERIFY(!registry->themeNames().contains(QStringLiteral("oled")));
    QVERIFY(registry->themeNames().contains(loom::theme()));
}

// A syntax error mid-keystroke must not strip the application down to the
// built-in tokens: keeping the last good set is the better failure.
void ConfigTests::failedReloadKeepsThePreviousTokens()
{
    auto *registry = LoomTokenRegistry::instance();
    const QString good = writeConfig(R"({"colors": {"brand": "#7c5cff"}})");
    QVERIFY(loom::loadConfig(good));
    QVERIFY(registry->hasColor(QStringLiteral("brand")));

    const QString broken = writeConfig(R"({"colors": {"brand": )");
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("not a JSON object")));
    QVERIFY(!loom::reloadConfig(broken));
    QVERIFY2(
        registry->hasColor(QStringLiteral("brand")),
        "a failed reload reset the tokens instead of keeping the last good set");

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("cannot open")));
    QVERIFY(!loom::reloadConfig(m_dir.filePath(QStringLiteral("nope.json"))));
    QVERIFY(registry->hasColor(QStringLiteral("brand")));
}

// The case that motivated vocabularyChanged: an item already on screen whose
// Lo.style names a token the config has just introduced. Its compiled style was
// produced when that token did not exist, so it dropped the rule -- re-applying
// the same compiled object would faithfully re-apply the gap.
void ConfigTests::reloadRecompilesLiveStyles()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "Rectangle { width: 10; height: 10; Lo.style: \"bg-brand\" }",
        QUrl(QStringLiteral("qrc:/tst_config/live.qml")));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    auto *item = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(item);

    // bg-brand does not resolve yet, so the rectangle keeps its own color.
    QVERIFY(item->property("color").value<QColor>() != QColor(0x7c, 0x5c, 0xff));

    const QString path = writeConfig(R"({"colors": {"brand": "#7c5cff"}})");
    QVERIFY(loom::reloadConfig(path));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0x7c, 0x5c, 0xff));

    // And the reverse: dropping the token stops the rule resolving again.
    const QString without = writeConfig(R"({"colors": {"other": "#123456"}})");
    QVERIFY(loom::reloadConfig(without));
    QTRY_VERIFY(item->property("color").value<QColor>() != QColor(0x7c, 0x5c, 0xff));
}

// Theme-local tokens are part of the design vocabulary even while another
// theme is active. Otherwise `theme-neon:rounded-flare` is discarded during
// compilation and can never start working when the theme later changes.
void ConfigTests::themeSpecificTokenIsKnownBeforeThemeActivation()
{
    const QString path = writeConfig(R"({
        "schemaVersion": 2,
        "themes": {
            "neon": {
                "extends": "light",
                "tokens": {"radius": {"flare": 21}}
            }
        },
        "theme": {"default": "light"}
    })");
    QVERIFY(loom::loadConfig(path));
    QCOMPARE(loom::theme(), QStringLiteral("light"));

    const auto compiled =
        LoomStyleCompiler::compile(QStringLiteral("theme-neon:rounded-flare"));
    QCOMPARE(compiled->rules.size(), 1);
    QVERIFY(
        LoomTokenRegistry::instance()->radiusKeys().contains(QStringLiteral("flare")));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "Rectangle { width: 10; height: 10; Lo.style: "
        "\"theme-neon:rounded-flare\" }",
        QUrl(QStringLiteral("qrc:/tst_config/theme-token.qml")));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
    QCOMPARE(object->property("radius").toReal(), 0.0);

    loom::setTheme(QStringLiteral("neon"));
    QTRY_COMPARE(object->property("radius").toReal(), 21.0);

    loom::setTheme(QStringLiteral("light"));
    QTRY_COMPARE(object->property("radius").toReal(), 0.0);
}

void ConfigTests::schemaV2LoadsEveryTokenFamilyAndRecipes()
{
    const QString path = writeConfig(R"({
        "$schema": "https://example.test/design-v2.schema.json",
        "schemaVersion": 2,
        "tokens": {
            "colors": {"brand": "#7c5cff"},
            "space": {"18": 72},
            "textSizes": {"display": {"size": 40, "lineHeight": 48}},
            "fontWeights": {"book": 350},
            "fontFamilies": {"brand": ["Inter", "Sans Serif"]},
            "tracking": {"brand": 0.03},
            "radius": {"card": 14},
            "shadows": {"card": {
                "color": "#33000000", "offsetX": 1, "offsetY": 3,
                "blur": 12, "spread": 2
            }},
            "opacity": {"quiet": 0.42},
            "durations": {"deliberate": 420},
            "easings": {"springy": [0.2, 0.8, 0.3, 1]},
            "breakpoints": {"3xl": 1920},
            "containers": {"prose": 680}
        },
        "themes": {
            "brand": {
                "extends": "light",
                "tokens": {
                    "colors": {"accent": "brand"},
                    "space": {"18": 80},
                    "textSizes": {"display": {"size": 44, "lineHeight": 52}},
                    "fontWeights": {"book": 375},
                    "fontFamilies": {"brand": "Brand Sans"},
                    "tracking": {"brand": 0.04},
                    "radius": {"card": 18},
                    "shadows": {"card": {
                        "color": "#44000000", "offsetX": 0, "offsetY": 5,
                        "blur": 18, "spread": 1
                    }},
                    "opacity": {"quiet": 0.5},
                    "durations": {"deliberate": 500},
                    "easings": {"springy": [0.1, 0.9, 0.2, 1]}
                }
            }
        },
        "theme": {"default": "brand", "light": "light", "dark": "dark"},
        "styles": {"card": "p-18 rounded-card bg-surface shadow-card"},
        "lint": {"arbitraryValues": "deny"}
    })");
    QVERIFY(loom::loadConfig(path));

    auto *registry = LoomTokenRegistry::instance();
    QCOMPARE(loom::theme(), QStringLiteral("brand"));
    QCOMPARE(registry->color(QStringLiteral("accent")), QColor(0x7c, 0x5c, 0xff));
    QCOMPARE(registry->space(QStringLiteral("18")), 80.0);
    QCOMPARE(registry->textSize(QStringLiteral("display")).size, 44.0);
    QCOMPARE(registry->textSize(QStringLiteral("display")).lineHeight, 52.0);
    QCOMPARE(registry->fontWeight(QStringLiteral("book")), 375);
    QCOMPARE(
        registry->fontFamily(QStringLiteral("brand")),
        QStringList{QStringLiteral("Brand Sans")});
    QCOMPARE(registry->tracking(QStringLiteral("brand")), 0.04);
    QCOMPARE(registry->radius(QStringLiteral("card")), 18.0);
    QCOMPARE(registry->shadow(QStringLiteral("card")).blur, 18.0);
    QCOMPARE(registry->opacityValue(QStringLiteral("quiet")), 0.5);
    QCOMPARE(registry->duration(QStringLiteral("deliberate")), 500);
    QVERIFY(
        registry->easing(QStringLiteral("springy")).type() == QEasingCurve::BezierSpline);
    QCOMPARE(registry->breakpoint(QStringLiteral("3xl")), 1920);
    QCOMPARE(registry->container(QStringLiteral("prose")), 680);
    QCOMPARE(registry->arbitraryValuePolicy(), QStringLiteral("deny"));

    const auto card = LoomStyleCompiler::compile(QStringLiteral("@card"));
    QCOMPARE(card->rules.size(), 7);
    QVERIFY(card->rules.constLast().utility == LoomUtility::Shadow);
}

void ConfigTests::schemaV1RequiresMigration()
{
    const QString path = writeConfig(R"({
        "schemaVersion": 1,
        "colors": {"brand": "#7c5cff"}
    })");
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("schemaVersion.*must be 2")));
    QVERIFY(!loom::loadConfig(path));
    QVERIFY(!LoomTokenRegistry::instance()->hasColor(QStringLiteral("brand")));
}

QTEST_GUILESS_MAIN(ConfigTests)
#include "tst_config.moc"
