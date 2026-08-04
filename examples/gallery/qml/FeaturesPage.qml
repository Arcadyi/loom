pragma ComponentBehavior: Bound
import QtQuick
import Loom

Column {
    Lo.style: "gap-4"

    SectionTitle { text: qsTr("Recipes, arbitrary values, and typography") }

    Row {
        Lo.style: "gap-4"

        Rectangle {
            Lo.style: "@card hover:@card w-[300px] h-28 transition-all"

            Column {
                Lo.style: "gap-1 ml-4 mt-4"

                Text {
                    Lo.style: "text-foreground text-display font-brand tracking-brand"
                    text: qsTr("@card")
                }
                Text {
                    Lo.style: "text-muted text-[13px] leading-[18px]"
                    text: qsTr("A recipe with arbitrary type and size values")
                }
            }
        }

        Rectangle {
            Lo.style: "bg-linear-to-tr from-brand-400 via-violet-500 to-blue-600"
                + " ring-4 ring-accent rounded-[18px] w-72 h-28"

            Text {
                Lo.style: "text-white text-lg font-semibold center"
                text: qsTr("gradient + ring")
            }
        }
    }

    SectionTitle { text: qsTr("Transforms, groups, and effect ownership") }

    Row {
        Lo.style: "gap-4"

        Rectangle {
            Lo.group: "motion-card"
            Lo.style: "@card w-64 h-24 hover:ring-4 hover:ring-accent"

            Text {
                Lo.style: "text-foreground text-sm font-medium center"
                    + " group-hover/motion-card:scale-110"
                    + " group-hover/motion-card:rotate-3"
                text: qsTr("hover the named group")
            }
        }

        Rectangle {
            Lo.effects: true
            Lo.style: "bg-brand-500 rounded-card w-64 h-24"
                + " hover:brightness-125 hover:saturate-150 transition-all"

            Text {
                Lo.style: "text-white text-sm font-medium center"
                text: qsTr("Lo.effects: true")
            }
        }
    }

    SectionTitle { text: qsTr("Container and structural variants") }

    Rectangle {
        Lo.container: true
        Lo.containerName: "showcase"
        Lo.style: "@card w-[620px] h-28"

        Row {
            Lo.style: "gap-2 center"

            Repeater {
                model: 4

                Rectangle {
                    required property int index

                    Lo.style: "bg-surface-alt rounded-md w-28 h-14"
                        + " odd:bg-brand-500 even:bg-blue-500"
                        + " first:ring-4 last:scale-110"
                        + " @card/showcase:rounded-full"

                    Text {
                        Lo.style: "text-white text-xs center"
                        text: qsTr("item %1").arg(parent.index + 1)
                    }
                }
            }
        }
    }
}
