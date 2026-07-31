#include <QtTest>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QScopedPointer>

#include "statecapture.h"

using namespace loom;

namespace {

QObject *build(QQmlComponent &component, const QByteArray &body)
{
    component.setData(
        QByteArray("import QtQuick\n") + body, QUrl(QStringLiteral("Scene.qml")));
    QObject *object = component.create();
    if (!object)
        qWarning("%s", qPrintable(component.errorString()));
    return object;
}

constexpr auto scene = R"(
Item {
    id: appRoot
    property string currentPage: "home"
    property int base: 21
    property int doubled: base * 2
    readonly property string fixed: "constant"
    Item {
        id: searchField
        property string text: ""
    }
    Item {
        property string anonymous: "no id, unaddressable"
    }
}
)";

} // namespace

class StateCaptureTests : public QObject {
    Q_OBJECT

private slots:
    void capturesDeclaredPropertiesOfIdBearingObjects();
    void skipsBoundAndReadonlyProperties();
    void restoresValuesIntoAFreshScene();
    void restoreLeavesBindingsIntact();
    void ignoresInheritedCppProperties();
    void ignoresQmlBelongingToOtherModules();
    void ignoresKeysWithNoMatchingObject();
};

void StateCaptureTests::capturesDeclaredPropertiesOfIdBearingObjects()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QObject> root(build(component, scene));
    QVERIFY(root);
    root->setProperty("currentPage", QStringLiteral("trending"));

    const QVariantMap state = captureSceneState(root.data());

    // Keyed by file and QML id, with nothing annotated in the scene.
    QVERIFY(state.contains(QStringLiteral("Scene.qml#appRoot")));
    QVERIFY(state.contains(QStringLiteral("Scene.qml#searchField")));
    QCOMPARE(
        state.value(QStringLiteral("Scene.qml#appRoot"))
            .toMap()
            .value(QStringLiteral("currentPage")),
        QVariant(QStringLiteral("trending")));

    // An object with no id cannot be addressed on the way back in, so it is
    // never captured in the first place.
    for (const QString &key : state.keys())
        QVERIFY(!state.value(key).toMap().contains(QStringLiteral("anonymous")));
}

void StateCaptureTests::skipsBoundAndReadonlyProperties()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QObject> root(build(component, scene));
    QVERIFY(root);

    const QVariantMap appRoot =
        captureSceneState(root.data()).value(QStringLiteral("Scene.qml#appRoot")).toMap();

    QVERIFY(appRoot.contains(QStringLiteral("currentPage")));
    QVERIFY(appRoot.contains(QStringLiteral("base")));
    // `doubled: base * 2` is a binding. Capturing it would mean writing it back
    // as a stored value later, which is what destroys the binding.
    QVERIFY(!appRoot.contains(QStringLiteral("doubled")));
    QVERIFY(!appRoot.contains(QStringLiteral("fixed")));
}

void StateCaptureTests::restoresValuesIntoAFreshScene()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QObject> first(build(component, scene));
    QVERIFY(first);
    first->setProperty("currentPage", QStringLiteral("mylist"));
    first->findChild<QObject *>()->setProperty("text", QStringLiteral("qt"));
    const QVariantMap state = captureSceneState(first.data());

    QQmlComponent second(&engine);
    QScopedPointer<QObject> reloaded(build(second, scene));
    QVERIFY(reloaded);
    QCOMPARE(reloaded->property("currentPage").toString(), QStringLiteral("home"));

    applySceneState(reloaded.data(), state);

    QCOMPARE(reloaded->property("currentPage").toString(), QStringLiteral("mylist"));
    // Nested state survives without the root funnelling anything: the point of
    // the whole exercise.
    QCOMPARE(
        reloaded->findChild<QObject *>()->property("text").toString(),
        QStringLiteral("qt"));
}

void StateCaptureTests::restoreLeavesBindingsIntact()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QObject> first(build(component, scene));
    QVERIFY(first);
    first->setProperty("base", 100);
    QCOMPARE(first->property("doubled").toInt(), 200);
    const QVariantMap state = captureSceneState(first.data());

    QQmlComponent second(&engine);
    QScopedPointer<QObject> reloaded(build(second, scene));
    QVERIFY(reloaded);
    applySceneState(reloaded.data(), state);

    QCOMPARE(reloaded->property("base").toInt(), 100);
    QCOMPARE(reloaded->property("doubled").toInt(), 200);
    // The real assertion: `doubled` is still live, not frozen at the restored
    // number. A capture that could not see bindings would fail here.
    reloaded->setProperty("base", 7);
    QCOMPARE(reloaded->property("doubled").toInt(), 14);
}

void StateCaptureTests::ignoresInheritedCppProperties()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    // An id-bearing object whose leaf type is plain C++ -- it declares nothing,
    // so QML synthesises no subclass and propertyOffset() points into
    // QQuickItem's own properties.
    QScopedPointer<QObject> root(build(component, R"(
        Item {
            id: appRoot
            property string kept: "yes"
            Item { id: plain; clip: true; opacity: 0.5 }
        }
    )"));
    QVERIFY(root);

    const QVariantMap state = captureSceneState(root.data());

    QCOMPARE(
        state.value(QStringLiteral("Scene.qml#appRoot"))
            .toMap()
            .value(QStringLiteral("kept")),
        QVariant(QStringLiteral("yes")));
    // Geometry, focus and painting belong to the new scene, which sets them up
    // for itself. Carrying a snapshot of them over is how a reload used to
    // resurrect stale layout.
    QVERIFY(!state.contains(QStringLiteral("Scene.qml#plain")));
    for (const QString &key : state.keys()) {
        const QVariantMap values = state.value(key).toMap();
        QVERIFY(!values.contains(QStringLiteral("clip")));
        QVERIFY(!values.contains(QStringLiteral("opacity")));
    }
}

void StateCaptureTests::ignoresQmlBelongingToOtherModules()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    // Qt's own controls are QML, and their internals carry ids and declared
    // properties: a bare TextField contributes selectionStartHandle and
    // friends. None of it is the application's state to restore.
    QScopedPointer<QObject> root(build(component, R"(
        import QtQuick.Controls
        Item {
            id: appRoot
            property string kept: "yes"
            TextField { id: field }
        }
    )"));
    QVERIFY(root);

    const QVariantMap state = captureSceneState(root.data());

    QVERIFY(state.contains(QStringLiteral("Scene.qml#appRoot")));
    for (const QString &key : state.keys())
        QVERIFY2(
            key.startsWith(QStringLiteral("Scene.qml#")),
            qPrintable(QStringLiteral("captured foreign QML: ") + key));
}

void StateCaptureTests::ignoresKeysWithNoMatchingObject()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QObject> root(build(component, scene));
    QVERIFY(root);

    QVariantMap stale;
    stale.insert(
        QStringLiteral("Deleted.qml#gone"),
        QVariantMap{{QStringLiteral("currentPage"), QStringLiteral("nowhere")}});
    applySceneState(root.data(), stale);

    // A renamed or deleted component leaves its state behind harmlessly.
    QCOMPARE(root->property("currentPage").toString(), QStringLiteral("home"));
}

QTEST_MAIN(StateCaptureTests)
#include "tst_statecapture.moc"
