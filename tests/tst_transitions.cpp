#include <QtTest>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QScopedPointer>
#include <loom/loom.h>

namespace {

QQuickItem *createItem(QQmlComponent &component, const QByteArray &document)
{
    component.setData(document, QUrl());
    return qobject_cast<QQuickItem *>(component.create());
}

} // namespace

class TransitionTests : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void colorsAnimateOnThemeSwitch();
    void withoutTransitionValuesSnap();
    void opacityAnimatesOnStateChange();
    void uncoveredPropertiesSnapWhileColorsAnimate();
    void transitionNoneDisables();

private:
    QColor colorOf(QQuickItem *item)
    {
        return item->property("color").value<QColor>();
    }
};

void TransitionTests::cleanup()
{
    loom::setTheme(QStringLiteral("light"));
}

void TransitionTests::colorsAnimateOnThemeSwitch()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"bg-white rounded dark:bg-black transition-colors"
        " duration-1000 ease-linear\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    // Wait on radius, which only the apply sets: the default color is already
    // white, and the theme must not flip before the (snapping) first apply.
    QTRY_COMPARE(item->property("radius").toReal(), 4.0);
    QCOMPARE(colorOf(item.data()), QColor(Qt::white));

    loom::setTheme(QStringLiteral("dark"));
    // Mid-flight: after a slice of the 1000ms animation the color must be
    // strictly between the endpoints.
    QTest::qWait(150);
    const QColor middle = colorOf(item.data());
    QVERIFY2(
        middle != QColor(Qt::white) && middle != QColor(Qt::black),
        qPrintable(middle.name()));
    QTRY_COMPARE_WITH_TIMEOUT(colorOf(item.data()), QColor(Qt::black), 3000);
}

void TransitionTests::withoutTransitionValuesSnap()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"bg-white rounded dark:bg-black\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("radius").toReal(), 4.0);

    loom::setTheme(QStringLiteral("dark"));
    QTRY_COMPARE(colorOf(item.data()), QColor(Qt::black));
}

void TransitionTests::opacityAnimatesOnStateChange()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"opacity-100 rounded disabled:opacity-0 transition-opacity"
        " duration-1000 ease-linear\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("radius").toReal(), 4.0);
    QCOMPARE(item->opacity(), 1.0);

    item->setEnabled(false);
    QTest::qWait(150);
    const qreal middle = item->opacity();
    QVERIFY2(middle > 0.0 && middle < 1.0, qPrintable(QString::number(middle)));
    QTRY_COMPARE_WITH_TIMEOUT(item->opacity(), 0.0, 3000);
}

void TransitionTests::uncoveredPropertiesSnapWhileColorsAnimate()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"bg-white rounded dark:bg-black dark:rounded-xl"
        " transition-colors duration-1000\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("radius").toReal(), 4.0);

    loom::setTheme(QStringLiteral("dark"));
    // radius is not covered by transition-colors: it must have snapped by the
    // time the (queued) apply has run, well before the color settles.
    QTRY_COMPARE(item->property("radius").toReal(), 12.0);
    QVERIFY(colorOf(item.data()) != QColor(Qt::black));
    QTRY_COMPARE_WITH_TIMEOUT(colorOf(item.data()), QColor(Qt::black), 3000);
}

void TransitionTests::transitionNoneDisables()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    // A more-qualified transition-none turns animation back off in dark mode.
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"bg-white rounded dark:bg-black transition-colors"
        " duration-1000 dark:transition-none\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("radius").toReal(), 4.0);
    QCOMPARE(colorOf(item.data()), QColor(Qt::white));

    loom::setTheme(QStringLiteral("dark"));
    // 100ms is enough for the queued snap-apply but far too short for the
    // 1000ms animation to finish: this fails if the animation runs at all.
    QTRY_COMPARE_WITH_TIMEOUT(colorOf(item.data()), QColor(Qt::black), 100);
}

QTEST_GUILESS_MAIN(TransitionTests)
#include "tst_transitions.moc"
