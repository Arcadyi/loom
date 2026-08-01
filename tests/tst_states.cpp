#include <QtTest>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedPointer>
#include <loom/loom.h>

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
    void cleanup();
    void hoverVariant();
    void pressedViaNativeProperty();
    void pressedViaTapHandler();
    void handlersCoexistWithMouseArea();
    void focusVariant();
    void disabledVariant();
    void variantComposition();
    void stateVariantBeatsBreakpoint();
    void controlAndNegatedVariants();
    void structuralVariants();
    void namedGroupVariant();
    void namedThemeVariant();
};

void StateTests::cleanup()
{
    loom::setTheme(QStringLiteral("light"));
}

// Regression: breakpoint and state variants used to share one "number of
// prefixes" specificity counter, so at equal counts the later class won. A
// `md:` rule written after a `hover:` rule for the same property therefore made
// hovering do nothing at any width above 768 -- the styling silently vanished
// on exactly the desktop widths most people develop at.
void StateTests::stateVariantBeatsBreakpoint()
{
    QQuickWindow window;
    window.resize(1024, 300); // >= md (768)
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createInWindow(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    width: 100; height: 100\n"
        "    Lo.style: \"bg-white hover:bg-black md:bg-red-500\"\n"
        "}\n",
        &window));
    QVERIFY2(item, qPrintable(component.errorString()));

    // Unhovered the breakpoint rule is the only qualified one.
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0xef, 0x44, 0x44));

    QTest::mouseMove(&window, QPoint(50, 50));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::black));

    QTest::mouseMove(&window, QPoint(500, 250));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0xef, 0x44, 0x44));
}

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

void StateTests::controlAndNegatedVariants()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    property bool checked: false\n"
        "    Lo.style: \"bg-white not-checked:bg-blue-500 checked:bg-red-500\"\n"
        "}\n",
        QUrl());
    QScopedPointer<QQuickItem> item(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0x3b, 0x82, 0xf6));

    item->setProperty("checked", true);
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0xef, 0x44, 0x44));
}

void StateTests::structuralVariants()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "Column {\n"
        "    Lo.style: \"hover:opacity-100\"\n"
        "    Rectangle { objectName: \"first\"; Lo.style: \"bg-white first:bg-blue-500\" "
        "}\n"
        "    Rectangle { objectName: \"middle\"; Lo.style: \"bg-white odd:bg-red-500\" "
        "}\n"
        "    Rectangle { objectName: \"last\"; Lo.style: \"bg-white last:bg-black\" }\n"
        "}\n",
        QUrl());
    QScopedPointer<QQuickItem> root(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *first = root->findChild<QQuickItem *>(QStringLiteral("first"));
    auto *middle = root->findChild<QQuickItem *>(QStringLiteral("middle"));
    auto *last = root->findChild<QQuickItem *>(QStringLiteral("last"));
    QVERIFY(first && middle && last);

    QTRY_COMPARE(first->property("color").value<QColor>(), QColor(0x3b, 0x82, 0xf6));
    // CSS-style positions are one-based, so the second child is even.
    QCOMPARE(middle->property("color").value<QColor>(), QColor(Qt::white));
    QCOMPARE(last->property("color").value<QColor>(), QColor(Qt::black));

    // Child insertion and later visibility changes both invalidate structural
    // positions. The new child did not exist when subscriptions were first
    // installed, which used to leave `last:` stale after it was hidden.
    auto *added = new QQuickItem(root.data());
    added->setParentItem(root.data());
    QTRY_COMPARE(last->property("color").value<QColor>(), QColor(Qt::white));
    added->setVisible(false);
    QTRY_COMPARE(last->property("color").value<QColor>(), QColor(Qt::black));
}

void StateTests::namedGroupVariant()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "Item {\n"
        "    property bool checked: false\n"
        "    Lo.group: \"menu\"\n"
        "    Rectangle { objectName: \"child\"; Lo.style: \"bg-white "
        "group-checked/menu:bg-blue-500\" }\n"
        "}\n",
        QUrl());
    QScopedPointer<QQuickItem> root(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *child = root->findChild<QQuickItem *>(QStringLiteral("child"));
    QVERIFY(child);
    QTRY_COMPARE(child->property("color").value<QColor>(), QColor(Qt::white));

    root->setProperty("checked", true);
    QTRY_COMPARE(child->property("color").value<QColor>(), QColor(0x3b, 0x82, 0xf6));
}

void StateTests::namedThemeVariant()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "Rectangle { Lo.style: \"bg-white theme-dark:bg-blue-500\" }\n",
        QUrl());
    QScopedPointer<QQuickItem> item(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));

    loom::setTheme(QStringLiteral("dark"));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0x3b, 0x82, 0xf6));
}

QTEST_MAIN(StateTests)
#include "tst_states.moc"
