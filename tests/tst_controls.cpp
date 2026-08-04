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

QQuickItem *itemProperty(const QObject *item, const char *name)
{
    return item->property(name).value<QQuickItem *>();
}

// Popups are not Items, so the Item-returning helper above cannot create one.
QObject *createObject(QQmlComponent &component, const QByteArray &document)
{
    component.setData(document, QUrl());
    return component.create();
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

// Two trivial pages on disk, so a RouteView has something real to resolve to.
QString writePage(const QTemporaryDir &dir, const QString &name, const QString &text)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(QStringLiteral("import QtQuick\nItem { property string tag: \"%1\" }\n")
                   .arg(text)
                   .toUtf8());
    return QUrl::fromLocalFile(path).toString();
}

QByteArray routeViewDocument(const QString &first, const QString &second)
{
    return QByteArray(
               "import QtQuick\nimport Loom\nimport Loom.Controls\n"
               "RouteView {\n"
               "    width: 100\n    height: 100\n"
               "    routes: ({ \"one\": \"")
        + first.toUtf8() + "\", \"two\": \"" + second.toUtf8() + "\" })\n"
        + "    fallback: \"" + first.toUtf8() + "\"\n}\n";
}

QByteArray iconDocument(const QString &source, const QByteArray &body)
{
    return QByteArray(
               "import QtQuick\nimport Loom\nimport Loom.Controls\n"
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
    void checkBoxIndicatorFollowsTheControlsState();
    void formControlBackgroundsTakeRootAppearanceUtilities();
    void formControlLabelsTakeRootTextUtilities();
    void switchHandleTravelsWithTheControl();
    void sliderFillFollowsItsValue();
    void selectPopupAndRowsAreStyleable();
    void overlayPanelsAreReachableOnlyThroughPartStyles();
    void cardIsAPaddedSurfaceThatCallSitesCanOverride();
    void progressFillFollowsItsValue();
    void badgeGrowsWithItsLabel();
    void tabIndicatorFollowsSelection();
    void rowDistributesChildrenOnTheMainAxis();
    void colDistributionSettlesRatherThanLooping();
    void routeViewResolvesRoutesAndFallsBack();
    void routeViewRestoresItsSourceAfterASeamReload();
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
    QScopedPointer<QQuickItem> item(createItem(
        component, iconDocument(source, "    Lo.style: \"size-8 text-white\"\n")));
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
    QCOMPARE(
        label->property("wrapMode").toInt(), referenceText->property("wrapMode").toInt());
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

// The mechanism the whole form tranche rests on. A control's sub-delegate is a
// Rectangle with no `checked` of its own, so it cannot read the state the way
// the control does -- `Lo.group` publishes it downward instead, which is the
// same thing that lets a ListRow's label follow the row's selection.
//
// If this stops working, every part of every form control silently stops
// reacting while still rendering, which is the worst failure shape available.
void ControlsTests::checkBoxIndicatorFollowsTheControlsState()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "CheckBox {\n"
        "    text: \"Remember me\"\n"
        "    indicatorStyle: \"rounded-md\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QQuickItem *const indicator = itemProperty(item.data(), "indicator");
    QVERIFY(indicator);

    // The part style reaches the part at all.
    QTRY_COMPARE(indicator->property("radius").toReal(), 6.0);

    // And the part's own classes survive it: the border is still there.
    QVERIFY(indicator->property("border").isValid());

    const QColor unchecked = indicator->property("color").value<QColor>();
    item->setProperty("checked", true);
    QTRY_VERIFY(indicator->property("color").value<QColor>() != unchecked);

    // Back again, so this is a live subscription rather than a one-way latch.
    item->setProperty("checked", false);
    QTRY_COMPARE(indicator->property("color").value<QColor>(), unchecked);
}

// Rule 2 of the contract in docs/styling/components.md, across the whole form
// tranche at once: `background` must be a Rectangle that nothing else writes.
//
// This is the test that catches the mistake worth catching. A delegate given
// its own default `Lo.style` looks right in isolation and is a *second writer*
// for the properties backgroundPath() already routes here -- so `bg-*` at the
// call site and the delegate's own class string race, and which one lands last
// decides the colour. Select was written that way first.
void ControlsTests::formControlBackgroundsTakeRootAppearanceUtilities()
{
    static constexpr const char *types[] = {
        "CheckBox",
        "Switch",
        "RadioButton",
        "Select",
    };

    for (const char *type : types) {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray document =
            QByteArray("import QtQuick\nimport Loom\nimport Loom.Controls\n") + type
            + " {\n    Lo.style: \"bg-blue-500 rounded-lg\"\n}\n";
        QScopedPointer<QQuickItem> item(createItem(component, document));
        QVERIFY2(
            item,
            qPrintable(QStringLiteral("%1: %2").arg(
                QString::fromLatin1(type), component.errorString())));

        QQuickItem *const background = itemProperty(item.data(), "background");
        QVERIFY2(background, type);
        QTRY_COMPARE_WITH_TIMEOUT(
            background->property("color").value<QColor>(), QColor(0x3b, 0x82, 0xf6),
            2000);
        QCOMPARE(background->property("radius").toReal(), 8.0);
    }
}

// The mirror of the above for the label half, and the reason none of these
// types has a `contentStyle`: contentPath() already routes `text-*` to a
// Control's contentItem, and the font group lands on the Control and
// propagates down. A part style for the label would be the same race.
void ControlsTests::formControlLabelsTakeRootTextUtilities()
{
    // Select carries a model rather than a `text`, so it names its own body.
    struct Labelled {
        const char *type;
        const char *extra;
    };
    static constexpr Labelled types[] = {
        {"CheckBox", "    text: \"Label\"\n"},
        {"Switch", "    text: \"Label\"\n"},
        {"RadioButton", "    text: \"Label\"\n"},
        {"Select", "    model: [\"Daily\"]\n"},
    };

    for (const auto &entry : types) {
        const char *const type = entry.type;
        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray document =
            QByteArray("import QtQuick\nimport Loom\nimport Loom.Controls\n") + type
            + " {\n" + entry.extra + "    Lo.style: \"text-white text-2xl\"\n}\n";
        QScopedPointer<QQuickItem> item(createItem(component, document));
        QVERIFY2(
            item,
            qPrintable(QStringLiteral("%1: %2").arg(
                QString::fromLatin1(type), component.errorString())));

        QQuickItem *const label = itemProperty(item.data(), "contentItem");
        QVERIFY2(label, type);
        QTRY_COMPARE_WITH_TIMEOUT(
            label->property("color").value<QColor>(), QColor(Qt::white), 2000);
        // text-2xl lands on the Control and reaches the label through the
        // `font: root.font` binding, not through a second class string.
        QCOMPARE(label->property("font").value<QFont>().pixelSize(), 24);
    }
}

void ControlsTests::switchHandleTravelsWithTheControl()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Switch {\n"
        "    handleStyle: \"bg-warning\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QQuickItem *const track = itemProperty(item.data(), "indicator");
    QVERIFY(track);
    const auto handles = track->childItems();
    QVERIFY(!handles.isEmpty());
    QQuickItem *const handle = handles.first();

    // The part style reaches the knob.
    QTRY_COMPARE(handle->property("color").value<QColor>(), QColor(0xf5, 0x9e, 0x0b));

    const qreal off = handle->x();
    item->setProperty("checked", true);
    // Travel is a Behavior, so this settles rather than jumping.
    QTRY_VERIFY(handle->x() > off);
}

void ControlsTests::sliderFillFollowsItsValue()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Slider {\n"
        "    width: 200\n"
        "    from: 0\n"
        "    to: 100\n"
        "    value: 50\n"
        "    trackStyle: \"bg-success\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QQuickItem *const groove = itemProperty(item.data(), "background");
    QVERIFY(groove);
    const auto fills = groove->childItems();
    QVERIFY(!fills.isEmpty());
    QQuickItem *const fill = fills.first();

    // Splitting groove from fill is the whole point: one part is the channel,
    // the other is how far along it the value is.
    QTRY_COMPARE(fill->property("color").value<QColor>(), QColor(0x16, 0xa3, 0x4a));
    QTRY_COMPARE(fill->width(), groove->width() / 2);

    item->setProperty("value", 100.0);
    QTRY_COMPARE(fill->width(), groove->width());
}

void ControlsTests::selectPopupAndRowsAreStyleable()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Select {\n"
        "    model: [\"Daily\", \"Weekly\"]\n"
        "    popupStyle: \"bg-warning\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QObject *const popup = item->property("popup").value<QObject *>();
    QVERIFY(popup);
    QQuickItem *const panel = popup->property("background").value<QQuickItem *>();
    QVERIFY(panel);

    // A Popup renders in the window's overlay, so its background is reachable
    // only through a part style -- nothing about the Select's own class string
    // crosses that boundary.
    QTRY_COMPARE(panel->property("color").value<QColor>(), QColor(0xf5, 0x9e, 0x0b));
}

// The constraint that shapes every popup-based type here. LoomStyleAttached
// casts its target to a QQuickItem and warns when it cannot, and a Popup is a
// QObject -- so `Lo.style` on a Dialog, Menu or Tooltip is not "ignored", there
// is no item for it to be about. Part styles are the only way in, which is why
// these types expose more of them than a Control-based one needs.
void ControlsTests::overlayPanelsAreReachableOnlyThroughPartStyles()
{
    struct Overlay {
        const char *type;
        const char *body;
    };
    static constexpr Overlay overlays[] = {
        {"Dialog", "    popupStyle: \"bg-warning\"\n"},
        {"Menu", "    popupStyle: \"bg-warning\"\n"},
        {"Tooltip", "    popupStyle: \"bg-warning\"\n"},
    };

    for (const auto &entry : overlays) {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray document =
            QByteArray("import QtQuick\nimport Loom\nimport Loom.Controls\n") + entry.type
            + " {\n" + entry.body + "}\n";
        QScopedPointer<QObject> popup(createObject(component, document));
        QVERIFY2(
            popup,
            qPrintable(QStringLiteral("%1: %2").arg(
                QString::fromLatin1(entry.type), component.errorString())));

        // Not an Item. This is the whole reason for the part styles.
        QVERIFY2(!qobject_cast<QQuickItem *>(popup.data()), entry.type);

        QQuickItem *const panel = itemProperty(popup.data(), "background");
        QVERIFY2(panel, entry.type);
        QTRY_COMPARE_WITH_TIMEOUT(
            panel->property("color").value<QColor>(), QColor(0xf5, 0x9e, 0x0b), 2000);
    }
}

// Card ships defaults as property bindings rather than as a class string, so a
// call site's own `bg-*` replaces them instead of racing them. The second half
// of this test is the part that would catch a regression to `Lo.style`.
void ControlsTests::cardIsAPaddedSurfaceThatCallSitesCanOverride()
{
    QQmlEngine engine;

    QQmlComponent plain(&engine);
    QScopedPointer<QQuickItem> bare(createItem(
        plain,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Card { }\n"));
    QVERIFY2(bare, qPrintable(plain.errorString()));
    // Real padding, inherited from Box, with no configuration.
    QCOMPARE(bare->property("topPadding").toReal(), 24.0);
    QQuickItem *const surface = itemProperty(bare.data(), "background");
    QVERIFY(surface);
    QCOMPARE(surface->property("radius").toReal(), 8.0);

    QQmlComponent styled(&engine);
    QScopedPointer<QQuickItem> overridden(createItem(
        styled,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Card {\n"
        "    Lo.style: \"bg-blue-500 p-2\"\n"
        "}\n"));
    QVERIFY2(overridden, qPrintable(styled.errorString()));
    QQuickItem *const painted = itemProperty(overridden.data(), "background");
    QVERIFY(painted);
    QTRY_COMPARE(painted->property("color").value<QColor>(), QColor(0x3b, 0x82, 0xf6));
    QTRY_COMPARE(overridden->property("topPadding").toReal(), 8.0);
}

void ControlsTests::progressFillFollowsItsValue()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Progress {\n"
        "    width: 200\n"
        "    value: 0.5\n"
        "    trackStyle: \"bg-success\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QQuickItem *const content = itemProperty(item.data(), "contentItem");
    QVERIFY(content);
    const auto fills = content->childItems();
    QVERIFY(!fills.isEmpty());
    QQuickItem *const fill = fills.first();

    QTRY_COMPARE(fill->property("color").value<QColor>(), QColor(0x16, 0xa3, 0x4a));
    QTRY_COMPARE(fill->width(), content->width() / 2);
}

// A Control rather than a Rectangle, so the pill sizes itself from its label
// instead of needing a width -- the same reason Box exists.
void ControlsTests::badgeGrowsWithItsLabel()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Row {\n"
        "    Badge { objectName: \"short\"; text: \"1\" }\n"
        "    Badge { objectName: \"long\"; text: \"1284\" }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QQuickItem *const shortBadge = item->findChild<QQuickItem *>(QStringLiteral("short"));
    QQuickItem *const longBadge = item->findChild<QQuickItem *>(QStringLiteral("long"));
    QVERIFY(shortBadge);
    QVERIFY(longBadge);
    QTRY_VERIFY(longBadge->implicitWidth() > shortBadge->implicitWidth());
}

void ControlsTests::tabIndicatorFollowsSelection()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Tabs {\n"
        "    width: 300\n"
        "    Tab { objectName: \"first\"; text: \"Overview\" }\n"
        "    Tab { objectName: \"second\"; text: \"Activity\" }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QQuickItem *const first = item->findChild<QQuickItem *>(QStringLiteral("first"));
    QQuickItem *const second = item->findChild<QQuickItem *>(QStringLiteral("second"));
    QVERIFY(first);
    QVERIFY(second);

    const auto firstMarks = itemProperty(first, "background")->childItems();
    const auto secondMarks = itemProperty(second, "background")->childItems();
    QVERIFY(!firstMarks.isEmpty());
    QVERIFY(!secondMarks.isEmpty());

    // TabBar selects the first tab on its own; the underline follows `checked`
    // with nothing wired at the call site.
    QTRY_VERIFY(firstMarks.first()->isVisible());
    QVERIFY(!secondMarks.first()->isVisible());

    item->setProperty("currentIndex", 1);
    QTRY_VERIFY(secondMarks.first()->isVisible());
    QVERIFY(!firstMarks.first()->isVisible());
}

// Qt Quick has no main-axis distribution anywhere: a positioner packs to the
// start, and a Layout spreads through per-child Layout.fillWidth. SpaceBetween
// previously meant an invisible `Item { Layout.fillWidth: true }` spacer, which
// is what the app template reaches for.
void ControlsTests::rowDistributesChildrenOnTheMainAxis()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Row {\n"
        "    width: 300\n"
        "    spacing: 0\n"
        "    justify: Row.SpaceBetween\n"
        "    Rectangle { objectName: \"a\"; width: 40; height: 10 }\n"
        "    Rectangle { objectName: \"b\"; width: 40; height: 10 }\n"
        "    Rectangle { objectName: \"c\"; width: 40; height: 10 }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QQuickItem *const a = item->findChild<QQuickItem *>(QStringLiteral("a"));
    QQuickItem *const b = item->findChild<QQuickItem *>(QStringLiteral("b"));
    QQuickItem *const c = item->findChild<QQuickItem *>(QStringLiteral("c"));
    QVERIFY(a);
    QVERIFY(b);
    QVERIFY(c);

    QTRY_COMPARE(a->x(), 0.0);
    QTRY_COMPARE(c->x(), 260.0);
    QCOMPARE(b->x(), 130.0);

    // Start hands the axis back to the positioner untouched, which is the
    // QtQuick behaviour and has to stay the default.
    item->setProperty("justify", 0);
    item->setProperty("width", 300);
    QTRY_COMPARE(a->x(), 0.0);
}

// Writing the axis the positioner owns re-enters through positioningComplete.
// Without the guard that is a loop, and a loop here does not crash -- it burns
// a core forever while rendering correctly, which is the kind of bug that ships.
void ControlsTests::colDistributionSettlesRatherThanLooping()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\nimport Loom.Controls\n"
        "Col {\n"
        "    property int passes: 0\n"
        "    height: 300\n"
        "    spacing: 0\n"
        "    justify: Col.SpaceEvenly\n"
        "    onPositioningComplete: passes++\n"
        "    Rectangle { objectName: \"a\"; width: 10; height: 40 }\n"
        "    Rectangle { width: 10; height: 40 }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QQuickItem *const a = item->findChild<QQuickItem *>(QStringLiteral("a"));
    QVERIFY(a);
    // free = 300 - 80 = 220, three gaps of 220/3.
    QTRY_COMPARE_WITH_TIMEOUT(a->y(), 220.0 / 3.0, 2000);

    const int settled = item->property("passes").toInt();
    QTest::qWait(50);
    QCOMPARE(item->property("passes").toInt(), settled);
}

// Router has held the route, its params and the history since 0.4 and had no
// rendering half, so it was 88 lines of shipped code with no documentation, no
// example and no user. Applications kept two index-aligned arrays and an
// integer instead -- which is what the gallery does.
void ControlsTests::routeViewResolvesRoutesAndFallsBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString one = writePage(dir, QStringLiteral("One.qml"), QStringLiteral("one"));
    const QString two = writePage(dir, QStringLiteral("Two.qml"), QStringLiteral("two"));
    QVERIFY(!one.isEmpty() && !two.isEmpty());

    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> view(createItem(component, routeViewDocument(one, two)));
    QVERIFY2(view, qPrintable(component.errorString()));

    QObject *const loader = view->property("loader").value<QObject *>();
    QVERIFY(loader);

    QObject *router = nullptr;
    QQmlComponent routerHandle(&engine);
    routerHandle.setData(
        "import QtQuick\nimport Loom\nQtObject { property var r: Router }\n", QUrl());
    QScopedPointer<QObject> holder(routerHandle.create());
    QVERIFY2(holder, qPrintable(routerHandle.errorString()));
    router = holder->property("r").value<QObject *>();
    QVERIFY(router);

    QVERIFY(
        QMetaObject::invokeMethod(
            router, "push", Q_ARG(QString, QStringLiteral("two")),
            Q_ARG(QVariantMap, QVariantMap{})));
    QTRY_COMPARE(loader->property("source").toUrl().toString(), two);
    // Asserting on `source` alone would pass for a URL that resolves to
    // nothing: a Loader that cannot find its file leaves `item` null and says
    // nothing about it, which is how a wrongly-resolved relative path stays
    // invisible. Check what actually loaded.
    QTRY_VERIFY(loader->property("item").value<QObject *>());
    QCOMPARE(
        loader->property("item").value<QObject *>()->property("tag").toString(),
        QStringLiteral("two"));

    QVERIFY(
        QMetaObject::invokeMethod(
            router, "push", Q_ARG(QString, QStringLiteral("nope")),
            Q_ARG(QVariantMap, QVariantMap{})));
    QTRY_COMPARE(loader->property("source").toUrl().toString(), one);
    QTRY_COMPARE(
        loader->property("item").value<QObject *>()->property("tag").toString(),
        QStringLiteral("one"));
}

// The property the whole design turns on. ReloadController::reloadBoundaries()
// repoints a seam Loader's source with setProperty(), which destroys a binding
// permanently -- so `source: routes[Router.route]` would navigate correctly
// until the first hot reload and then silently stop, while still rendering.
// Assigning imperatively and restoring when the Loader reports itself empty is
// what survives that; this simulates the blanking the reload does.
void ControlsTests::routeViewRestoresItsSourceAfterASeamReload()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString one = writePage(dir, QStringLiteral("One.qml"), QStringLiteral("one"));
    const QString two = writePage(dir, QStringLiteral("Two.qml"), QStringLiteral("two"));
    QVERIFY(!one.isEmpty() && !two.isEmpty());

    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> view(createItem(component, routeViewDocument(one, two)));
    QVERIFY2(view, qPrintable(component.errorString()));

    QObject *const loader = view->property("loader").value<QObject *>();
    QVERIFY(loader);
    QTRY_VERIFY(!loader->property("source").toUrl().isEmpty());
    const QUrl before = loader->property("source").toUrl();

    // What the reload does to a seam.
    loader->setProperty("source", QUrl());
    QTRY_COMPARE(loader->property("source").toUrl(), before);
}

QTEST_MAIN(ControlsTests)
#include "tst_controls.moc"
