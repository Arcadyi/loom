pragma ComponentBehavior: Bound
import QtQuick
import Loom
import Loom.Controls

Column {
    Lo.style: "gap-3"

    SectionTitle {
        text: qsTr("Utility strings")
    }

    Label {
        Lo.style: "text-muted text-sm"
        text: qsTr("Each card is styled by the Lo.style string shown inside it.")
    }

    Column {
        Lo.style: "gap-4"

        Repeater {
            model: [
                "bg-blue-500 rounded-lg w-96 h-16",
                "bg-surface border border-outline rounded-xl w-96 h-16 shadow-md",
                "bg-emerald-500 rounded-full w-96 h-16 opacity-75",
                "bg-surface-alt rounded-t-2xl w-96 h-16",
                "bg-amber-400 border-4 border-amber-700 w-96 h-16"
            ]

            Rectangle {
                id: card

                required property string modelData

                Lo.style: modelData

                Text {
                    Lo.style: "text-foreground text-xs ml-4"
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: card.modelData
                }
            }
        }
    }

    SectionTitle {
        text: qsTr("Text utilities")
    }

    Column {
        Lo.style: "gap-2"

        Text {
            Lo.style: "text-accent text-lg font-bold"
            text: qsTr("text-accent text-lg font-bold")
        }

        Text {
            Lo.style: "text-danger text-base italic underline"
            text: qsTr("text-danger text-base italic underline")
        }

        Text {
            Lo.style: "text-muted text-sm tracking-wider"
            text: qsTr("text-muted text-sm tracking-wider")
        }

        Text {
            Lo.style: "text-foreground text-sm line-through"
            text: qsTr("text-foreground text-sm line-through")
        }
    }

    SectionTitle {
        text: qsTr("Spacing")
    }

    Row {
        Lo.style: "gap-2"

        Repeater {
            model: ["1", "2", "4", "6", "8", "12", "16"]

            Column {
                id: spaceSample

                required property string modelData

                Lo.style: "gap-1"

                Rectangle {
                    Lo.style: "bg-violet-500 rounded-sm h-10 w-" + spaceSample.modelData
                }

                Text {
                    Lo.style: "text-muted text-xs"
                    text: "w-" + spaceSample.modelData
                }
            }
        }
    }
}
