#include <loom/application.h>
#include <loom/loom.h>
#include <loom/reloadcontroller.h>

#include <QByteArray>
#include <QHostAddress>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>

namespace loom {

namespace {
// Only ever used by a scene that asked for no size at all.
constexpr int DefaultWindowWidth = 1100;
constexpr int DefaultWindowHeight = 720;
} // namespace

void Application::connectDevelopmentRuntime()
{
    auto host = qEnvironmentVariable("LOOM_DEV_HOST", QStringLiteral("127.0.0.1"));
    auto rawPort = qEnvironmentVariable("LOOM_DEV_PORT");
    auto token = qEnvironmentVariable("LOOM_DEV_TOKEN");
    bool allowRemote = false;
    for (const QString &argument : QCoreApplication::arguments()) {
        if (argument.startsWith(QLatin1String("--loom-dev-host=")))
            host = argument.mid(qstrlen("--loom-dev-host="));
        else if (argument.startsWith(QLatin1String("--loom-dev-port=")))
            rawPort = argument.mid(qstrlen("--loom-dev-port="));
        else if (argument.startsWith(QLatin1String("--loom-dev-token=")))
            token = argument.mid(qstrlen("--loom-dev-token="));
        else if (argument == QLatin1String("--loom-dev-allow-remote"))
            allowRemote = true;
    }

    if (rawPort.isEmpty() && token.isEmpty())
        return;

    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        host = QStringLiteral("127.0.0.1");
    // A reload bundle is executable QML, so only ever accept one from this
    // machine. Without this the runtime would happily run code served by an
    // arbitrary remote host.
    const QHostAddress address(host);
    if (address.isNull() || (!address.isLoopback() && !allowRemote)) {
        qWarning(
            "loom: refusing development host '%s'; non-loopback needs the explicit "
            "--loom-dev-allow-remote flag",
            qUtf8Printable(host));
        return;
    }

    bool portValid = false;
    const auto port = rawPort.toUInt(&portValid);
    if (!portValid || port == 0 || port > 65535) {
        qWarning(
            "loom: LOOM_DEV_PORT '%s' is not a valid port; "
            "hot reload is disabled",
            qUtf8Printable(rawPort));
        return;
    }
    if (token.isEmpty()) {
        qWarning("loom: LOOM_DEV_TOKEN is not set; hot reload is disabled");
        return;
    }

    m_reloadController->connectToDevelopmentServer(
        host, static_cast<quint16>(port), token);
}

void Application::loadCompiledDesign()
{
#ifdef LOOM_APP_DESIGN
    // LOOM_APP_DESIGN is defined by loom_add_application when the project has a
    // DESIGN file, so its absence means the project declared no design tokens
    // rather than that something went wrong. loadConfig warns on its own if the
    // resource is somehow missing.
    loom::loadConfig(QStringLiteral(LOOM_APP_DESIGN));
#endif
}

void Application::installInspector()
{
    if (!m_developmentRuntimeEnabled
        || (qEnvironmentVariableIsSet("LOOM_INSPECTOR")
            && qEnvironmentVariableIntValue("LOOM_INSPECTOR") == 0))
        return;
    if (m_inspector)
        m_inspector->deleteLater();
    m_inspector.clear();

    QObject *root = m_reloadController ? m_reloadController->rootObject() : nullptr;
    QQuickItem *targetRoot = qobject_cast<QQuickItem *>(root);
    if (auto *window = qobject_cast<QQuickWindow *>(root))
        targetRoot = window->contentItem();
    if (!targetRoot)
        return;

    static constexpr char inspectorQml[] = R"LOOM_INSPECTOR(import QtQuick
import Loom

Item {
    id: inspector
    required property Item targetRoot
    parent: targetRoot
    anchors.fill: parent
    z: 2147483647
    visible: false

    property Item inspected: null
    property bool locked: false

    function pick(item, rootX, rootY) {
        const children = item.children
        for (let index = children.length - 1; index >= 0; --index) {
            const child = children[index]
            if (!child || child === inspector || !child.visible)
                continue
            const local = child.mapFromItem(targetRoot, rootX, rootY)
            if (local.x < 0 || local.y < 0 || local.x > child.width || local.y > child.height)
                continue
            const nested = pick(child, rootX, rootY)
            if (nested)
                return nested
            if (Loom.inspect(child).style)
                return child
        }
        return item !== targetRoot && Loom.inspect(item).style ? item : null
    }

    function description() {
        if (!inspected)
            return "Move over a styled item"
        const info = Loom.inspect(inspected)
        let lines = [info.type + (info.objectName ? " #" + info.objectName : ""),
                     "Lo.style: " + (info.style || "(empty)"),
                     "theme: " + info.theme,
                     "states: " + (info.states.length ? info.states.join(", ") : "none")]
        const values = info.resolved || ({})
        const keys = Object.keys(values).sort()
        if (keys.length)
            lines.push("resolved:")
        for (let index = 0; index < keys.length; ++index)
            lines.push("  " + keys[index] + ": " + values[keys[index]])
        return lines.join("\n")
    }

    Shortcut {
        sequence: "Ctrl+Shift+I"
        onActivated: {
            inspector.visible = !inspector.visible
            inspector.locked = false
            if (!inspector.visible)
                inspector.inspected = null
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor
        onPositionChanged: mouse => {
            if (!inspector.locked)
                inspector.inspected = inspector.pick(inspector.targetRoot, mouse.x, mouse.y)
        }
        onClicked: inspector.locked = !inspector.locked
    }

    Rectangle {
        visible: inspector.inspected !== null
        color: "transparent"
        border.width: 2
        border.color: "#7c3aed"
        x: inspected ? inspected.mapToItem(targetRoot, 0, 0).x : 0
        y: inspected ? inspected.mapToItem(targetRoot, 0, 0).y : 0
        width: inspected ? inspected.width : 0
        height: inspected ? inspected.height : 0
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        width: Math.min(460, parent.width - 24)
        height: Math.min(details.implicitHeight + 24, parent.height - 24)
        radius: 8
        color: "#ee111827"
        border.color: "#7c3aed"
        border.width: 1

        Text {
            id: details
            anchors.fill: parent
            anchors.margins: 12
            color: "#f8fafc"
            font.family: "monospace"
            font.pixelSize: 12
            wrapMode: Text.WrapAnywhere
            text: inspector.description()
        }
    }
}
)LOOM_INSPECTOR";

    QQmlComponent component(&m_engine);
    component.setData(inspectorQml, QUrl());
    QObject *created = component.createWithInitialProperties(
        {{QStringLiteral("targetRoot"), QVariant::fromValue(targetRoot)}});
    if (!created) {
        qWarning(
            "loom: could not create development inspector: %s",
            qUtf8Printable(component.errorString()));
        return;
    }
    created->setParent(targetRoot);
    created->setProperty("_loomInternal", true);
    m_inspector = created;
}

Application::Application(int &argc, char **argv)
    : m_application(argc, argv)
{
}

Application::~Application() = default;

void Application::setEngineInitializer(EngineInitializer initializer)
{
    m_initializer = std::move(initializer);
}

void Application::enableDevelopmentRuntime(const bool enabled)
{
    m_developmentRuntimeEnabled = enabled;
}

// A scene rooted at a Window is that window, so every reload of the document
// takes the window with it -- position, size, focus and all. A scene rooted at
// an Item has nothing to show it in at all, which used to mean it simply never
// appeared. Giving that case a window here answers both: the window belongs to
// the process rather than to the document, so it outlives every reload.
void Application::hostScene()
{
    QObject *root = m_reloadController ? m_reloadController->rootObject() : nullptr;
    auto *item = qobject_cast<QQuickItem *>(root);
    if (!item || qobject_cast<QQuickWindow *>(root))
        return;

    if (!m_hostWindow) {
        m_hostWindow = std::make_unique<QQuickWindow>();
        m_hostWindow->setTitle(QCoreApplication::applicationName());
        // Whatever size the scene asked for, once; after that the window's size
        // is the user's and a reload does not get to reset it.
        const qreal wide = item->width() > 0 ? item->width() : item->implicitWidth();
        const qreal high = item->height() > 0 ? item->height() : item->implicitHeight();
        m_hostWindow->resize(
            wide > 0 ? int(wide) : DefaultWindowWidth,
            high > 0 ? int(high) : DefaultWindowHeight);
        QObject::connect(m_hostWindow.get(), &QQuickWindow::widthChanged, [this] {
            fitHostedScene();
        });
        QObject::connect(m_hostWindow.get(), &QQuickWindow::heightChanged, [this] {
            fitHostedScene();
        });
        m_hostWindow->show();
    }
    // The visual parent only. The controller still owns the scene and deletes it
    // on the next reload, which is what has to keep working.
    item->setParentItem(m_hostWindow->contentItem());
    fitHostedScene();
}

void Application::fitHostedScene()
{
    if (!m_hostWindow || !m_reloadController)
        return;
    if (auto *item = qobject_cast<QQuickItem *>(m_reloadController->rootObject())) {
        item->setWidth(m_hostWindow->width());
        item->setHeight(m_hostWindow->height());
    }
}

int Application::run(const QString &moduleUri, const QString &entryType)
{
    // Before the initializer and before the scene: tokens resolved during the
    // first evaluation of a binding are the ones the window paints with, so a
    // config loaded later shows up as a visible repaint on the first frame.
    loadCompiledDesign();

    if (m_initializer)
        m_initializer(m_engine);

    m_reloadController = std::make_unique<ReloadController>(m_engine);
    QObject::connect(m_reloadController.get(), &ReloadController::sceneReloaded, [this] {
        hostScene();
        installInspector();
    });
    if (!m_reloadController->load(moduleUri, entryType)) {
        // Previously a bare `return 1`: the application exited non-zero with no
        // output whatsoever, which is the least actionable failure there is.
        // The QML errors are already collected in lastError().
        qWarning(
            "loom: could not load %s from module %s:\n%s", qUtf8Printable(entryType),
            qUtf8Printable(moduleUri), qUtf8Printable(m_reloadController->lastError()));
        return 1;
    }

    hostScene();
    if (m_developmentRuntimeEnabled)
        connectDevelopmentRuntime();
    installInspector();

    return m_application.exec();
}

QGuiApplication &Application::guiApplication()
{
    return m_application;
}

QQmlApplicationEngine &Application::engine()
{
    return m_engine;
}

} // namespace loom
