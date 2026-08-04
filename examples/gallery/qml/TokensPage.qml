pragma ComponentBehavior: Bound
import QtQuick
import Loom

Column {
    id: page

    readonly property var hues: [
        "slate", "gray", "zinc", "neutral", "stone", "red", "orange", "amber",
        "yellow", "lime", "green", "emerald", "teal", "cyan", "sky", "blue",
        "indigo", "violet", "purple", "fuchsia", "pink", "rose"
    ]
    readonly property var shades: [
        "50", "100", "200", "300", "400", "500", "600", "700", "800", "900", "950"
    ]

    Lo.style: "gap-3"

    SectionTitle {
        text: qsTr("Color palette")
    }

    Column {
        Lo.style: "gap-0.5"

        Repeater {
            model: page.hues

            Row {
                id: hueRow

                required property string modelData

                Lo.style: "gap-0.5"

                Text {
                    Lo.style: "text-muted text-xs"
                    width: Loom.space.s16
                    anchors.verticalCenter: parent.verticalCenter
                    text: hueRow.modelData
                }

                Repeater {
                    model: page.shades

                    Rectangle {
                        required property string modelData

                        Lo.style: "rounded-sm size-6"
                        color: Loom.color.value(hueRow.modelData + "-" + modelData)
                    }
                }
            }
        }
    }

    SectionTitle {
        text: qsTr("Type scale")
    }

    Column {
        Lo.style: "gap-1"

        Repeater {
            model: ["xs", "sm", "base", "lg", "xl", "2xl", "3xl", "4xl"]

            Text {
                required property string modelData

                color: Loom.color.foreground
                font.pixelSize: Loom.text.value(modelData).size
                text: qsTr("text-%1 — Weaving quick brown foxes").arg(modelData)
            }
        }
    }

    SectionTitle {
        text: qsTr("Radius and shadows")
    }

    Row {
        Lo.style: "gap-6"

        Repeater {
            model: ["none", "sm", "md", "lg", "xl", "2xl", "full"]

            Column {
                id: radiusSample

                required property string modelData

                Lo.style: "gap-1"

                Rectangle {
                    Lo.style: "bg-accent size-14 rounded-" + radiusSample.modelData
                }

                Text {
                    Lo.style: "text-muted text-xs"
                    text: radiusSample.modelData
                }
            }
        }
    }

    Row {
        Lo.style: "gap-8"

        Repeater {
            model: ["sm", "base", "md", "lg", "xl", "2xl"]

            Column {
                id: shadowSample

                required property string modelData

                Lo.style: "gap-1"

                Rectangle {
                    Lo.style: "bg-surface rounded-md size-14 shadow-"
                        + shadowSample.modelData
                }

                Text {
                    Lo.style: "text-muted text-xs"
                    text: "shadow-" + shadowSample.modelData
                }
            }
        }
    }
}
