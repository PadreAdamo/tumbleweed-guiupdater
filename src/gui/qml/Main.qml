import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    width: 720
    height: 460
    visible: true
    title: "Tumbleweed Updater"

    property bool runStatusRequested: false
    property bool runApplyRequested: false

    property bool busy: false
    property bool updatesAvailable: false
    property string statusKind: "ok"
    property string statusText: "Idle"
    property string packageList: ""

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
                text: root.statusText
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                width: parent.width

                color: root.statusKind === "ok"   ? Kirigami.Theme.positiveTextColor
                     : root.statusKind === "warn" ? Kirigami.Theme.neutralTextColor
                     : root.statusKind === "lock" ? Kirigami.Theme.neutralTextColor
                     : Kirigami.Theme.negativeTextColor
            }

            Controls.ProgressBar {
                indeterminate: true
                visible: root.busy
                width: parent.width
            }

            Controls.Button {
                text: root.busy ? "Checking…" : "Check Status"
                enabled: !root.busy
                onClicked: {
                    root.busy = true
                    root.statusText = "Checking for updates…"
                    root.statusKind = "ok"
                    root.runStatusRequested = true
                }
            }

            Controls.Button {
                text: root.busy ? "Applying…" : "Apply Updates (Admin)"
                enabled: !root.busy && root.updatesAvailable
                onClicked: {
                    root.busy = true
                    root.statusText = "Applying updates (admin)…"
                    root.statusKind = "warn"
                    root.runApplyRequested = true
                }
            }

	    Controls.Button {
                text: root.packageList.length > 0
                   ? "View " + root.packageList.split("\n").length + " Packages"
                   : "View Packages"
                enabled: root.packageList.length > 0
                onClicked: packageDialog.open()
            }
        }
    }

    Controls.Dialog {
        id: packageDialog
        title: "Available Package Updates"
        modal: true
        width: Math.min(root.width * 0.9, 650)
        height: Math.min(root.height * 0.8, 420)
        standardButtons: Controls.Dialog.Close

        contentItem: Controls.ScrollView {
            clip: true

            Controls.TextArea {
                id: packageText
                text: root.packageList
                readOnly: true
                wrapMode: TextEdit.NoWrap
            }
        }
    }
}
