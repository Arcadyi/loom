pragma ComponentBehavior: Bound
import QtQuick
import Loom

Column {
    spacing: Loom.space.s3

    SectionTitle {
        text: qsTr("Interaction states")
    }

    Text {
        Lo.style: "text-muted text-sm"
        text: qsTr("Hover, press, focus and disable these cards; every change "
            + "is a variant in the Lo.style string.")
    }

    Row {
        spacing: Loom.space.s4

        Rectangle {
            Lo.style: "bg-surface border border-outline rounded-lg w-56 h-20 transition-colors"
                + " hover:bg-surface-alt hover:border-accent"

            Text {
                Lo.style: "text-foreground text-sm"
                anchors.centerIn: parent
                text: qsTr("hover:")
            }
        }

        Rectangle {
            Lo.style: "bg-accent rounded-lg w-56 h-20 pressed:bg-accent-hover"
                + " pressed:opacity-90"

            Text {
                Lo.style: "text-on-accent text-sm font-medium"
                anchors.centerIn: parent
                text: qsTr("pressed: (passive TapHandler)")
            }
        }

        Rectangle {
            id: clickCard

            property int clicks: 0

            Lo.style: "bg-surface border border-outline rounded-lg w-56 h-20 transition-colors"
                + " hover:shadow-md pressed:bg-blue-100"

            Text {
                Lo.style: "text-foreground text-sm"
                anchors.centerIn: parent
                text: qsTr("clicked %n time(s)", "", clickCard.clicks)
            }

            MouseArea {
                anchors.fill: parent
                onClicked: clickCard.clicks++
            }
        }
    }

    Row {
        spacing: Loom.space.s4

        Rectangle {
            Lo.style: "bg-surface border border-outline rounded-lg w-56 h-20 transition-colors"
                + " focus:border-accent focus:bg-blue-50 dark:focus:bg-slate-800"
            activeFocusOnTab: true

            Text {
                Lo.style: "text-foreground text-sm"
                anchors.centerIn: parent
                text: parent.activeFocus ? qsTr("focused!") : qsTr("focus: (click me)")
            }

            MouseArea {
                anchors.fill: parent
                onClicked: parent.forceActiveFocus()
            }
        }

        Rectangle {
            id: disableCard

            Lo.style: "bg-surface border border-outline rounded-lg w-56 h-20 transition-colors"
                + " disabled:opacity-40"
            enabled: false

            Text {
                Lo.style: "text-foreground text-sm"
                anchors.centerIn: parent
                text: qsTr("disabled:opacity-40")
            }
        }

        Rectangle {
            Lo.style: "bg-surface border border-outline rounded-lg w-56 h-20 transition-colors"
                + " hover:bg-surface-alt"

            Text {
                Lo.style: "text-foreground text-sm"
                anchors.centerIn: parent
                text: disableCard.enabled ? qsTr("disable neighbor")
                                          : qsTr("enable neighbor")
            }

            MouseArea {
                anchors.fill: parent
                onClicked: disableCard.enabled = !disableCard.enabled
            }
        }
    }

    SectionTitle {
        text: qsTr("MouseArea-backed pressed state")
    }

    Text {
        Lo.style: "text-muted text-sm"
        text: qsTr("The MouseArea's own pressed property drives the variant; "
            + "no handler is injected.")
    }

    MouseArea {
        Lo.style: "w-56 h-20 opacity-100 pressed:opacity-60"

        Rectangle {
            Lo.style: "bg-rose-500 rounded-lg w-full h-full"

            Text {
                Lo.style: "text-white text-sm font-medium"
                anchors.centerIn: parent
                text: qsTr("press me")
            }
        }
    }
}
