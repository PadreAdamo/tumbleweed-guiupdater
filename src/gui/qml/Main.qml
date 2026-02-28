import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    width: 720
    height: 420
    visible: true
    title: "Tumbleweed Updater"

    // Existing click-to-run flag
    property bool runStatusRequested: false

    // New UI state
    property bool busy: false
    property string statusKind: "ok"     // ok | warn | lock | error
    property string statusText: "Status: idle"

    pageStack.initialPage: Kirigami.Page {
        title: "Tumbleweed Updater"

        Column {
            anchors.centerIn: parent
            spacing: Kirigami.Units.largeSpacing
            width: parent.width * 0.85

            Kirigami.Heading {
                text: "Tumbleweed Updater"
                level: 1
                horizontalAlignment: Text.AlignHCenter
            }

            Controls.Label {
                id: statusLabel
                objectName: "statusLabel"
                text: root.statusText
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                width: parent.width

                // Simple “semantic” coloring
                color: root.statusKind === "ok"   ? Kirigami.Theme.positiveTextColor
                     : root.statusKind === "warn" ? Kirigami.Theme.neutralTextColor
                     : root.statusKind === "lock" ? Kirigami.Theme.neutralTextColor
                     : Kirigami.Theme.negativeTextColor
            }

            Controls.ProgressBar {
                id: busyBar
                indeterminate: true
                visible: root.busy
                width: parent.width
            }

            Controls.Button {
                id: updateButton
                objectName: "updateButton"
                text: root.busy ? "Checking…" : "Check Status"
                enabled: !root.busy

                onClicked: {
                    root.busy = true
                    root.statusKind = "ok"
                    root.statusText = "Checking for updates…"
                    root.runStatusRequested = true
                }
            }
        }
    }
}
