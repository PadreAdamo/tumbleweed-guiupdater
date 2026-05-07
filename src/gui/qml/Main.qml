import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import QtCore

Kirigami.ApplicationWindow {
    id: root
    width: 720
    height: 500
    visible: true
    title: "Tumbleweed Updater"

    // Closing the window hides it (minimizes to tray) instead of quitting.
    // The real Quit action lives in the tray context menu.
    onClosing: function(close) {
        close.accepted = false
        root.hide()
    }

    property bool runStatusRequested: false
    property bool runApplyRequested: false
    property bool runRebootRequested: false

    property bool busy: false
    property bool updatesAvailable: false
    property bool rebootRequired: false
    property string statusKind: "ok"
    property string statusText: "Idle"
    property string packageList: ""
    property string applyLog: ""

    // One-shot trigger: C++ sets this true; we open the dialog and reset it.
    property bool showRebootDialog: false
    onShowRebootDialogChanged: {
        if (showRebootDialog) {
            showRebootDialog = false
            rebootDialog.open()
        }
    }

    Settings {
        id: appSettings
        location: StandardPaths.writableLocation(StandardPaths.AppConfigLocation) + "/settings.ini"
        category: "General"
        property bool autoCheckOnLaunch: true
    }

    Component.onCompleted: {
        if (appSettings.autoCheckOnLaunch && !root.busy) {
            root.busy = true
            root.statusText = "Checking for updates…"
            root.statusKind = "ok"
            root.runStatusRequested = true
        }
    }

    pageStack.initialPage: Kirigami.Page {
        title: "Tumbleweed Updater"

        // Controls panel — centered vertically when no log, top-aligned when log is visible
        Column {
            id: controlsCol
            width: parent.width * 0.85
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: root.applyLog.length > 0
                ? Kirigami.Units.largeSpacing
                : Math.max(Kirigami.Units.largeSpacing,
                           (parent.height - implicitHeight) / 2)
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Heading {
                text: "Tumbleweed Updater"
                level: 1
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
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

            Controls.BusyIndicator {
                running: root.busy
                visible: root.busy
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Controls.CheckBox {
                text: "Check for updates on launch"
                checked: appSettings.autoCheckOnLaunch
                onToggled: appSettings.autoCheckOnLaunch = checked
            }

            Controls.Button {
                text: root.busy && !root.runApplyRequested ? "Checking…" : "Check Status"
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

        // Live apply log — visible during and after apply, fills remaining space
        Controls.ScrollView {
            id: logView
            visible: root.applyLog.length > 0
            clip: true
            anchors {
                top: controlsCol.bottom
                topMargin: Kirigami.Units.largeSpacing
                left: parent.left
                leftMargin: Kirigami.Units.largeSpacing
                right: parent.right
                rightMargin: Kirigami.Units.largeSpacing
                bottom: parent.bottom
                bottomMargin: Kirigami.Units.largeSpacing
            }

            Controls.TextArea {
                id: logArea
                readOnly: true
                text: root.applyLog
                wrapMode: TextEdit.WrapAnywhere
                font.family: "monospace"
                font.pixelSize: 11
                onTextChanged: cursorPosition = length
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

    Controls.Dialog {
        id: rebootDialog
        title: "Reboot Recommended"
        modal: true
        width: Math.min(root.width * 0.8, 480)

        contentItem: Controls.Label {
            padding: Kirigami.Units.largeSpacing
            wrapMode: Text.Wrap
            text: "A reboot is required to complete the update.\n\n" +
                  "Kernel or core system libraries were updated and will not " +
                  "take effect until the system is restarted."
        }

        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: "Reboot Now"
                onClicked: {
                    root.runRebootRequested = true
                    rebootDialog.close()
                }
            }
            Controls.Button {
                text: "Later"
                onClicked: rebootDialog.close()
            }
        }
    }
}
