#include <QtTest>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedPointer>

namespace {

QQuickItem *
createInWindow(QQmlComponent &component, const QByteArray &document, QQuickWindow *window)
{
    component.setData(document, QUrl());
    auto *item = qobject_cast<QQuickItem *>(component.create());
    if (item)
        item->setParentItem(window->contentItem());
    return item;
}

} // namespace

class StateTests : public QObject {
    Q_OBJECT

private slots:
    void hoverVariant();
    void pressedViaNativeProperty();
    void pressedViaTapHandler();
    void handlersCoexistWithMouseArea();
    void focusVariant();
    void disabledVariant();
    void variantComposition();
};

void StateTests::hoverVariant()
{
    QQuickWindow window;
    window.resize(400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createInWindow(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    width: 100; height: 100\n"
        "    Lo.style: \"bg-white hover:bg-black\"\n"
        "}\n",
        &window));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));

    QTest::mouseMove(&window, QPoint(50, 50));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::black));

    QTest::mouseMove(&window, QPoint(300, 250));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));
}

void StateTests::pressedViaNativeProperty()
{
    QQuickWindow window;
    window.resize(400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    // MouseArea has its own NOTIFYing bool `pressed`; no TapHandler is needed.
    QScopedPointer<QQuickItem> item(createInWindow(
        component,
        "import QtQuick\nimport Loom\n"
        "MouseArea {\n"
        "    width: 100; height: 100\n"
        "    Lo.style: \"opacity-100 pressed:opacity-50\"\n"
        "}\n",
        &window));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->opacity(), 1.0);

    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTRY_COMPARE(item->opacity(), 0.5);

    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTRY_COMPARE(item->opacity(), 1.0);
}

void StateTests::pressedViaTapHandler()
{
    QQuickWindow window;
    window.resize(400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    // A plain Rectangle has no pressed property; the engine-created passive
    // TapHandler supplies the state.
    QScopedPointer<QQuickItem> item(createInWindow(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    width: 100; height: 100\n"
        "    Lo.style: \"bg-white pressed:bg-black\"\n"
        "}\n",
        &window));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));

    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::black));

    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));
}

void StateTests::handlersCoexistWithMouseArea()
{
    QQuickWindow window;
    window.resize(400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    // The watcher item and its handlers must not steal events from the
    // target's own MouseArea children.
    QScopedPointer<QQuickItem> item(createInWindow(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    property int clicks: 0\n"
        "    width: 100; height: 100\n"
        "    Lo.style: \"bg-white hover:bg-black pressed:bg-blue-500\"\n"
        "    MouseArea {\n"
        "        anchors.fill: parent\n"
        "        onClicked: parent.clicks++\n"
        "    }\n"
        "}\n",
        &window));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));

    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTRY_COMPARE(item->property("clicks").toInt(), 1);
}

void StateTests::focusVariant()
{
    QQuickWindow window;
    window.resize(400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createInWindow(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    width: 100; height: 100\n"
        "    Lo.style: \"bg-white focus:bg-black\"\n"
        "}\n",
        &window));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));

    item->forceActiveFocus();
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::black));

    item->setFocus(false);
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));
}

void StateTests::disabledVariant()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"opacity-100 disabled:opacity-40\"\n"
        "}\n",
        QUrl());
    QScopedPointer<QQuickItem> item(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->opacity(), 1.0);

    item->setEnabled(false);
    QTRY_COMPARE(item->opacity(), 0.4);

    item->setEnabled(true);
    QTRY_COMPARE(item->opacity(), 1.0);
}

void StateTests::variantComposition()
{
    QQuickWindow window;
    window.resize(400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    // hover:pressed: requires both states; hover alone keeps the hover rule,
    // and the doubly-qualified rule outranks it while pressed.
    QScopedPointer<QQuickItem> item(createInWindow(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    width: 100; height: 100\n"
        "    Lo.style: \"bg-white hover:bg-black hover:pressed:bg-red-500\"\n"
        "}\n",
        &window));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));

    QTest::mouseMove(&window, QPoint(50, 50));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::black));

    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0xef, 0x44, 0x44));

    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::black));
}

QTEST_MAIN(StateTests)
#include "tst_states.moc"
