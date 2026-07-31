#include <QtTest>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedPointer>

namespace {

QQuickItem *createItem(QQmlComponent &component, const QByteArray &document)
{
    component.setData(document, QUrl());
    return qobject_cast<QQuickItem *>(component.create());
}

} // namespace

class BreakpointTests : public QObject {
    Q_OBJECT

private slots:
    void tiersFollowWindowWidth();
    void mobileFirstMinWidth();
    void noWindowMeansBaseTier();
    void fullSizeTracksParent();
};

void BreakpointTests::tiersFollowWindowWidth()
{
    QQuickWindow window;
    window.resize(400, 300); // below sm (640)
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"bg-white md:bg-black xl:bg-red-500\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    item->setParentItem(window.contentItem());

    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));

    window.resize(800, 300); // >= md (768)
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::black));

    window.resize(1300, 300); // >= xl (1280)
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0xef, 0x44, 0x44));

    window.resize(400, 300); // back to base
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));
}

void BreakpointTests::mobileFirstMinWidth()
{
    QQuickWindow window;
    window.resize(1300, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    // Only an sm: rule; at xl width it still applies (min-width semantics).
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"w-4 sm:w-64\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    item->setParentItem(window.contentItem());

    QTRY_COMPARE(item->width(), 256.0);
}

void BreakpointTests::noWindowMeansBaseTier()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"bg-white sm:bg-black\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));
}

void BreakpointTests::fullSizeTracksParent()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Item {\n"
        "    width: 200; height: 150\n"
        "    Rectangle {\n"
        "        objectName: \"child\"\n"
        "        Lo.style: \"w-full h-full\"\n"
        "    }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QQuickItem *child = item->findChild<QQuickItem *>(QStringLiteral("child"));
    QVERIFY(child);

    QTRY_COMPARE(child->width(), 200.0);
    QCOMPARE(child->height(), 150.0);

    item->setWidth(320);
    item->setHeight(240);
    QTRY_COMPARE(child->width(), 320.0);
    QCOMPARE(child->height(), 240.0);
}

QTEST_MAIN(BreakpointTests)
#include "tst_breakpoints.moc"
