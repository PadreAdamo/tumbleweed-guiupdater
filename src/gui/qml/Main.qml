import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    width: 720
    height: 420
    visible: true
    title: "Tumbleweed Updater"
    property bool runStatusRequested: false

    pageStack.initialPage: Kirigami.Page {
        title: "Tumbleweed Updater"

        Column {
            anchors.centerIn: parent
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Heading {
                text: "Tumbleweed Updater"
                level: 1
            }

            Controls.Label {
                id: statusLabel
                objectName: "statusLabel"
                text: "Status: idle"
            }

            Controls.Button {
                text: "Update Now"
                onClicked: {
			statusLabel.text = "Running status..."
			runStatusRequested = true
			   }
            }
        }
    }
}
