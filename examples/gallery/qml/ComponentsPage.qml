pragma ComponentBehavior: Bound
import QtQuick
// Qualified, so it cannot take part in resolving a bare type name. Unqualified
// it made `Row` ambiguous enough that the composite enum lookup
// `Row.SpaceBetween` returned undefined -- while still instantiating Loom's Row,
// so the only symptom was a justify that silently did nothing.
import QtQuick.Controls as QQC
import Loom
import Loom.Controls

Column {
    id: page

    Lo.style: "gap-6"

    SectionTitle {
        text: qsTr("Components")
    }

    Label {
        Lo.style: "text-muted text-sm"
        text: qsTr("Loom.Controls ships the shapes a class string cannot express, "
            + "because a class cannot change an item's type. Everything below is "
            + "styled with the same Lo.style vocabulary as everything else.")
    }

    // ------------------------------------------------------------------
    Row {
        Lo.style: "gap-4"
        align: Qt.AlignTop

        Card {
            Lo.style: "w-72"

            Col {
                Lo.style: "gap-3"
                width: parent.width

                Row {
                    Lo.style: "gap-2"
                    align: Qt.AlignVCenter

                    Icon { name: "bell" }
                    Label { Lo.style: "font-semibold"; text: qsTr("Card") }
                    Badge { text: qsTr("new") }
                }

                Divider { width: parent.width }

                Label {
                    Lo.style: "text-muted text-sm"
                    text: qsTr("A padded surface. Its defaults are token bindings, "
                        + "so bg-* and rounded-* at the call site replace them.")
                    width: parent.width
                }

                Progress {
                    Lo.style: "w-full"
                    value: 0.41
                    trackStyle: "bg-success"
                }
            }
        }

        Card {
            Lo.style: "w-72"

            Col {
                Lo.style: "gap-3"
                width: parent.width

                Label { Lo.style: "font-semibold"; text: qsTr("Icon") }

                Label {
                    Lo.style: "text-muted text-sm"
                    text: qsTr("Colour comes from text-*, with every variant that "
                        + "implies. Hover the last one.")
                    width: parent.width
                }

                Row {
                    Lo.style: "gap-4"
                    align: Qt.AlignVCenter

                    Icon { name: "check"; Lo.style: "size-6 text-success" }
                    Icon { name: "bell"; Lo.style: "size-6 text-accent" }
                    Icon {
                        name: "trash"
                        Lo.style: "size-6 text-muted hover:text-danger transition-colors"
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------
    SectionTitle {
        text: qsTr("Form controls")
    }

    Label {
        Lo.style: "text-muted text-sm"
        text: qsTr("Each derives from its QtQuick.Controls type and replaces only "
            + "the delegates the style engine cannot reach. The indicator reads the "
            + "control's state through Lo.group, which is why none of this is wired "
            + "at the call site.")
    }

    Row {
        Lo.style: "gap-6"
        align: Qt.AlignTop

        Card {
            Lo.style: "w-72"

            Col {
                Lo.style: "gap-3"
                width: parent.width

                CheckBox {
                    text: qsTr("Remember me")
                    checked: true
                }
                CheckBox {
                    text: qsTr("Rounded indicator")
                    indicatorStyle: "rounded-full"
                }
                Switch {
                    text: qsTr("Sync automatically")
                    checked: true
                    indicatorStyle: "group-checked/switch:bg-success"
                }
                Switch {
                    text: qsTr("Disabled")
                    enabled: false
                }
            }
        }

        Card {
            Lo.style: "w-72"

            Col {
                Lo.style: "gap-3"
                width: parent.width

                RadioButton {
                    text: qsTr("Daily")
                    checked: true
                }
                RadioButton {
                    text: qsTr("Weekly")
                }

                Divider { width: parent.width }

                Slider {
                    Lo.style: "w-full"
                    from: 0
                    to: 100
                    value: 60
                    trackStyle: "bg-success"
                }

                Select {
                    Lo.style: "w-full"
                    model: [qsTr("Daily"), qsTr("Weekly"), qsTr("Never")]
                    popupStyle: "rounded-lg"
                    itemStyle: "rounded-md"
                }
            }
        }

        Card {
            Lo.style: "w-72"

            Col {
                Lo.style: "gap-3"
                width: parent.width

                Field {
                    label: qsTr("Email")
                    placeholder: qsTr("you@example.com")
                    width: parent.width
                    invalid: text.length > 0 && !text.includes("@")
                }

                Field {
                    label: qsTr("With part styles")
                    placeholder: qsTr("contentStyle: text-lg")
                    width: parent.width
                    contentStyle: "text-lg"
                    labelStyle: "text-accent"
                }
            }
        }
    }

    // ------------------------------------------------------------------
    SectionTitle {
        text: qsTr("Overlays")
    }

    Label {
        Lo.style: "text-muted text-sm"
        text: qsTr("A Popup is not an Item, so Lo.style has nothing to attach to on "
            + "these -- part styles are the only way in. Hover the last button for a "
            + "tooltip.")
    }

    Row {
        Lo.style: "gap-3"
        align: Qt.AlignVCenter

        Button {
            text: qsTr("Open dialog")
            Lo.style: "px-4 py-2 rounded-lg bg-accent hover:bg-accent-hover"
                    + " pressed:bg-accent-hover text-on-accent transition-colors"
            onClicked: sampleDialog.open()
        }

        Button {
            text: qsTr("Open menu")
            Lo.style: "px-4 py-2 rounded-lg bg-surface border border-outline"
                    + " hover:bg-surface-alt text-foreground transition-colors"
            onClicked: sampleMenu.popup()
        }

        Button {
            id: tooltipTarget

            text: qsTr("Hover me")
            Lo.style: "px-4 py-2 rounded-lg bg-surface border border-outline"
                    + " hover:border-accent text-foreground transition-colors"

            Tooltip {
                text: qsTr("Styled through popupStyle and contentStyle")
                visible: tooltipTarget.hovered
                popupStyle: "rounded-lg"
            }
        }
    }

    Dialog {
        id: sampleDialog

        title: qsTr("Delete project?")
        modal: true
        popupStyle: "rounded-xl"

        Label {
            text: qsTr("This cannot be undone.")
        }
    }

    Menu {
        id: sampleMenu

        popupStyle: "rounded-lg"
        itemStyle: "rounded-md"

        QQC.Action { text: qsTr("Rename") }
        QQC.Action { text: qsTr("Duplicate") }
        QQC.Action { text: qsTr("Delete") }
    }

    // ------------------------------------------------------------------
    SectionTitle {
        text: qsTr("Tabs")
    }

    Tabs {
        id: sampleTabs

        Lo.style: "w-96"

        Tab { text: qsTr("Overview") }
        Tab { text: qsTr("Activity") }
        Tab { text: qsTr("Settings"); indicatorStyle: "bg-success" }
    }

    Label {
        Lo.style: "text-sm text-muted"
        text: qsTr("Selected tab: %1").arg(sampleTabs.currentIndex + 1)
    }

    // ------------------------------------------------------------------
    SectionTitle {
        text: qsTr("Spacer")
    }

    Label {
        Lo.style: "text-muted text-sm"
        text: qsTr("Row and Col take a justify property -- Qt Quick has main-axis "
            + "distribution nowhere. Spacer is the explicit alternative inside a "
            + "positioner, where there is no free space to distribute.")
    }

    Card {
        Lo.style: "w-96"

        Row {
            Lo.style: "gap-0"
            width: parent.width
            justify: Justify.SpaceBetween

            Label { text: qsTr("Left") }
            Label { Lo.style: "text-muted"; text: qsTr("Middle") }
            Label { text: qsTr("Right") }
        }
    }
}
