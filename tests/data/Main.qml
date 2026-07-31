import QtQuick

Item {
    objectName: "embedded"
    property int count: 1

    function loomSaveState() {
        return { "count": count }
    }

    function loomRestoreState(state) {
        count = state.count
    }
}
