import QtQuick

Item {
    objectName: "embedded"
    property int count: 1

    function respinSaveState() {
        return { "count": count }
    }

    function respinRestoreState(state) {
        count = state.count
    }
}
