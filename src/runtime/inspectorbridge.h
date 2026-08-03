#pragma once

#include <QObject>
#include <QPointer>

#include <loom/reloadcontroller.h>

namespace loom {

// The Ctrl+Shift+I overlay's QML. Exposed so tst_runtime can compile it: it is
// the only QML the runtime ships, and a syntax error in it would otherwise only
// ever appear as a runtime warning nothing reads.
const char *inspectorOverlayQml();

// What the Ctrl+Shift+I overlay is allowed to do to the project's source.
//
// A separate object rather than exposing the ReloadController to the overlay:
// the overlay is untrusted QML built from a string literal, and this is the
// whole surface it gets -- one question and one action.
class InspectorBridge final : public QObject {
    Q_OBJECT
    // Drives whether the style field is editable. False with no development
    // server, and false against a server that predates the styleEdit
    // capability, where sending the frame would be a fatal framing error and
    // would drop hot reload for the rest of the session.
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY canEditChanged)

public:
    explicit InspectorBridge(ReloadController *controller, QObject *parent = nullptr)
        : QObject(parent)
        , m_controller(controller)
    {
        if (m_controller) {
            // Capabilities arrive with the first bundle, which is usually after
            // the overlay exists, so the field has to be able to become
            // editable rather than being decided once at construction.
            connect(
                m_controller, &ReloadController::sceneReloaded, this,
                &InspectorBridge::canEditChanged);
        }
    }

    bool canEdit() const
    {
        return m_controller && m_controller->canEditSource();
    }

    //! Returns false when the edit could not even be addressed and sent.
    Q_INVOKABLE bool applyStyleEdit(
        QObject *item, const QString &oldStyle, const QString &newStyle)
    {
        return m_controller && m_controller->editStyle(item, oldStyle, newStyle);
    }

signals:
    void canEditChanged();

private:
    QPointer<ReloadController> m_controller;
};

} // namespace loom
