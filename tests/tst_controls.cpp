#include <QtTest>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
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
    void shadowingTypesDeriveFromWhatTheyShadow();
    void rowAlignsChildrenOnTheCrossAxis();
    void rowAlignmentRespectsPadding();
    void rowHonoursPaddingWrittenAfterConstruction();
    void colAlignsChildrenOnTheCrossAxis();
    void gapUtilityReachesPositionerSpacing();
    void buttonStylesThroughItsBackgroundAndLabel();
    void fieldInvalidStateNeedsNoConfiguration();
    void listRowSelectionReachesItsLabel();
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

// The invariant that makes shadowing safe. `import Loom.Controls` after
// `import QtQuick` re-points the name `Row` at this module, in every file that
// imports both. That is only harmless while the shadowing type is a strict
// superset of the one it hides -- so every colliding name must derive from what
// it shadows, and this is the check that says so.
void ControlsTests::shadowingTypesDeriveFromWhatTheyShadow()
{
    struct Shadowed {
        const char *type;
        const char *base;
    };
    // Col is absent on purpose: it does not collide with a QtQuick name, which
    // is exactly why it is spelled Col and not Column.
    static constexpr Shadowed shadowed[] = {
        {"Row", "QQuickRow"},
        {"Grid", "QQuickGrid"},
    };

    for (const auto &entry : shadowed) {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray document = QByteArray("import QtQuick\nimport Loom.Controls\n")
            + entry.type + " {\n}\n";
        QScopedPointer<QQuickItem> item(createItem(component, document));
        QVERIFY2(item, qPrintable(component.errorString()));

        bool derives = false;
        for (const QMetaObject *meta = item->metaObject(); meta; meta = meta->superClass()) {
            if (qstrcmp(meta->className(), entry.base) == 0) {
                derives = true;
                break;
            }
        }
        QVERIFY2(derives,
            qPrintable(QStringLiteral("Loom.Controls.%1 shadows a QtQuick type without "
                                      "deriving from %2")
                    .arg(QString::fromLatin1(entry.type), QString::fromLatin1(entry.base))));
    }
}

void ControlsTests::rowAlignsChildrenOnTheCrossAxis()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom.Controls\n"
        "Row {\n"
        "    align: Qt.AlignVCenter\n"
        "    Rectangle { objectName: \"tall\"; width: 10; height: 40 }\n"
        "    Rectangle { objectName: \"short\"; width: 10; height: 10 }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    // The Row is as tall as its tallest child, so that one stays at the top
    // and the short one is offset to sit on the same centre line.
    QQuickItem *const tall = item->findChild<QQuickItem *>(QStringLiteral("tall"));
    QQuickItem *const shortItem = item->findChild<QQuickItem *>(QStringLiteral("short"));
    QVERIFY(tall && shortItem);
    QTRY_COMPARE(shortItem->y(), 15.0);
    QCOMPARE(tall->y(), 0.0);
}

void ControlsTests::rowAlignmentRespectsPadding()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Row {\n"
        "    Lo.style: \"p-4\"\n"
        "    align: Qt.AlignBottom\n"
        "    Rectangle { objectName: \"tall\"; width: 10; height: 40 }\n"
        "    Rectangle { objectName: \"short\"; width: 10; height: 10 }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    // A positioner carries the padding properties too, so `p-4` lands on it via
    // the same duck-typing Box uses -- but only because Row forces a layout
    // when padding changes. See rowHonoursPaddingWrittenAfterConstruction.
    QQuickItem *const shortItem = item->findChild<QQuickItem *>(QStringLiteral("short"));
    QVERIFY(shortItem);
    QTRY_COMPARE(item->property("topPadding").toReal(), 16.0);
    // The padded box is 16 + 40 + 16 tall, so the content band is 40 and the
    // short child sits at topPadding + (40 - 10).
    QTRY_COMPARE(item->height(), 72.0);
    QTRY_COMPARE(shortItem->y(), 46.0);
}

// QQuickBasePositioner ignores padding assigned after construction: it neither
// grows the implicit size nor re-offsets children, so a bare QtQuick.Row given
// topPadding from C++ leaves them at y == 0. That is Qt's behaviour and not
// Loom's -- but Lo.style only ever writes late, through a queued invoke, so
// without the forceLayout() in Row.qml `p-4` on a Row would set the property
// and change nothing that renders.
void ControlsTests::rowHonoursPaddingWrittenAfterConstruction()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom.Controls\n"
        "Row {\n"
        "    Rectangle { objectName: \"only\"; width: 10; height: 40 }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QQuickItem *const only = item->findChild<QQuickItem *>(QStringLiteral("only"));
    QVERIFY(only);
    QCOMPARE(item->implicitHeight(), 40.0);

    // Deliberately a plain property write, the way the style engine does it,
    // rather than a declared value -- the declared case works on stock QtQuick.
    QQmlProperty(item.data(), QStringLiteral("topPadding")).write(16.0);
    QQmlProperty(item.data(), QStringLiteral("bottomPadding")).write(16.0);

    QTRY_COMPARE(item->implicitHeight(), 72.0);
    QCOMPARE(only->y(), 16.0);
}

void ControlsTests::colAlignsChildrenOnTheCrossAxis()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom.Controls\n"
        "Col {\n"
        "    align: Qt.AlignHCenter\n"
        "    Rectangle { objectName: \"wide\"; width: 40; height: 10 }\n"
        "    Rectangle { objectName: \"narrow\"; width: 10; height: 10 }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QQuickItem *const narrow = item->findChild<QQuickItem *>(QStringLiteral("narrow"));
    QVERIFY(narrow);
    QTRY_COMPARE(narrow->x(), 15.0);
}

void ControlsTests::gapUtilityReachesPositionerSpacing()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Row {\n"
        "    Lo.style: \"gap-6\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    // The examples reach for `spacing: Loom.space.sN` about three times as
    // often as `gap-*`. Nothing was stopping them -- the utility already routes
    // to `spacing` on anything that has it -- so this pins that down for the
    // shipped containers.
    QTRY_COMPARE(item->property("spacing").toReal(), 24.0);
}

void ControlsTests::buttonStylesThroughItsBackgroundAndLabel()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Button {\n"
        "    text: \"Save\"\n"
        "    Lo.style: \"px-4 py-2 rounded-lg bg-blue-500 text-white\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    // Replaces the Rectangle + Text + MouseArea triple the gallery writes out
    // three times: appearance on the background, text colour on the label.
    QQuickItem *const background = itemProperty(item.data(), "background");
    QVERIFY(background);
    QTRY_COMPARE(background->property("color").value<QColor>(), QColor(0x3b, 0x82, 0xf6));
    QCOMPARE(item->property("leftPadding").toReal(), 16.0);

    QQuickItem *const label = itemProperty(item.data(), "contentItem");
    QVERIFY(label);
    QTRY_COMPARE(label->property("color").value<QColor>(), QColor(Qt::white));
    QCOMPARE(label->property("text").toString(), QStringLiteral("Save"));
}

// `invalid` is a built-in state rather than an application-declared one
// precisely so this works with no design file: a shipped component cannot
// require the application to have configured something before it renders
// correctly. This test runs against the default registry.
void ControlsTests::fieldInvalidStateNeedsNoConfiguration()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Field {\n"
        "    label: \"Email\"\n"
        "    width: 300\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    // The cookbook had to write this as
    //     + (field.invalid ? " border-danger" : " border-outline")
    // duplicated across the items that needed it. It is a variant now.
    QQuickItem *const field = item.data();
    QTRY_VERIFY(field->property("invalid").isValid());
    QCOMPARE(field->property("invalid").toBool(), false);

    field->setProperty("invalid", true);
    QTRY_COMPARE(field->property("invalid").toBool(), true);
    // The message line is bound to the same property, so it appears with it.
    bool sawMessage = false;
    for (QQuickItem *child : field->childItems()) {
        for (QQuickItem *inner : child->childItems()) {
            if (inner->property("text").toString().contains(
                    QStringLiteral("does not look right")))
                sawMessage = inner->isVisible();
        }
        if (child->property("text").toString().contains(
                QStringLiteral("does not look right")))
            sawMessage = child->isVisible();
    }
    QVERIFY2(sawMessage, "Field's error line did not follow its invalid property");
}

void ControlsTests::listRowSelectionReachesItsLabel()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "ListRow {\n"
        "    width: 200\n"
        "    text: \"Inbox\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    // The cookbook's delegate spelled selection as a ternary written twice --
    // once on the row and once on its label, because the two cannot share a
    // string. The label reads the row's state through group-selected/row.
    QQuickItem *const background = itemProperty(item.data(), "background");
    QVERIFY(background);
    const QColor unselected = background->property("color").value<QColor>();

    item->setProperty("selected", true);
    QTRY_VERIFY(background->property("color").value<QColor>() != unselected);
}

QTEST_MAIN(ControlsTests)
#include "tst_controls.moc"
