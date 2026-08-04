#include <QtTest>

#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QUrl>
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

// Escaped rather than a raw string literal for the reason tst_icon.cpp gives:
// moc does not honour raw strings and reads the "//" in the xmlns URL as a
// comment, which eats the Q_OBJECT below with it.
constexpr auto squareSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" "
    "viewBox=\"0 0 16 16\">"
    "<rect x=\"0\" y=\"0\" width=\"16\" height=\"16\" fill=\"#0000ff\"/>"
    "</svg>";

// A real asset behind a real URL, so an Icon under test resolves rather than
// warning its way to an empty image. Returns the file:// form, which
// loomResolveIconSource leaves alone -- no icon root to configure.
QString writeIcon(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("home.svg"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(squareSvg);
    return QUrl::fromLocalFile(path).toString();
}

QByteArray iconDocument(const QString &source, const QByteArray &body)
{
    return QByteArray("import QtQuick\nimport Loom\nimport Loom.Controls\n"
                      "Icon {\n    name: \"")
        + source.toUtf8() + "\"\n" + body + "}\n";
}

} // namespace

class ControlsTests : public QObject {
    Q_OBJECT

private slots:
    // The same style loom::Application settles on for an application whose
    // platform default cannot be customised, pinned here so these tests assert
    // on what Loom does rather than on which style the machine running them
    // happens to default to -- the native macOS and Windows styles replace
    // neither `background` nor `contentItem`, so under them the two delegate
    // tests below would be measuring Qt's refusal.
    void initTestCase()
    {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
    }

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
    void fieldPartStylesReachTheParts();
    void listRowSelectionReachesItsLabel();
    void iconResolvesItsSourceThroughTheProvider();
    void iconTakesItsColourFromATextUtility();
    void scrollContentHeightFollowsItsContent();
    void scrollPaddingUtilityReachesTheViewport();
    void labelAndDividerFollowTheirTokens();
    void spacerFillsInALayoutAndSizesInAPositioner();
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
    // Col, Box, Field, ListRow, Icon, Scroll, Spacer and Divider are absent on
    // purpose: none of them collide with a QtQuick or QtQuick.Controls name,
    // which is exactly why Col is spelled Col and not Column.
    //
    // Label is the one that nearly went wrong. It reads like a plain Text --
    // and was written as one -- but QtQuick.Controls has a Label, so shadowing
    // it with a Text subclass would have taken away the padding and background
    // that QQuickLabel adds. Deriving from the Control end costs nothing and
    // keeps the invariant.
    static constexpr Shadowed shadowed[] = {
        {"Row", "QQuickRow"},
        {"Grid", "QQuickGrid"},
        {"Button", "QQuickButton"},
        {"Label", "QQuickLabel"},
    };

    for (const auto &entry : shadowed) {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray document =
            QByteArray("import QtQuick\nimport Loom.Controls\n") + entry.type + " {\n}\n";
        QScopedPointer<QQuickItem> item(createItem(component, document));
        QVERIFY2(item, qPrintable(component.errorString()));

        bool derives = false;
        for (const QMetaObject *meta = item->metaObject(); meta;
             meta = meta->superClass()) {
            if (qstrcmp(meta->className(), entry.base) == 0) {
                derives = true;
                break;
            }
        }
        QVERIFY2(
            derives,
            qPrintable(QStringLiteral(
                           "Loom.Controls.%1 shadows a QtQuick type without "
                           "deriving from %2")
                           .arg(
                               QString::fromLatin1(entry.type),
                               QString::fromLatin1(entry.base))));
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

// A part-style property is the library's answer to a sub-delegate the engine
// cannot reach: LoomStyleAttached writes onto the item carrying `Lo.style`, and
// Field's caption, input and error line are internal to Field.qml. Forwarding
// is ordinary QML -- what has to be pinned is that the classes *append*, so an
// override at the call site keeps the part's own styling.
void ControlsTests::fieldPartStylesReachTheParts()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Field {\n"
        "    width: 300\n"
        "    label: \"Email\"\n"
        "    contentStyle: \"text-white\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QQuickItem *input = nullptr;
    const auto children = item->findChildren<QQuickItem *>();
    for (QQuickItem *const child : children) {
        if (child->inherits("QQuickTextField")) {
            input = child;
            break;
        }
    }
    QVERIFY(input);

    // Field.qml already writes `text-foreground` onto the input. `text-white`
    // arrives after it in the same string and at the same specificity, and
    // later wins -- which is the whole reason these are appended rather than
    // substituted.
    QTRY_COMPARE(input->property("color").value<QColor>(), QColor(Qt::white));
    // The part's own classes survive the override.
    QCOMPARE(input->property("leftPadding").toReal(), 12.0);
}

// Icon exists because two places in this repository -- Row.qml's docstring and
// docs/styling/components.md -- wrote `Icon { }` for a type that did not.
void ControlsTests::iconResolvesItsSourceThroughTheProvider()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = writeIcon(dir);
    QVERIFY(!source.isEmpty());

    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(component, iconDocument(source, {})));
    QVERIFY2(item, qPrintable(component.errorString()));

    // Not the file URL: an icon is served through LoomIconProvider so the tint
    // can be applied on the way out, which is the only reachable recolouring
    // hook Qt leaves for a non-mask source.
    QTRY_VERIFY(item->property("source").toUrl().toString().startsWith(
        QLatin1String("image://loom/")));
    QCOMPARE(item->width(), 20.0);
}

// The negative control for the one engine change in this phase. Revert the
// QQuickImage branch in LoomTargetProfile::forType and `color` keeps its
// binding to the foreground token, so this goes red on exactly that line.
void ControlsTests::iconTakesItsColourFromATextUtility()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = writeIcon(dir);
    QVERIFY(!source.isEmpty());

    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(
        createItem(component, iconDocument(source, "    Lo.style: \"size-8 text-white\"\n")));
    QVERIFY2(item, qPrintable(component.errorString()));

    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(Qt::white));
    // Sizing is a class too, and sourceSize follows it, so the asset
    // rasterises at the size it is drawn at.
    QCOMPARE(item->width(), 32.0);
    QCOMPARE(item->property("sourceSize").toSize(), QSize(32, 32));
}

// The hand-wired Flickable appears in Main.qml and in the app template, and
// the two compute contentHeight differently. This is that arithmetic, once.
void ControlsTests::scrollContentHeightFollowsItsContent()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Scroll {\n"
        "    width: 200\n"
        "    height: 100\n"
        "    Rectangle { width: 100; height: 400 }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QTRY_COMPARE(item->property("contentHeight").toReal(), 400.0);
    QCOMPARE(item->property("contentWidth").toReal(), 200.0);
}

// Flickable is not a Control and has no padding properties. Declaring the four
// conventional names is enough, because LoomTargetProfile duck-types on them --
// the same opt-in a user component gets, and the reason Box could be an
// ordinary Control rather than a special case in the engine.
void ControlsTests::scrollPaddingUtilityReachesTheViewport()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Scroll {\n"
        "    width: 200\n"
        "    height: 100\n"
        "    Lo.style: \"p-6\"\n"
        "    Rectangle { width: 100; height: 400 }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QTRY_COMPARE(item->property("topPadding").toReal(), 24.0);
    // And the padding is inside the scrollable extent rather than clipped off
    // it, which is the part the hand-written versions kept restating.
    QTRY_COMPARE(item->property("contentHeight").toReal(), 448.0);
}

// Compared against reference items in the same document rather than against
// literals: what matters is that these types agree with the tokens, not what
// the tokens currently happen to be.
void ControlsTests::labelAndDividerFollowTheirTokens()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Item {\n"
        "    Label { objectName: \"label\"; text: \"body\" }\n"
        "    Text {\n"
        "        objectName: \"referenceText\"\n"
        "        wrapMode: Text.WordWrap\n"
        "        color: Loom.color.foreground\n"
        "    }\n"
        "    Divider { objectName: \"divider\"; width: 100 }\n"
        "    Rectangle { objectName: \"referenceRule\"; color: Loom.color.outline }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QQuickItem *const label = item->findChild<QQuickItem *>(QStringLiteral("label"));
    QQuickItem *const referenceText =
        item->findChild<QQuickItem *>(QStringLiteral("referenceText"));
    QVERIFY(label);
    QVERIFY(referenceText);
    QCOMPARE(label->property("wrapMode").toInt(), referenceText->property("wrapMode").toInt());
    QCOMPARE(
        label->property("color").value<QColor>(),
        referenceText->property("color").value<QColor>());

    QQuickItem *const divider = item->findChild<QQuickItem *>(QStringLiteral("divider"));
    QQuickItem *const referenceRule =
        item->findChild<QQuickItem *>(QStringLiteral("referenceRule"));
    QVERIFY(divider);
    QVERIFY(referenceRule);
    QCOMPARE(
        divider->property("color").value<QColor>(),
        referenceRule->property("color").value<QColor>());
    // Thickness on the cross axis, nothing on the main one: the extent is
    // still owed by the call site, and staying 0 makes that obvious.
    QCOMPARE(divider->implicitHeight(), 1.0);
    QCOMPARE(divider->implicitWidth(), 0.0);
}

void ControlsTests::spacerFillsInALayoutAndSizesInAPositioner()
{
    QQmlEngine engine;

    QQmlComponent layoutComponent(&engine);
    QScopedPointer<QQuickItem> layout(createItem(
        layoutComponent,
        "import QtQuick\nimport QtQuick.Layouts\nimport Loom\nimport Loom.Controls\n"
        "RowLayout {\n"
        "    width: 300\n"
        "    spacing: 0\n"
        "    Item { implicitWidth: 50; implicitHeight: 10 }\n"
        "    Spacer { objectName: \"spacer\" }\n"
        "    Item { implicitWidth: 50; implicitHeight: 10 }\n"
        "}\n"));
    QVERIFY2(layout, qPrintable(layoutComponent.errorString()));

    QQuickItem *const spacer = layout->findChild<QQuickItem *>(QStringLiteral("spacer"));
    QVERIFY(spacer);
    QTRY_COMPARE(spacer->width(), 200.0);

    // A positioner distributes nothing -- Row places children end to end and
    // stops -- so there is no fill to take and `size` is the only thing that
    // can work there. Saying so in a test rather than only in a docstring.
    QQmlComponent rowComponent(&engine);
    QScopedPointer<QQuickItem> row(createItem(
        rowComponent,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Row {\n"
        "    spacing: 0\n"
        "    Item { implicitWidth: 10; implicitHeight: 10 }\n"
        "    Spacer { size: 16 }\n"
        "    Item { objectName: \"tail\"; implicitWidth: 10; implicitHeight: 10 }\n"
        "}\n"));
    QVERIFY2(row, qPrintable(rowComponent.errorString()));

    QQuickItem *const tail = row->findChild<QQuickItem *>(QStringLiteral("tail"));
    QVERIFY(tail);
    QTRY_COMPARE(tail->x(), 26.0);
}

QTEST_MAIN(ControlsTests)
#include "tst_controls.moc"
