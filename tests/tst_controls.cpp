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

QQuickItem *itemProperty(const QQuickItem *item, const char *name)
{
    return item->property(name).value<QQuickItem *>();
}

} // namespace

class ControlsTests : public QObject {
    Q_OBJECT

private slots:
    void boxTakesPaddingFromAUtility();
    void boxBackgroundTakesAppearanceUtilities();
    void boxImplicitHeightIsContentPlusPadding();
    void boxChildrenLandInTheContentItem();
};

// Box exists because `p-4` needs `topPadding`, and the Rectangle everyone
// reaches for has none -- so a padded card has always been an inner item inset
// by anchors.margins plus hand-written implicitHeight arithmetic. These four
// tests are that construction, replaced.

void ControlsTests::boxTakesPaddingFromAUtility()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Box {\n"
        "    Lo.style: \"p-4 px-6\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    // Control declares the four properties LoomTargetProfile duck-types on, so
    // this needs no engine change at all -- the same assertions as
    // tst_apply's duckTypedPadding, now against a shipped type.
    QTRY_COMPARE(item->property("topPadding").toReal(), 16.0);
    QCOMPARE(item->property("bottomPadding").toReal(), 16.0);
    // px-6 comes later at equal specificity, so it wins the horizontal sides.
    QCOMPARE(item->property("leftPadding").toReal(), 24.0);
    QCOMPARE(item->property("rightPadding").toReal(), 24.0);
}

void ControlsTests::boxBackgroundTakesAppearanceUtilities()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Box {\n"
        "    Lo.style: \"bg-blue-500 rounded-lg\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    // Box declares a Rectangle background purely so these have somewhere to
    // land: LoomStyleAttached::backgroundPath() refuses a non-Rectangle
    // delegate, and with no background at all `bg-*` would warn as unsupported.
    QQuickItem *const background = itemProperty(item.data(), "background");
    QVERIFY(background);
    QTRY_COMPARE(background->property("color").value<QColor>(), QColor(0x3b, 0x82, 0xf6));
    QCOMPARE(background->property("radius").toReal(), 8.0);
}

void ControlsTests::boxImplicitHeightIsContentPlusPadding()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Box {\n"
        "    Lo.style: \"p-6\"\n"
        "    Rectangle { implicitWidth: 100; implicitHeight: 40 }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    // The arithmetic this replaces, written out at three separate call sites in
    // the gallery and the template:
    //     implicitHeight: body.implicitHeight + 2 * Loom.space.s6
    QTRY_COMPARE(item->implicitHeight(), 40.0 + 2 * 24.0);
    QCOMPARE(item->implicitWidth(), 100.0 + 2 * 24.0);
}

void ControlsTests::boxChildrenLandInTheContentItem()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Box {\n"
        "    Rectangle { objectName: \"child\" }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    // The default property is aliased to the content item, not to the Box, so
    // padding actually insets children. A child parented to the Box itself
    // would ignore padding entirely and the component would be decorative.
    QQuickItem *const content = itemProperty(item.data(), "contentItem");
    QVERIFY(content);
    QQuickItem *const child = item->findChild<QQuickItem *>(QStringLiteral("child"));
    QVERIFY(child);
    QCOMPARE(child->parentItem(), content);
}

QTEST_MAIN(ControlsTests)
#include "tst_controls.moc"
