pragma ComponentBehavior: Bound
import QtQuick
import Loom

Column {
    id: page

    readonly property var semanticNames: [
        "background", "surface", "surface-alt", "outline", "foreground",
        "muted", "faint", "accent", "accent-hover", "on-accent", "danger",
        "success", "warning"
    ]

    spacing: Loom.space.s3

    SectionTitle {
        text: qsTr("Semantic tokens")
    }

    Text {
        Lo.style: "text-muted text-sm"
        text: qsTr("Current theme: %1 (dark: %2). Semantic tokens re-resolve "
            + "live on every theme switch.").arg(Loom.theme).arg(Loom.dark)
    }

    Row {
        Lo.style: "gap-2"

        Repeater {
            model: Loom.themes()

            Rectangle {
                id: themeButton

                required property string modelData

                Lo.style: modelData === Loom.theme
                    ? "bg-accent rounded-md h-9 w-24"
                    : "bg-surface-alt hover:bg-outline rounded-md h-9 w-24"

                Text {
                    Lo.style: themeButton.modelData === Loom.theme
                        ? "text-on-accent text-sm font-medium"
                        : "text-foreground text-sm"
                    anchors.centerIn: parent
                    text: themeButton.modelData
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: Loom.theme = themeButton.modelData
                }
            }
        }
    }

    Column {
        spacing: Loom.space.s1

        Repeater {
            model: page.semanticNames

            Row {
                id: semanticRow

                required property string modelData

                spacing: Loom.space.s3

                Rectangle {
                    Lo.style: "border border-outline rounded-sm size-6"
                    color: Loom.color.value(semanticRow.modelData)
                }

                Text {
                    Lo.style: "text-foreground text-sm"
                    anchors.verticalCenter: parent.verticalCenter
                    text: semanticRow.modelData
                }
            }
        }
    }

    SectionTitle {
        text: qsTr("A themed card")
    }

    Rectangle {
        Lo.style: "bg-surface border border-outline rounded-xl w-96 h-32 shadow-md"

        Column {
            Lo.style: "gap-1 mt-4 ml-4"
            anchors.top: parent.top
            anchors.left: parent.left

            Text {
                Lo.style: "text-foreground text-lg font-semibold"
                text: qsTr("Semantic styling")
            }

            Text {
                Lo.style: "text-muted text-sm"
                text: qsTr("bg-surface, border-outline, text-foreground…")
            }

            Text {
                Lo.style: "text-accent text-sm font-medium mt-2"
                text: qsTr("Reads correctly in every theme")
            }
        }
    }
}
