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

QTEST_MAIN(ConfigTests)
#include "tst_config.moc"
