#include <QtTest>

#include <QQmlComponent>
#include <QQmlEngine>
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

} // namespace

class ApplyTests : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void rectangleUtilities();
    void textUtilities();
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
        " border-slate-200 invisible\"\n"
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
        "    Lo.style: \"text-red-600 text-2xl font-bold italic underline"
        " tracking-wide\"\n"
        "}\n"));
    QVERIFY2(item, qPrintable(component.errorString()));

    QTRY_COMPARE(item->property("color").value<QColor>(), QColor(0xdc, 0x26, 0x26));
    QCOMPARE(QQmlProperty(item.data(), "font.pixelSize").read().toReal(), 24.0);
    QCOMPARE(item->property("lineHeight").toReal(), 32.0);
    QCOMPARE(item->property("lineHeightMode").toInt(), 1);
    QCOMPARE(QQmlProperty(item.data(), "font.weight").read().toInt(), 700);
    QCOMPARE(QQmlProperty(item.data(), "font.italic").read().toBool(), true);
    QCOMPARE(QQmlProperty(item.data(), "font.underline").read().toBool(), true);
    // tracking-wide is 0.025em against the applied 24px size; QFont stores
    // letter spacing in 1/64 steps, hence the tolerance.
    const qreal spacing = QQmlProperty(item.data(), "font.letterSpacing").read().toReal();
    QVERIFY2(qAbs(spacing - 0.6) < 0.02, qPrintable(QString::number(spacing)));
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

QTEST_MAIN(ApplyTests)
#include "tst_apply.moc"
