// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Adam Girardo <adamjohngirardo@gmail.com>

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import QtCore

Kirigami.ApplicationWindow {
    id: root
    width: 720
    height: 500
    visible: true
    title: "Tumbleweed Updater"
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.ToolBar

    onClosing: function(close) {
        close.accepted = false
        root.hide()
    }

    // ---- Controller trigger properties ----
    property bool runStatusRequested:  false
    property bool runApplyRequested:   false
    property bool runRebootRequested:  false
    property bool enableTimerRequested: false

    // ---- Systemd timer state ----
    property bool   timerEnabled:             false
    property bool   setTimerEnabledRequested: false
    property string nextCheckTime:            ""
    property bool   timerOfferReady:          false
    Timer {
        interval: 1200
        running: root.timerOfferReady
        repeat: false
        onTriggered: {
            root.timerOfferReady = false
            timerOfferDialog.open()
        }
    }

    // ---- Status / UI state ----
    property bool   busy:           false
    property bool   updatesAvailable: false
    property bool   rebootRequired: false
    property string statusKind:     "ok"
    property string statusText:     "Idle"
    property string packageList:    ""
    property bool   flatpakUpdatesAvailable: false
    property int    flatpakUpdateCount: 0
    property string flatpakList:    ""
    property string applyLog:       ""

    // ---- History ----
    property string historyLog:              ""
    property bool   loadHistoryRequested:    false
    property bool   clearHistoryRequested:   false

    // ---- Settings (read from KConfig by C++ at startup; written back via saveSettingsRequested) ----
    property bool   settingsAutoCheckEnabled: true
    property int    settingsIntervalHours:    24
    property bool   settingsSnapperEnabled:   true
    property bool   settingsFlatpakEnabled:   false
    property string settingsVendorPolicy:     "priority"
    property bool   saveSettingsRequested:    false

    // ---- Vendor change state (populated by status/apply JSON result) ----
    property bool vendorChangeDetected: false
    property int  vendorChangeCount:    0
    property var  vendorChanges:        []

    // ---- Read-only state set by C++ ----
    property bool   snapperAvailable:        false
    property bool   snapperGuiAvailable:     false
    property bool   yastAvailable:           false
    property bool   anySnapperToolAvailable: false
    property string appVersion:              ""

    // ---- Snapper rollback state ----
    property bool   snapperUsed:              false
    property int    snapshotPre:              -1
    property int    snapshotPost:             -1
    property bool   showSnapperBanner:        false
    property bool   openSnapperToolRequested: false

    // Rollback trigger and result
    property int    rollbackSnapshotNum:      -1
    property int    rollbackSnapshotPost:     -1
    property string rollbackTimestamp:        ""
    property bool   runRollbackRequested:     false
    property bool   rollbackSucceeded:        false
    property string rollbackOutput:           ""
    property bool   showRollbackResultDialog: false
    onShowRollbackResultDialogChanged: {
        if (showRollbackResultDialog) {
            showRollbackResultDialog = false
            rollbackResultDialog.open()
        }
    }

    // Post-reboot recovery check
    property bool   postRebootCheckReady:     false
    property int    postRebootSnapshotPre:    -1
    property int    postRebootSnapshotPost:   -1
    property string postRebootTimestamp:      ""
    property bool   rebootConfirmedRequested: false

    // Fires once ~800ms after launch if C++ detected a post-reboot update cycle.
    Timer {
        interval: 800
        running: root.postRebootCheckReady
        repeat: false
        onTriggered: {
            root.postRebootCheckReady = false
            postRebootDialog.open()
        }
    }

    // ---- Cross-page navigation ----
    property int currentTab: 0   // drives tabBar via Connections

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

    function formatTimestamp(isoStr) {
        var dt = new Date(isoStr)
        if (isNaN(dt.getTime())) return isoStr
        var months = ["Jan","Feb","Mar","Apr","May","Jun",
                      "Jul","Aug","Sep","Oct","Nov","Dec"]
        var h = dt.getHours(), m = String(dt.getMinutes()).padStart(2, "0")
        var ampm = h >= 12 ? "PM" : "AM"
        h = h % 12 || 12
        return months[dt.getMonth()] + " " + dt.getDate() + ", " + dt.getFullYear() +
               " at " + h + ":" + m + " " + ampm
    }

    // ---- History entry formatter ----
    function formatHistoryEntry(entry) {
        var icon = "🔍"
        var primary = ""

        if (entry.operation === "reboot-confirmed") {
            icon = "✅"
            primary = "Confirmed — system running normally after update"
        } else if (entry.operation === "apply") {
            if (entry.ok) {
                icon = "✅"
                primary = "Applied updates"
            } else {
                icon = "❌"
                primary = "Apply failed"
            }
        } else {
            if (!entry.ok) {
                icon = "❌"
                primary = "Check failed"
            } else if (entry.updateCount > 0) {
                var n = entry.updateCount
                primary = "Checked — " + n + " update" + (n !== 1 ? "s" : "") + " available"
            } else {
                primary = "Checked — up to date"
            }
        }

        var dt = new Date(entry.timestamp)
        var months = ["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"]
        var h = dt.getHours()
        var ampm = h >= 12 ? "PM" : "AM"
        h = h % 12 || 12
        var m = String(dt.getMinutes()).padStart(2, "0")
        var ts = months[dt.getMonth()] + " " + dt.getDate() + ", " + dt.getFullYear()
                 + " at " + h + ":" + m + " " + ampm

        var sub = ts
        if (entry.snapperUsed)
            sub += "  ·  Snapshots #" + entry.snapshotPre + " / #" + entry.snapshotPost
        if (entry.rebootRequired)
            sub += "  ·  Reboot required"

        return { label: icon + "  " + primary, subtitle: sub }
    }

    function policyLabel(mode) {
        if (mode === "opensuse") return "Always use openSUSE repositories"
        if (mode === "allow")   return "Always allow vendor changes"
        if (mode === "deny")    return "Never allow vendor changes"
        return "Follow repository priority"
    }

    // ---- Settings page ----
    Component {
        id: settingsPageComponent

        Kirigami.ScrollablePage {
            title: "Settings"

            Kirigami.FormLayout {

                // ════════ Auto-Check ════════
                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: "Automatic Updates"
                }

                Controls.Switch {
                    id: backgroundCheckSwitch
                    Kirigami.FormData.label: "Background checks"
                    checked: root.timerEnabled
                    onToggled: {
                        root.timerEnabled = checked
                        root.settingsAutoCheckEnabled = checked
                        root.setTimerEnabledRequested = true
                        root.saveSettingsRequested = true
                    }
                }

                Controls.Label {
                    Kirigami.FormData.label: " "
                    text: backgroundCheckSwitch.checked
                        ? "Background checks active (systemd timer)"
                        : "Background checks disabled"
                    opacity: 0.7
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                }

                Controls.Label {
                    Kirigami.FormData.label: " "
                    visible: root.timerEnabled && root.nextCheckTime.length > 0
                    text: "Next check: " + root.nextCheckTime
                    opacity: 0.7
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                }

                Controls.ComboBox {
                    Kirigami.FormData.label: "Check interval"
                    enabled: root.settingsAutoCheckEnabled

                    readonly property var hourValues: [1, 2, 4, 8, 24]
                    model: ["1 hour", "2 hours", "4 hours", "8 hours", "24 hours"]

                    Component.onCompleted: {
                        var idx = hourValues.indexOf(root.settingsIntervalHours)
                        currentIndex = idx >= 0 ? idx : 4
                    }

                    onActivated: function(idx) {
                        root.settingsIntervalHours = hourValues[idx]
                        root.saveSettingsRequested = true
                    }
                }

                // ════════ Updates ════════
                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: "Updates"
                }

                Controls.Switch {
                    Kirigami.FormData.label: "Snapper snapshots"
                    checked: root.settingsSnapperEnabled && root.snapperAvailable
                    enabled: root.snapperAvailable
                    onToggled: {
                        root.settingsSnapperEnabled = checked
                        root.saveSettingsRequested = true
                    }
                }

                Controls.Label {
                    Kirigami.FormData.label: " "
                    visible: !root.snapperAvailable
                    text: "Snapper is not installed"
                    font.italic: true
                    opacity: 0.6
                }

                Controls.Switch {
                    Kirigami.FormData.label: "Flatpak updates"
                    checked: root.settingsFlatpakEnabled
                    onToggled: {
                        root.settingsFlatpakEnabled = checked
                        root.saveSettingsRequested = true
                    }
                }

                // ════════ Vendor Policy ════════
                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: "Vendor Policy"
                }

                Controls.ButtonGroup { id: vendorPolicyGroup }

                Controls.RadioButton {
                    Kirigami.FormData.label: "Vendor policy"
                    text: "Always use openSUSE repositories"
                    Controls.ButtonGroup.group: vendorPolicyGroup
                    checked: root.settingsVendorPolicy === "opensuse"
                    onToggled: if (checked) {
                        root.settingsVendorPolicy = "opensuse"
                        root.saveSettingsRequested = true
                    }
                }

                Controls.RadioButton {
                    text: "Follow repository priority  (default)"
                    Controls.ButtonGroup.group: vendorPolicyGroup
                    checked: root.settingsVendorPolicy === "priority"
                    onToggled: if (checked) {
                        root.settingsVendorPolicy = "priority"
                        root.saveSettingsRequested = true
                    }
                }

                Controls.RadioButton {
                    text: "Always allow vendor changes"
                    Controls.ButtonGroup.group: vendorPolicyGroup
                    checked: root.settingsVendorPolicy === "allow"
                    onToggled: if (checked) {
                        root.settingsVendorPolicy = "allow"
                        root.saveSettingsRequested = true
                    }
                }

                Controls.RadioButton {
                    text: "Never allow vendor changes"
                    Controls.ButtonGroup.group: vendorPolicyGroup
                    checked: root.settingsVendorPolicy === "deny"
                    onToggled: if (checked) {
                        root.settingsVendorPolicy = "deny"
                        root.saveSettingsRequested = true
                    }
                }

                Controls.Label {
                    Kirigami.FormData.label: " "
                    text: "Controls how zypper handles packages that would switch\nbetween vendors or repositories during an update."
                    font.italic: true
                    opacity: 0.65
                    wrapMode: Text.Wrap
                }

                // ════════ About ════════
                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: "About"
                }

                Controls.Label {
                    Kirigami.FormData.label: "Application"
                    text: "Tumbleweed GUI Updater"
                }

                Controls.Label {
                    Kirigami.FormData.label: "Version"
                    text: root.appVersion
                }

                Controls.Label {
                    Kirigami.FormData.label: " "
                    text: "A KDE-native system update orchestrator\nfor openSUSE Tumbleweed."
                    wrapMode: Text.Wrap
                }

                Controls.Button {
                    Kirigami.FormData.label: "History"
                    text: "View History Log"
                    onClicked: {
                        pageStack.pop()
                        root.currentTab = 1
                    }
                }

                Controls.Button {
                    text: "Clear History"
                    onClicked: clearConfirmDialog.open()
                }
            }
        }
    }

    // ---- Main page ----
    pageStack.initialPage: Kirigami.Page {
        title: tabBar.currentIndex === 0 ? "Tumbleweed Updater" : "History"
        padding: 0

        actions: [
            Kirigami.Action {
                icon.name: "configure"
                text: "Settings"
                onTriggered: {
                    if (pageStack.depth > 1)
                        pageStack.pop()
                    else
                        pageStack.push(settingsPageComponent)
                }
            }
        ]

        // Drive tabBar from root.currentTab (set by settings page "View History Log")
        Connections {
            target: root
            function onCurrentTabChanged() {
                if (tabBar.currentIndex !== root.currentTab)
                    tabBar.currentIndex = root.currentTab
            }
        }

        // Reload History when returning from the Settings page
        Connections {
            target: pageStack
            function onCurrentItemChanged() {
                if (pageStack.depth === 1 && tabBar.currentIndex === 1)
                    root.loadHistoryRequested = true
            }
        }

        header: Controls.TabBar {
            id: tabBar
            onCurrentIndexChanged: {
                root.currentTab = currentIndex
                mainView.currentIndex = currentIndex
                if (currentIndex === 1)
                    root.loadHistoryRequested = true
            }
            Controls.TabButton { text: "Updater" }
            Controls.TabButton { text: "History" }
        }

        Controls.SwipeView {
            id: mainView
            anchors.fill: parent
            interactive: false
            clip: true

            // ---- Tab 0: Updater ----
            Item {
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

                    Kirigami.InlineMessage {
                        width: parent.width
                        visible: root.showSnapperBanner && root.snapperAvailable &&
                                 root.snapshotPre > 0
                        type: Kirigami.MessageType.Information
                        showCloseButton: true
                        onVisibleChanged: if (!visible) root.showSnapperBanner = false
                        text: "Snapshots #" + root.snapshotPre + " (pre) and #" +
                              root.snapshotPost + " (post) were created before this update. " +
                              "If something went wrong, you can roll back."
                        actions: [
                            Kirigami.Action {
                                text: root.yastAvailable ? "Open Filesystem Snapshots"
                                                         : "Open Snapper GUI"
                                visible: root.anySnapperToolAvailable
                                onTriggered: root.openSnapperToolRequested = true
                            },
                            Kirigami.Action {
                                text: "How to Roll Back"
                                onTriggered: Qt.openUrlExternally(
                                    "https://doc.opensuse.org/documentation/leap/reference/" +
                                    "html/book-reference/cha-snapper.html")
                            }
                        ]
                    }

                    Controls.BusyIndicator {
                        running: root.busy
                        visible: root.busy
                        anchors.horizontalCenter: parent.horizontalCenter
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
                            if (root.vendorChangeDetected && root.vendorChanges.length > 0) {
                                vendorChangeDialog.open()
                            } else {
                                root.busy = true
                                root.statusText = "Applying updates (admin)…"
                                root.statusKind = "warn"
                                root.runApplyRequested = true
                            }
                        }
                    }

                    Controls.Button {
                        text: {
                            var total = 0
                            if (root.packageList.length > 0)
                                total += root.packageList.split("\n").length
                            if (root.flatpakList.length > 0)
                                total += root.flatpakList.split("\n").length
                            return total > 0 ? "View " + total + " Packages" : "View Packages"
                        }
                        enabled: root.packageList.length > 0 || root.flatpakList.length > 0
                        onClicked: packageDialog.open()
                    }
                }

                Controls.ScrollView {
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
                        readOnly: true
                        text: root.applyLog
                        wrapMode: TextEdit.WrapAnywhere
                        font.family: "monospace"
                        font.pixelSize: 11
                        onTextChanged: cursorPosition = length
                    }
                }
            }

            // ---- Tab 1: History ----
            Item {
                id: historyTab

                property var entries: {
                    if (!root.historyLog) return []
                    var lines = root.historyLog.split('\n')
                    var result = []
                    for (var i = 0; i < lines.length; i++) {
                        var line = lines[i].trim()
                        if (!line) continue
                        try { result.push(JSON.parse(line)) } catch(e) {}
                    }
                    result.reverse()
                    return result
                }

                Row {
                    id: historyToolRow
                    visible: historyTab.entries.length > 0
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: Kirigami.Units.smallSpacing
                    height: visible ? implicitHeight : 0
                    z: 1

                    Controls.Button {
                        flat: true
                        text: "Clear History"
                        icon.name: "edit-clear-history"
                        onClicked: clearConfirmDialog.open()
                    }
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    visible: historyTab.entries.length === 0
                    text: "No update history yet"
                    explanation: "Check for updates or apply an update to begin building history"
                    icon.name: "view-history"
                }

                ListView {
                    anchors {
                        top: historyToolRow.visible ? historyToolRow.bottom : parent.top
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                    }
                    visible: historyTab.entries.length > 0
                    model: historyTab.entries
                    clip: true

                    delegate: Item {
                        id: histDelegate
                        required property var modelData
                        required property int index

                        width: ListView.view.width
                        implicitHeight: subDelegate.implicitHeight

                        readonly property var _fmt: root.formatHistoryEntry(modelData)
                        readonly property bool canRollBack:
                            root.snapperAvailable &&
                            (modelData.snapperUsed ?? false) &&
                            (modelData.snapshotPre ?? -1) > 0 &&
                            (modelData.operation ?? "") === "apply" &&
                            (modelData.ok ?? false)

                        Kirigami.SubtitleDelegate {
                            id: subDelegate
                            anchors {
                                left: parent.left
                                top: parent.top
                                bottom: parent.bottom
                                right: rollBackBtn.visible ? rollBackBtn.left
                                                          : parent.right
                                rightMargin: rollBackBtn.visible
                                             ? Kirigami.Units.smallSpacing : 0
                            }
                            text: histDelegate._fmt.label
                            subtitle: histDelegate._fmt.subtitle
                        }

                        Controls.Button {
                            id: rollBackBtn
                            visible: histDelegate.canRollBack
                            text: "Roll Back"
                            icon.name: "edit-undo"
                            flat: true
                            anchors.right: parent.right
                            anchors.rightMargin: Kirigami.Units.smallSpacing
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: {
                                root.rollbackSnapshotNum  = histDelegate.modelData.snapshotPre
                                root.rollbackSnapshotPost =
                                    histDelegate.modelData.snapshotPost ?? -1
                                root.rollbackTimestamp =
                                    histDelegate.modelData.timestamp ?? ""
                                rollbackConfirmDialog.open()
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- Dialogs ----

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
                text: {
                    var content = ""
                    if (root.packageList.length > 0) {
                        var sysCount = root.packageList.split("\n").length
                        content += "── System Packages (" + sysCount + ") ──\n" + root.packageList
                    }
                    if (root.flatpakList.length > 0) {
                        var fpCount = root.flatpakList.split("\n").length
                        if (content.length > 0) content += "\n\n"
                        content += "── Flatpak Apps (" + fpCount + ") ──\n" + root.flatpakList
                    }
                    return content
                }
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

    Controls.Dialog {
        id: vendorChangeDialog
        title: "Vendor Changes Detected"
        modal: true
        width: Math.min(root.width * 0.9, 580)
        height: Math.min(root.height * 0.85, 500)

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "The following packages would switch vendors during this update:"
            }

            Controls.ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(root.vendorChangeCount * 28 + 16, 200)
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: 2

                    Repeater {
                        model: root.vendorChanges
                        Controls.Label {
                            Layout.fillWidth: true
                            text: "• " + modelData.package + ": "
                                  + modelData.fromVendor + " → " + modelData.toVendor
                            wrapMode: Text.Wrap
                            font.family: "monospace"
                            font.pixelSize: 12
                        }
                    }
                }
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "This is normal if you have third-party repositories configured,\n"
                    + "but review this list before proceeding."
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.italic: true
                opacity: 0.75
                text: "Current policy: " + root.policyLabel(root.settingsVendorPolicy)
            }
        }

        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: "Cancel"
                onClicked: vendorChangeDialog.close()
            }
            Controls.Button {
                text: "Proceed with Update"
                onClicked: {
                    vendorChangeDialog.close()
                    root.busy = true
                    root.statusText = "Applying updates (admin)…"
                    root.statusKind = "warn"
                    root.runApplyRequested = true
                }
            }
        }
    }

    Controls.Dialog {
        id: rollbackConfirmDialog
        title: "Roll Back to Snapshot #" + root.rollbackSnapshotNum + "?"
        modal: true
        width: Math.min(root.width * 0.9, 560)

        contentItem: Controls.Label {
            padding: Kirigami.Units.largeSpacing
            wrapMode: Text.Wrap
            text: "This will restore your system to the state it was in before the update\n" +
                  "applied on " + root.formatTimestamp(root.rollbackTimestamp) + ".\n\n" +
                  "This operation cannot be undone without applying updates again.\n" +
                  "Administrator authentication will be required.\n\n" +
                  "Snapper command that will run:\n" +
                  "  snapper rollback " + root.rollbackSnapshotNum
            font.family: "monospace"
            font.pixelSize: 13
        }

        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: "Cancel"
                onClicked: rollbackConfirmDialog.close()
            }
            Controls.Button {
                text: "Roll Back Now"
                onClicked: {
                    rollbackConfirmDialog.close()
                    root.runRollbackRequested = true
                }
            }
        }
    }

    Controls.Dialog {
        id: rollbackResultDialog
        title: root.rollbackSucceeded ? "Rollback Complete" : "Rollback Failed"
        modal: true
        width: Math.min(root.width * 0.85, 520)

        contentItem: Controls.Label {
            padding: Kirigami.Units.largeSpacing
            wrapMode: Text.Wrap
            text: root.rollbackSucceeded
                  ? "Rollback complete. A reboot is required to apply the restored state.\n\n" +
                    "Your system has been reverted to snapshot #" + root.rollbackSnapshotNum + "."
                  : "Rollback failed.\n\n" + root.rollbackOutput
        }

        footer: Controls.DialogButtonBox {
            Controls.Button {
                visible: root.rollbackSucceeded
                text: "Reboot Now"
                onClicked: {
                    root.runRebootRequested = true
                    rollbackResultDialog.close()
                }
            }
            Controls.Button {
                text: root.rollbackSucceeded ? "Later" : "OK"
                onClicked: rollbackResultDialog.close()
            }
        }
    }

    Controls.Dialog {
        id: postRebootDialog
        title: "How did the last update go?"
        modal: true
        width: Math.min(root.width * 0.9, 560)

        contentItem: Controls.Label {
            padding: Kirigami.Units.largeSpacing
            wrapMode: Text.Wrap
            text: "Your system was updated on " +
                  root.formatTimestamp(root.postRebootTimestamp) +
                  " and has since been rebooted.\n\n" +
                  "Snapshots #" + root.postRebootSnapshotPre + " (pre) and #" +
                  root.postRebootSnapshotPost + " (post) are available if you need to roll back.\n\n" +
                  "If everything looks good, dismiss this — Snapper's cleanup algorithm will " +
                  "manage the snapshots automatically."
        }

        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: "Everything is Fine"
                onClicked: {
                    root.rebootConfirmedRequested = true
                    postRebootDialog.close()
                }
            }
            Controls.Button {
                text: "I Need to Roll Back"
                onClicked: {
                    postRebootDialog.close()
                    root.rollbackSnapshotNum  = root.postRebootSnapshotPre
                    root.rollbackSnapshotPost = root.postRebootSnapshotPost
                    root.rollbackTimestamp    = root.postRebootTimestamp
                    rollbackConfirmDialog.open()
                }
            }
        }
    }

    Controls.Dialog {
        id: timerOfferDialog
        title: "Enable Background Update Checks?"
        modal: true
        width: Math.min(root.width * 0.9, 560)

        contentItem: Controls.Label {
            padding: Kirigami.Units.largeSpacing
            wrapMode: Text.Wrap
            text: "Tumbleweed Updater can check for updates automatically in the " +
                  "background — even when the app is closed — using a systemd timer.\n\n" +
                  "• Checks every " + (root.settingsIntervalHours === 1 ? "hour" : root.settingsIntervalHours + " hours") + " (configurable in Settings)\n" +
                  "• Sends a notification when updates are found\n" +
                  "• Uses no resources when not checking\n" +
                  "• Can be turned off in Settings at any time"
        }

        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: "Not Now"
                onClicked: timerOfferDialog.close()
            }
            Controls.Button {
                text: "Enable"
                onClicked: {
                    root.enableTimerRequested = true
                    root.timerEnabled = true
                    timerOfferDialog.close()
                }
            }
        }
    }

    Controls.Dialog {
        id: clearConfirmDialog
        title: "Clear History"
        modal: true
        width: Math.min(root.width * 0.7, 400)

        contentItem: Controls.Label {
            padding: Kirigami.Units.largeSpacing
            wrapMode: Text.Wrap
            text: "Delete the entire update history log?\n\nThis cannot be undone."
        }

        footer: Controls.DialogButtonBox {
            Controls.Button {
                text: "Delete"
                onClicked: {
                    root.clearHistoryRequested = true
                    clearConfirmDialog.close()
                }
            }
            Controls.Button {
                text: "Cancel"
                onClicked: clearConfirmDialog.close()
            }
        }
    }
}
