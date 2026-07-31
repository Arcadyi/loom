#include <QtTest>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QScopedPointer>
#include <loom/loom.h>

#include "tokens/loomtokenregistry.h"

namespace {

QObject *createFromQml(QQmlComponent &component, const char *body)
{
    const QByteArray document = QByteArray("import QtQuick\nimport Loom\nQtObject {\n")
        + body + QByteArray("\n}\n");
    component.setData(document, QUrl());
    return component.create();
}

} // namespace

class TokenTests : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void cppVersionMatchesProject();
    void singletonIsReachableFromQml();
    void tokensResolveInQml();
    void compositeTokensResolveInQml();
    void themeSwitchReevaluatesBindings();
    void unknownThemeIsRejected();
    void easingTokensAreBezierCurves();
};

void TokenTests::cleanup()
{
    // Theme state is process-global; never leak a dark theme into the next test.
    loom::setTheme(QStringLiteral("light"));
}

void TokenTests::cppVersionMatchesProject()
{
    QCOMPARE(QString::fromLatin1(loom::version()), QStringLiteral(LOOM_VERSION_STR));
}

void TokenTests::singletonIsReachableFromQml()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QObject> object(
        createFromQml(component, "property string v: Loom.version"));
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("v").toString(), QStringLiteral(LOOM_VERSION_STR));
}

void TokenTests::tokensResolveInQml()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QObject> object(createFromQml(
        component,
        "property color blue: Loom.color.blue500\n"
        "property color semantic: Loom.color.surface\n"
        "property real space4: Loom.space.s4\n"
        "property real spaceHalf: Loom.space.s0_5\n"
        "property real radiusLg: Loom.radius.lg\n"
        "property real halfOpacity: Loom.opacity.o50\n"
        "property int quick: Loom.duration.d150\n"
        "property int weight: Loom.text.bold\n"
        "property int bpMd: Loom.breakpoint.md"));
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("blue").value<QColor>(), QColor(0x3b, 0x82, 0xf6));
    QCOMPARE(object->property("semantic").value<QColor>(), QColor(Qt::white));
    QCOMPARE(object->property("space4").toReal(), 16.0);
    QCOMPARE(object->property("spaceHalf").toReal(), 2.0);
    QCOMPARE(object->property("radiusLg").toReal(), 8.0);
    QCOMPARE(object->property("halfOpacity").toReal(), 0.5);
    QCOMPARE(object->property("quick").toInt(), 150);
    QCOMPARE(object->property("weight").toInt(), 700);
    QCOMPARE(object->property("bpMd").toInt(), 768);
}

void TokenTests::compositeTokensResolveInQml()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QObject> object(createFromQml(
        component,
        "property real xlSize: Loom.text.xl.size\n"
        "property real xlLineHeight: Loom.text.xl.lineHeight\n"
        "property real shadowBlur: Loom.shadow.md.blur\n"
        "property color shadowColor: Loom.shadow.md.color"));
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("xlSize").toReal(), 20.0);
    QCOMPARE(object->property("xlLineHeight").toReal(), 28.0);
    QCOMPARE(object->property("shadowBlur").toReal(), 6.0);
    QCOMPARE(object->property("shadowColor").value<QColor>(), QColor(0, 0, 0, 25));
}

void TokenTests::themeSwitchReevaluatesBindings()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QObject> object(createFromQml(
        component,
        "property color surface: Loom.color.surface\n"
        "property bool dark: Loom.dark\n"
        "property string theme: Loom.theme\n"
        "function switchTo(name) { Loom.theme = name; }"));
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("surface").value<QColor>(), QColor(Qt::white));
    QCOMPARE(object->property("dark").toBool(), false);

    loom::setTheme(QStringLiteral("dark"));
    QCOMPARE(object->property("surface").value<QColor>(), QColor(0x0f, 0x17, 0x2a));
    QCOMPARE(object->property("dark").toBool(), true);
    QCOMPARE(object->property("theme").toString(), QStringLiteral("dark"));

    // Assigning Loom.theme from QML must round-trip too.
    QVERIFY(
        QMetaObject::invokeMethod(
            object.data(), "switchTo", Q_ARG(QVariant, QStringLiteral("light"))));
    QCOMPARE(loom::theme(), QStringLiteral("light"));
    QCOMPARE(object->property("surface").value<QColor>(), QColor(Qt::white));
}

void TokenTests::unknownThemeIsRejected()
{
    loom::setTheme(QStringLiteral("dark"));
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("unknown theme")));
    loom::setTheme(QStringLiteral("solarized"));
    QCOMPARE(loom::theme(), QStringLiteral("dark"));
}

void TokenTests::easingTokensAreBezierCurves()
{
    const QEasingCurve curve =
        LoomTokenRegistry::instance()->easing(QStringLiteral("in-out"));
    QCOMPARE(curve.type(), QEasingCurve::BezierSpline);
    // cubic-bezier(0.4, 0, 0.2, 1): slow start, long decelerating tail.
    QVERIFY(curve.valueForProgress(0.1) < 0.1);
    QVERIFY(curve.valueForProgress(0.5) > 0.5);
    QVERIFY(curve.valueForProgress(0.9) > 0.9);
}

QTEST_MAIN(TokenTests)
#include "tst_tokens.moc"
