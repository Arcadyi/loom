#include <QtTest>

#include <QJSValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlListReference>
#include <QQmlProperty>
#include <QQuickItem>
#include <QScopedPointer>
#include <loom/loom.h>

#include "style/loomstyleattached.h"

namespace {

QQuickItem *createItem(QQmlComponent &component, const QByteArray &document)
{
    component.setData(document, QUrl());
    QObject *object = component.create();
    return qobject_cast<QQuickItem *>(object);
}

QObject *objectPointer(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QJSValue>())
        return value.value<QJSValue>().toQObject();
    if (!(value.metaType().flags() & QMetaType::IsPointer))
        return nullptr;
    return *static_cast<QObject *const *>(value.constData());
}

} // namespace

class ApplyTests : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void rectangleUtilities();
    void textUtilities();
    void visibilityUtilitiesHaveDistinctSemantics();
    void styleWinsOverInitialAssignment();
    void unsupportedUtilityWarnsAndSkips();
    void removedClassRestoresOriginal();
    void clearedStyleRestoresEverything();
    void gapAndMargins();
    void layoutMargins();
    void duckTypedPadding();
    void paddingOnPlainItemWarns();
    void trackingIsOrderIndependent();
    void shadowIsChildOfTarget();
    void shadowDoesNotDisturbPositioner();
    void shadowRadiusFollowsBackgroundDelegate();
    void specificityLaterClassWins();
    void backgroundDelegation();
    void backgroundDelegationRestoresOriginal();
    void nonRectangleBackgroundWarns();
    void alphaModifierScalesResolvedColour();
    void anchorsOutsideALayout();
    void layoutAttachedInsideALayout();
    void reparentReroutesBetweenAnchorsAndLayout();
    void layoutOnlyUtilityWarnsOutsideALayout();
    void pinInsideALayoutWarns();
    void aspectRatioDerivesHeightFromWidth();
    void removedAnchorIsReleased();
    void modernVisualUtilitiesApplyAndRestore();
};

void ApplyTests::cleanup()
{
    loom::setTheme(QStringLiteral("light"));
}

void ApplyTests::rectangleUtilities()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"bg-blue-500 rounded-lg opacity-50 w-64 h-8 border-2"
        " border-slate-200 hidden\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0x3b, 0x82, 0xf6));
    QCOMPARE(item->property("radius").toReal(), 8.0);
    QCOMPARE(item->opacity(), 0.5);
    QCOMPARE(item->width(), 256.0);
    QCOMPARE(item->height(), 32.0);
    QCOMPARE(QQmlProperty(item.data(), "border.width").read().toReal(), 2.0);
    QCOMPARE(
        QQmlProperty(item.data(), "border.color").read().value<QColor>(),
        QColor(0xe2, 0xe8, 0xf0));
    QCOMPARE(item->isVisible(), false);
}

void ApplyTests::textUtilities()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Text {\n"
        "    text: \"styled\"\n"
        "    Lo.style: \"text-red-600 text-2xl font-bold font-sans italic underline"
        " tracking-wide\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0xdc, 0x26, 0x26));
    QCOMPARE(QQmlProperty(item.data(), "font.pixelSize").read().toReal(), 24.0);
    QCOMPARE(item->property("lineHeight").toReal(), 32.0);
    QCOMPARE(item->property("lineHeightMode").toInt(), 1);
    QCOMPARE(QQmlProperty(item.data(), "font.weight").read().toInt(), 700);
    QCOMPARE(
        QQmlProperty(item.data(), "font.family").read().toString(),
        QStringLiteral("Sans Serif"));
    QCOMPARE(QQmlProperty(item.data(), "font.italic").read().toBool(), true);
    QCOMPARE(QQmlProperty(item.data(), "font.underline").read().toBool(), true);
    // tracking-wide is 0.025em against the applied 24px size; QFont stores
    // letter spacing in 1/64 steps, hence the tolerance.
    const qreal spacing = QQmlProperty(item.data(), "font.letterSpacing").read().toReal();
    QVERIFY2(qAbs(spacing - 0.6) < 0.02, qPrintable(QString::number(spacing)));
}

void ApplyTests::visibilityUtilitiesHaveDistinctSemantics()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport Loom\n"
        "Item {\n"
        "    property alias hiddenItem: hiddenItem\n"
        "    property alias invisibleItem: invisibleItem\n"
        "    Rectangle { id: hiddenItem; Lo.style: \"hidden\" }\n"
        "    Rectangle { id: invisibleItem; Lo.style: \"invisible\" }\n"
        "}\n",
        QUrl());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *hidden = root->property("hiddenItem").value<QQuickItem *>();
    auto *invisible = root->property("invisibleItem").value<QQuickItem *>();
    QVERIFY(hidden);
    QVERIFY(invisible);
    QTRY_VERIFY(!hidden->isVisible());
    QTRY_COMPARE(invisible->opacity(), 0.0);
    QVERIFY(invisible->isVisible());
}

// Regression: tracking is em-relative and used to resolve against whatever
// `font.pixelSize` happened to hold when its own rule was reached, so it was
// only correct when `text-{size}` appeared earlier in the string.
void ApplyTests::trackingIsOrderIndependent()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Text {\n"
        "    text: \"styled\"\n"
        "    Lo.style: \"tracking-wide text-2xl\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QTRY_COMPARE(QQmlProperty(item.data(), "font.pixelSize").read().toReal(), 24.0);
    // Same 0.025em against 24px as the size-first spelling, not against the
    // default pixel size the item started at.
    const qreal spacing = QQmlProperty(item.data(), "font.letterSpacing").read().toReal();
    QVERIFY2(qAbs(spacing - 0.6) < 0.02, qPrintable(QString::number(spacing)));
}

void ApplyTests::styleWinsOverInitialAssignment()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    color: \"red\"\n"
        "    Lo.style: \"bg-blue-500\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0x3b, 0x82, 0xf6));
}

void ApplyTests::unsupportedUtilityWarnsAndSkips()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    // The warning has to name the utility family and the token. It used to
    // print the rule's key alone, which is empty for every flag utility, so an
    // unsupported `italic` read "Lo.style: utility  is not supported on ...".
    QTest::ignoreMessage(
        QtWarningMsg,
        QRegularExpression(QStringLiteral(
            R"(utility bg-\* \(blue-500\) is not supported on QQuickText)")));
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Text {\n"
        "    text: \"t\"\n"
        "    color: \"black\"\n"
        "    Lo.style: \"bg-blue-500\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTest::qWait(10);
    // bg-* means background; it must not leak into Text's foreground color.
    QCOMPARE(item->property("color").value<QColor>(), QColor(Qt::black));
}

void ApplyTests::removedClassRestoresOriginal()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    property bool fancy: true\n"
        "    radius: 3\n"
        "    Lo.style: fancy ? \"bg-blue-500 rounded-xl\" : \"bg-blue-500\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("radius").toReal(), 12.0);

    item->setProperty("fancy", false);
    QTRY_COMPARE(item->property("radius").toReal(), 3.0);
    QCOMPARE(item->property("color").value<QColor>(), QColor(0x3b, 0x82, 0xf6));
}

void ApplyTests::clearedStyleRestoresEverything()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    color: \"tomato\"\n"
        "    radius: 3\n"
        "    opacity: 0.9\n"
        "    Lo.style: \"bg-blue-500 rounded-xl opacity-50\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("radius").toReal(), 12.0);

    QObject *attached = qmlAttachedPropertiesObject<Lo>(item.data());
    QVERIFY(attached);
    attached->setProperty("style", QString());
    QTRY_COMPARE(item->property("radius").toReal(), 3.0);
    QCOMPARE(item->property("color").value<QColor>(), QColor(QStringLiteral("tomato")));
    QCOMPARE(item->opacity(), 0.9);
}

void ApplyTests::gapAndMargins()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Item {\n"
        "    width: 100; height: 100\n"
        "    Column {\n"
        "        objectName: \"column\"\n"
        "        anchors.fill: parent\n"
        "        Lo.style: \"gap-2 m-4\"\n"
        "    }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QQuickItem *column = item->findChild<QQuickItem *>(QStringLiteral("column"));
    QVERIFY(column);

    QTRY_COMPARE(column->property("spacing").toReal(), 8.0);
    QTRY_COMPARE(QQmlProperty(column, "anchors.topMargin").read().toReal(), 16.0);
    QCOMPARE(QQmlProperty(column, "anchors.leftMargin").read().toReal(), 16.0);
    // Anchored fill plus 16px margins on a 100px parent.
    QTRY_COMPARE(column->width(), 68.0);
}

void ApplyTests::layoutMargins()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    // Inside a Layout, m-* must write the Layout.* attached margins (anchor
    // margins do nothing there).
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport QtQuick.Layouts\nimport Loom\n"
        "RowLayout {\n"
        "    spacing: 0\n"
        "    Rectangle {\n"
        "        objectName: \"cell\"\n"
        "        implicitWidth: 40; implicitHeight: 40\n"
        "        Lo.style: \"m-4 ml-2\"\n"
        "    }\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QQuickItem *cell = item->findChild<QQuickItem *>(QStringLiteral("cell"));
    QVERIFY(cell);

    // Attached properties only resolve through a QML context.
    QQmlContext *context = qmlContext(cell);
    QTRY_COMPARE(QQmlProperty(cell, "Layout.topMargin", context).read().toReal(), 16.0);
    QCOMPARE(QQmlProperty(cell, "Layout.rightMargin", context).read().toReal(), 16.0);
    // ml-2 comes later at equal specificity, so it wins the left side.
    QCOMPARE(QQmlProperty(cell, "Layout.leftMargin", context).read().toReal(), 8.0);
    // Geometry is not asserted: layouts only relayout through the polish
    // cycle of a real window, and honoring its own margins is Qt's contract.
}

void ApplyTests::duckTypedPadding()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    // Any type declaring the conventional per-side padding properties opts in
    // (all Quick Controls do; this fixture stands in for them).
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Item {\n"
        "    property real topPadding: 0\n"
        "    property real rightPadding: 0\n"
        "    property real bottomPadding: 0\n"
        "    property real leftPadding: 0\n"
        "    Lo.style: \"p-4 px-6\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QTRY_COMPARE(item->property("topPadding").toReal(), 16.0);
    QCOMPARE(item->property("bottomPadding").toReal(), 16.0);
    // px-6 comes later at equal specificity, so it wins the horizontal sides.
    QCOMPARE(item->property("leftPadding").toReal(), 24.0);
    QCOMPARE(item->property("rightPadding").toReal(), 24.0);
}

void ApplyTests::paddingOnPlainItemWarns()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    for (int i = 0; i < 4; ++i)
        QTest::ignoreMessage(
            QtWarningMsg, QRegularExpression(QStringLiteral("not supported on")));
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"p-4 bg-blue-500\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0x3b, 0x82, 0xf6));
}

void ApplyTests::shadowIsChildOfTarget()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> root(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Item {\n"
        "    width: 300; height: 300\n"
        "    Rectangle {\n"
        "        objectName: \"card\"\n"
        "        property bool floating: true\n"
        "        radius: 5\n"
        "        width: 100; height: 100\n"
        "        Lo.style: floating ? \"bg-white shadow-md\" : \"bg-white shadow-none\"\n"
        "    }\n"
        "}\n"));
    QVERIFY2(root, qPrintable(component.errorString()));
    QQuickItem *card = root->findChild<QQuickItem *>(QStringLiteral("card"));
    QVERIFY(card);

    // The managed RectangularShadow is a child of the card, so it stays out of
    // whatever layout the card itself participates in.
    QTRY_COMPARE(card->childItems().size(), 1);
    QCOMPARE(root->childItems().size(), 1);
    QQuickItem *shadow = card->childItems().first();
    QCOMPARE(shadow->parentItem(), card);
    QCOMPARE(shadow->width(), 100.0);
    QCOMPARE(shadow->property("radius").toReal(), 5.0);
    QCOMPARE(shadow->property("blur").toReal(), 6.0);
    QCOMPARE(shadow->property("color").value<QColor>(), QColor(0, 0, 0, 25));
    // Negative z draws it beneath the card's own background.
    QVERIFY(shadow->z() < 0);

    // shadow-none removes the managed item again.
    card->setProperty("floating", false);
    QTRY_COMPARE(card->childItems().size(), 0);
}

// Regression: the shadow used to be a sibling parented into `target.parent`.
// Inside a positioner that made it a laid-out child of its own, so it took a
// slot and its x/y bindings fought the positioner's writes -- shipped broken in
// the gallery's own Theming and Tokens pages.
void ApplyTests::shadowDoesNotDisturbPositioner()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> root(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Column {\n"
        "    spacing: 0\n"
        "    Rectangle {\n"
        "        objectName: \"first\"\n"
        "        width: 100; height: 50\n"
        "        Lo.style: \"bg-blue-500 shadow-md\"\n"
        "    }\n"
        "    Rectangle {\n"
        "        objectName: \"second\"\n"
        "        width: 100; height: 50\n"
        "    }\n"
        "}\n"));
    QVERIFY2(root, qPrintable(component.errorString()));
    QQuickItem *first = root->findChild<QQuickItem *>(QStringLiteral("first"));
    QQuickItem *second = root->findChild<QQuickItem *>(QStringLiteral("second"));
    QVERIFY(first);
    QVERIFY(second);

    // Once the style has landed the shadow exists; the Column must still see
    // exactly its two rectangles and lay them out as if nothing were added.
    // (A distinctive colour, so the wait cannot pass on a Rectangle's white
    // default before the deferred first apply has run at all.)
    QTRY_COMPARE(first->property("color").value<QColor>(), QColor(0x3b, 0x82, 0xf6));
    QCOMPARE(root->childItems().size(), 2);
    QCOMPARE(first->y(), 0.0);
    QCOMPARE(second->y(), 50.0);
    QCOMPARE(root->height(), 100.0);
    QCOMPARE(first->childItems().size(), 1);
}

// Regression: `rounded-*` lands on a Control's background delegate, but the
// shadow read `target.radius` -- undefined on a Control, so 0. A rounded button
// cast a square shadow.
void ApplyTests::shadowRadiusFollowsBackgroundDelegate()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Item {\n"
        "    property Item background: Rectangle { parent: null }\n"
        "    width: 80; height: 36\n"
        "    Lo.style: \"bg-blue-500 rounded-xl shadow-lg\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QTRY_COMPARE(item->childItems().size(), 1);
    QQuickItem *shadow = item->childItems().first();
    // rounded-xl is 12, written through to background.radius.
    QCOMPARE(QQmlProperty(item.data(), "background.radius").read().toReal(), 12.0);
    QCOMPARE(shadow->property("radius").toReal(), 12.0);
}

void ApplyTests::specificityLaterClassWins()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"bg-blue-500 bg-red-500\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0xef, 0x44, 0x44));
}

// A Control-shaped stand-in: loom does not depend on QtQuick.Controls, so the
// delegation is exercised through the same duck-typed `background` property a
// Control exposes.
void ApplyTests::backgroundDelegation()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Item {\n"
        "    property Item background: Rectangle { parent: null }\n"
        "    width: 36\n"
        "    height: 36\n"
        "    Lo.style: \"bg-blue-500 rounded-full border-2 border-slate-200\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QTRY_COMPARE(
        QQmlProperty(item.data(), "background.color").read().value<QColor>(),
        QColor(0x3b, 0x82, 0xf6));
    // rounded-full is 9999; Qt clamps it to half the shorter side when painting.
    QCOMPARE(QQmlProperty(item.data(), "background.radius").read().toReal(), 9999.0);
    QCOMPARE(QQmlProperty(item.data(), "background.border.width").read().toReal(), 2.0);
    QCOMPARE(
        QQmlProperty(item.data(), "background.border.color").read().value<QColor>(),
        QColor(0xe2, 0xe8, 0xf0));
    // The utility landed on the delegate, not on the control itself.
    QVERIFY(!item->property("color").isValid());
}

void ApplyTests::backgroundDelegationRestoresOriginal()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Item {\n"
        "    property bool styled: true\n"
        "    property Item background: Rectangle { parent: null; color: \"red\" }\n"
        "    Lo.style: styled ? \"bg-blue-500\" : \"\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QTRY_COMPARE(
        QQmlProperty(item.data(), "background.color").read().value<QColor>(),
        QColor(0x3b, 0x82, 0xf6));
    item->setProperty("styled", false);
    QTRY_COMPARE(
        QQmlProperty(item.data(), "background.color").read().value<QColor>(),
        QColor(0xff, 0x00, 0x00));
}

// Delegation only happens when the delegate can actually take the utility; a
// non-Rectangle background still reports the class as unsupported.
void ApplyTests::nonRectangleBackgroundWarns()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QTest::ignoreMessage(
        QtWarningMsg, QRegularExpression(QStringLiteral("not supported on")));
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Item {\n"
        "    property Item background: Item { parent: null }\n"
        // opacity-50 lands on the item itself, so waiting for it pumps the
        // deferred apply that emits the warning for bg-blue-500.
        "    Lo.style: \"bg-blue-500 opacity-50\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->opacity(), 0.5);
    QVERIFY(!QQmlProperty(item.data(), "background.color").isValid());
}

void ApplyTests::alphaModifierScalesResolvedColour()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    Lo.style: \"bg-blue-500/70 border-2 border-slate-200/25\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    // Same RGB as the bare token; only the alpha channel moves.
    QColor expected(0x3b, 0x82, 0xf6);
    expected.setAlphaF(0.7f);
    QTRY_COMPARE(item->property("color").value<QColor>(), expected);

    QColor expectedBorder(0xe2, 0xe8, 0xf0);
    expectedBorder.setAlphaF(0.25f);
    QCOMPARE(
        QQmlProperty(item.data(), "border.color").read().value<QColor>(), expectedBorder);

    // The modifier travels with the token, not the resolved value: switching
    // theme must re-resolve and re-apply the alpha, not strand the old colour.
    QQmlComponent themed(&engine);
    QScopedPointer<QQuickItem> surface(createItem(
        themed,
        "import QtQuick\nimport Loom\n"
        "Rectangle { Lo.style: \"bg-surface/50\" }\n"));
    QVERIFY2(surface, qPrintable(themed.errorString()));
    // Compared in percent: QColor keeps 16-bit channels, so a round-tripped
    // 0.5 reads back as 0.5000076.
    const auto alphaPercent = [](const QQuickItem *target) {
        return qRound(target->property("color").value<QColor>().alphaF() * 100.0f);
    };
    QTRY_COMPARE(alphaPercent(surface.data()), 50);
    const QColor light = surface->property("color").value<QColor>();

    loom::setTheme(QStringLiteral("dark"));
    // Wait on the re-resolve itself; asserting the alpha first would pass
    // against the pre-switch colour and prove nothing.
    QTRY_VERIFY(surface->property("color").value<QColor>().rgb() != light.rgb());
    QCOMPARE(alphaPercent(surface.data()), 50);
}

void ApplyTests::modernVisualUtilitiesApplyAndRestore()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle {\n"
        "    width: 100; height: 80\n"
        "    Lo.effects: true\n"
        "    Lo.style: \"overflow-hidden z-7 rotate-12 scale-110 "
        "translate-x-[5px] translate-y-[7px] ring-2 ring-blue-500 "
        "bg-linear-to-r from-red-500 to-blue-500 brightness-125\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QTRY_COMPARE(item->z(), 7.0);
    QCOMPARE(item->clip(), true);
    QCOMPARE(item->rotation(), 12.0);
    QCOMPARE(item->scale(), 1.1);

    QQmlListReference transforms(item.data(), "transform");
    QTRY_COMPARE(transforms.count(), 1);
    QObject *translate = transforms.at(0);
    QVERIFY(translate);
    QCOMPARE(translate->property("x").toReal(), 5.0);
    QCOMPARE(translate->property("y").toReal(), 7.0);

    QTRY_COMPARE(item->childItems().size(), 1);
    QQuickItem *ring = item->childItems().constFirst();
    QCOMPARE(QQmlProperty(ring, "border.width").read().toReal(), 2.0);
    QCOMPARE(
        QQmlProperty(ring, "border.color").read().value<QColor>(),
        QColor(0x3b, 0x82, 0xf6));

    // These are typed QML pointer properties (QQuickGradient* and
    // QQmlComponent*). QVariant does not down-convert either to QObject*, so
    // inspect the typed pointer payload without depending on private Qt Quick
    // types.
    QTRY_VERIFY(objectPointer(QQmlProperty(item.data(), "gradient").read()));
    QTRY_VERIFY(QQmlProperty(item.data(), "layer.enabled").read().toBool());
    QVERIFY(objectPointer(QQmlProperty(item.data(), "layer.effect").read()));

    QObject *attached = qmlAttachedPropertiesObject<Lo>(item.data());
    QVERIFY(attached);
    attached->setProperty("style", QString());
    QTRY_COMPARE(item->z(), 0.0);
    QCOMPARE(item->clip(), false);
    QCOMPARE(item->rotation(), 0.0);
    QCOMPARE(item->scale(), 1.0);
    QTRY_COMPARE(transforms.count(), 0);
    QTRY_COMPARE(item->childItems().size(), 0);
    QTRY_VERIFY(!objectPointer(QQmlProperty(item.data(), "gradient").read()));
    QTRY_VERIFY(!QQmlProperty(item.data(), "layer.enabled").read().toBool());
}

// Outside a QtQuick.Layouts layout the layout family writes anchors, and the
// anchors are real: the item tracks its parent's geometry rather than just
// receiving a number once.
void ApplyTests::anchorsOutsideALayout()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> root(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Item {\n"
        "    width: 200; height: 100\n"
        "    Rectangle { objectName: \"filled\"; Lo.style: \"fill m-4\" }\n"
        "    Rectangle {\n"
        "        objectName: \"centred\"; width: 20; height: 20\n"
        "        Lo.style: \"center\"\n"
        "    }\n"
        "    Rectangle {\n"
        "        objectName: \"pinned\"; width: 10; height: 10\n"
        "        Lo.style: \"pin-l\"\n"
        "    }\n"
        "}\n"));
    QVERIFY2(root, qPrintable(component.errorString()));

    QQuickItem *filled = root->findChild<QQuickItem *>(QStringLiteral("filled"));
    QQuickItem *centred = root->findChild<QQuickItem *>(QStringLiteral("centred"));
    QQuickItem *pinned = root->findChild<QQuickItem *>(QStringLiteral("pinned"));
    QVERIFY(filled && centred && pinned);

    // `fill` plus the existing m-* margins: the anchor is what finally makes
    // those margins do something. `m-4` is spacing step 4, which is 16px.
    QTRY_COMPARE(filled->width(), 168.0);
    QCOMPARE(filled->height(), 68.0);
    QCOMPARE(filled->x(), 16.0);

    QCOMPARE(centred->x(), 90.0);
    QCOMPARE(centred->y(), 40.0);
    QCOMPARE(pinned->x(), 0.0);

    // Still anchored, not merely positioned once.
    root->setWidth(300);
    QTRY_COMPARE(filled->width(), 268.0);
    QCOMPARE(centred->x(), 140.0);
}

void ApplyTests::layoutAttachedInsideALayout()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> root(createItem(
        component,
        "import QtQuick\nimport QtQuick.Layouts\nimport Loom\n"
        "ColumnLayout {\n"
        "    width: 200; height: 200\n"
        "    Rectangle {\n"
        "        objectName: \"grown\"; implicitHeight: 10\n"
        "        Lo.style: \"fill min-w-16 max-w-64\"\n"
        "    }\n"
        "    Rectangle {\n"
        "        objectName: \"aligned\"; implicitWidth: 20; implicitHeight: 20\n"
        "        Lo.style: \"self-center\"\n"
        "    }\n"
        "}\n"));
    QVERIFY2(root, qPrintable(component.errorString()));

    QQuickItem *grown = root->findChild<QQuickItem *>(QStringLiteral("grown"));
    QQuickItem *aligned = root->findChild<QQuickItem *>(QStringLiteral("aligned"));
    QVERIFY(grown && aligned);
    QQmlContext *context = qmlContext(grown);

    // `fill` in a layout is fillWidth + fillHeight, not anchors -- anchoring an
    // item a layout manages is undefined behaviour Qt warns about.
    QTRY_COMPARE(QQmlProperty(grown, "Layout.fillWidth", context).read().toBool(), true);
    QCOMPARE(QQmlProperty(grown, "Layout.fillHeight", context).read().toBool(), true);
    QCOMPARE(
        QQmlProperty(grown, "anchors.fill", context).read().value<QQuickItem *>(),
        nullptr);

    QCOMPARE(QQmlProperty(grown, "Layout.minimumWidth", context).read().toReal(), 64.0);
    QCOMPARE(QQmlProperty(grown, "Layout.maximumWidth", context).read().toReal(), 256.0);

    QCOMPARE(
        QQmlProperty(aligned, "Layout.alignment", context).read().toInt(),
        int(Qt::AlignCenter));
}

// The routing is per instance and re-resolved on every apply, so moving an item
// between the two worlds has to move the write with it.
void ApplyTests::reparentReroutesBetweenAnchorsAndLayout()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> root(createItem(
        component,
        "import QtQuick\nimport QtQuick.Layouts\nimport Loom\n"
        "Item {\n"
        "    width: 200; height: 200\n"
        "    property alias mover: mover\n"
        "    property alias column: column\n"
        "    ColumnLayout { id: column; width: 100; height: 100 }\n"
        "    Rectangle { id: mover; Lo.style: \"fill\" }\n"
        "}\n"));
    QVERIFY2(root, qPrintable(component.errorString()));

    QQuickItem *mover = root->property("mover").value<QQuickItem *>();
    QQuickItem *column = root->property("column").value<QQuickItem *>();
    QVERIFY(mover && column);
    QQmlContext *context = qmlContext(mover);

    QTRY_COMPARE(mover->width(), 200.0);
    QCOMPARE(
        QQmlProperty(mover, "anchors.fill", context).read().value<QQuickItem *>(),
        root.data());

    mover->setParentItem(column);
    // The anchor is released and the Layout form takes over.
    QTRY_COMPARE(QQmlProperty(mover, "Layout.fillWidth", context).read().toBool(), true);
    QCOMPARE(
        QQmlProperty(mover, "anchors.fill", context).read().value<QQuickItem *>(),
        nullptr);
}

void ApplyTests::layoutOnlyUtilityWarnsOutsideALayout()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QTest::ignoreMessage(
        QtWarningMsg,
        QRegularExpression(
            QStringLiteral(R"(utility self-\* only applies inside a RowLayout)")));
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle { Lo.style: \"self-center\" }\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTest::qWait(10);
}

void ApplyTests::pinInsideALayoutWarns()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    // A pin has no layout form that would not fight the layout's own placement.
    // The warning has to say *that*, not "not supported on QQuickRectangle" --
    // pin-l is perfectly supported on a Rectangle, just not inside a layout.
    QTest::ignoreMessage(
        QtWarningMsg,
        QRegularExpression(QStringLiteral(
            R"(utility pin-l has no form inside a layout.*use self-start)")));
    QScopedPointer<QQuickItem> root(createItem(
        component,
        "import QtQuick\nimport QtQuick.Layouts\nimport Loom\n"
        "ColumnLayout {\n"
        "    width: 100; height: 100\n"
        "    Rectangle { objectName: \"child\"; Lo.style: \"pin-l\" }\n"
        "}\n"));
    QVERIFY2(root, qPrintable(component.errorString()));
    QTest::qWait(10);
}

void ApplyTests::aspectRatioDerivesHeightFromWidth()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> item(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Rectangle { width: 80; Lo.style: \"aspect-square\" }\n"));
    QVERIFY2(item, qPrintable(component.errorString()));
    QTRY_COMPARE(item->height(), 80.0);

    // Tracks the width, which is the whole point.
    item->setWidth(120);
    QTRY_COMPARE(item->height(), 120.0);

    QQmlComponent widescreen(&engine);
    QScopedPointer<QQuickItem> video(createItem(
        widescreen,
        "import QtQuick\nimport Loom\n"
        "Rectangle { width: 160; Lo.style: \"aspect-16/9\" }\n"));
    QVERIFY2(video, qPrintable(widescreen.errorString()));
    QTRY_COMPARE(video->height(), 90.0);
}

// Anchors cannot be released by writing the saved value back -- a default
// anchor line names no item and Qt refuses it. Without the reset() fallback the
// item would stay anchored after the class stopped applying.
void ApplyTests::removedAnchorIsReleased()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QQuickItem> root(createItem(
        component,
        "import QtQuick\nimport Loom\n"
        "Item {\n"
        "    width: 200; height: 100\n"
        "    Rectangle {\n"
        "        objectName: \"child\"\n"
        "        property bool stretched: true\n"
        "        Lo.style: stretched ? \"fill\" : \"bg-surface\"\n"
        "    }\n"
        "}\n"));
    QVERIFY2(root, qPrintable(component.errorString()));
    QQuickItem *child = root->findChild<QQuickItem *>(QStringLiteral("child"));
    QVERIFY(child);
    QTRY_COMPARE(child->width(), 200.0);

    child->setProperty("stretched", false);
    QTRY_COMPARE(
        QQmlProperty(child, "anchors.fill", qmlContext(child))
            .read()
            .value<QQuickItem *>(),
        nullptr);

    // Released, so the size is the item's own again.
    child->setWidth(30);
    QTRY_COMPARE(child->width(), 30.0);
    root->setWidth(400);
    QCOMPARE(child->width(), 30.0);
}

QTEST_MAIN(ApplyTests)
#include "tst_apply.moc"
