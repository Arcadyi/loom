import QtQuick
import QtQuick.Window

Window {
    objectName: "embeddedWindow"
    visible: true
    width: 200
    height: 100

    property int count: 0

    function respinSaveState() {
        return { "count": count }
    }

    function respinRestoreState(state) {
        count = state.count ?? 0
    }
}
