pragma ComponentBehavior: Bound
import QtQuick
import Loom
import Loom.Controls

Column {
    Lo.style: "gap-3"

    SectionTitle {
        text: qsTr("Interaction states")
    }

    Label {
        Lo.style: "text-muted text-sm"
        text: qsTr("Hover, press, focus and disable these cards; every change "
            + "is a variant in the Lo.style string.")
    }

    Row {
        Lo.style: "gap-4"

        Rectangle {
            Lo.style: "@state-card"
                + " hover:bg-surface-alt hover:border-accent"

            Text {
                Lo.style: "text-foreground text-sm center"
                text: qsTr("hover:")
            }
        }

        Rectangle {
            Lo.style: "bg-accent rounded-lg w-56 h-20 pressed:bg-accent-hover"
                + " pressed:opacity-90"

            Text {
                Lo.style: "text-on-accent text-sm font-medium center"
                text: qsTr("pressed: (passive TapHandler)")
            }
        }

        Rectangle {
            id: clickCard

            property int clicks: 0

            Lo.style: "@state-card"
                + " hover:shadow-md pressed:bg-blue-100"

            Text {
                Lo.style: "text-foreground text-sm center"
                text: qsTr("clicked %n time(s)", "", clickCard.clicks)
            }

            MouseArea {
                Lo.style: "fill"
                onClicked: clickCard.clicks++
            }
        }
    }

    Row {
        Lo.style: "gap-4"

        Rectangle {
            Lo.style: "@state-card"
                + " focus:border-accent focus:bg-blue-50 dark:focus:bg-slate-800"
            activeFocusOnTab: true

            Text {
                Lo.style: "text-foreground text-sm center"
                text: parent.activeFocus ? qsTr("focused!") : qsTr("focus: (click me)")
            }

            MouseArea {
                Lo.style: "fill"
                onClicked: parent.forceActiveFocus()
            }
        }

        Rectangle {
            id: disableCard

            Lo.style: "@state-card"
                + " disabled:opacity-40"
            enabled: false

            Text {
                Lo.style: "text-foreground text-sm center"
                text: qsTr("disabled:opacity-40")
            }
        }

        Rectangle {
            Lo.style: "@state-card"
                + " hover:bg-surface-alt"

            Text {
                Lo.style: "text-foreground text-sm center"
                text: disableCard.enabled ? qsTr("disable neighbor")
                                          : qsTr("enable neighbor")
            }

            MouseArea {
                Lo.style: "fill"
                onClicked: disableCard.enabled = !disableCard.enabled
            }
        }
    }

    SectionTitle {
        text: qsTr("MouseArea-backed pressed state")
    }

    Label {
        Lo.style: "text-muted text-sm"
        text: qsTr("The MouseArea's own pressed property drives the variant; "
            + "no handler is injected.")
    }

    MouseArea {
        Lo.style: "w-56 h-20 opacity-100 pressed:opacity-60"

        Rectangle {
            Lo.style: "bg-rose-500 rounded-lg fill"

            Text {
                Lo.style: "text-white text-sm font-medium center"
                text: qsTr("press me")
            }
        }
    }
}
