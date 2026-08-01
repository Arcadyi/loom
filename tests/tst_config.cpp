#include <QtTest>

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
    void themeColourCanAliasAnInheritedSemanticName();
    void unresolvableThemeColourWarnsRatherThanGoingInvalid();
    void reloadClearsAnIconRootTheFileNoLongerDefines();
    void nonMonotonicBreakpointsWarn();

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
    file.write(json);
    return path;
}

void ConfigTests::cleanup()
{
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
        QtWarningMsg, QRegularExpression(QStringLiteral("breakpoints only accepts")));
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("is not wider than")));
    QVERIFY(loom::loadConfig(zero));
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
    const QString path = writeConfig(R"({"colors": {"special": "#010203"}})");
    QVERIFY(loom::loadConfig(path));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "QtObject {\n"
        "    property color special: Loom.color.value(\"special\")\n"
        "    property real custom: Loom.space.value(\"18\")\n"
        "}\n",
        QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("special").value<QColor>(), QColor(0x01, 0x02, 0x03));
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
    const QString path = writeConfig(R"({
        "colors": {"broken": "#zzz"},
        "space": {"neg": -5},
        "breakpoints": {"xxl": 1600},
        "typo": {}
    })");
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("unknown key.*typo")));
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("invalid color.*broken")));
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("space entry.*neg")));
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("breakpoints only accepts")));
    QVERIFY(loom::loadConfig(path));
    QVERIFY(!LoomTokenRegistry::instance()->hasColor(QStringLiteral("broken")));
    QVERIFY(!LoomTokenRegistry::instance()->hasSpace(QStringLiteral("neg")));
}

void ConfigTests::unknownExtendsWarns()
{
    const QString path = writeConfig(R"({
        "themes": {"exotic": {"extends": "solarized", "surface": "#111111"}}
    })");
    QTest::ignoreMessage(
        QtWarningMsg,
        QRegularExpression(QStringLiteral("exotic.*unknown theme.*solarized")));
    QVERIFY(loom::loadConfig(path));
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

QTEST_MAIN(ConfigTests)
#include "tst_config.moc"
