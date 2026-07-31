pragma ComponentBehavior: Bound
import QtQuick
import Loom

Column {
    id: page

    spacing: Loom.space.s3

    SectionTitle {
        text: qsTr("Responsive breakpoints")
    }

    Text {
        Lo.style: "text-muted text-sm"
        text: qsTr("Resize the window. Thresholds: sm %1, md %2, lg %3, xl %4 px.")
            .arg(Loom.breakpoint.sm).arg(Loom.breakpoint.md)
            .arg(Loom.breakpoint.lg).arg(Loom.breakpoint.xl)
    }

    Text {
        Lo.style: "text-foreground text-lg font-semibold"
        text: qsTr("Window width: %1 px").arg(page.Window.window
            ? page.Window.window.width : 0)
    }

    Rectangle {
        Lo.style: "rounded-lg h-24 w-24 sm:w-40 md:w-64 lg:w-96 xl:h-32"
            + " bg-slate-400 sm:bg-sky-500 md:bg-emerald-500 lg:bg-amber-500"
            + " xl:bg-rose-500"

        Text {
            Lo.style: "text-white text-sm font-medium"
            anchors.centerIn: parent
            text: qsTr("mobile-first")
        }
    }

    SectionTitle {
        text: qsTr("Adaptive card row")
    }

    Text {
        Lo.style: "text-muted text-sm"
        text: qsTr("Cards grow their padding and gain shadows on wider windows.")
    }

    Row {
        Lo.style: "gap-2 md:gap-6"

        Repeater {
            model: 3

            Rectangle {
                Lo.style: "bg-surface border border-outline rounded-lg"
                    + " w-32 h-20 md:w-48 md:h-28 md:shadow-md"
            }
        }
    }
}
