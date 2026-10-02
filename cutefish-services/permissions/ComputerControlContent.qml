import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FishUI 1.0 as FishUI

// The computer-control kind. The request carries `agent_name`, the agent's own
// name for itself; the compositor asks, so there is no app id to show.
ColumnLayout {
    readonly property string agentName: prompt.request.agent_name || qsTr("An app")
    readonly property string title: qsTr("Let “%1” control this computer?").arg(agentName)
    readonly property string description: qsTr("It will see your screen and use the pointer and keyboard as you would, until you stop it. It cannot answer system dialogs like this one.")
    readonly property string accept: qsTr("Allow")
    readonly property bool ready: true

    signal accepted()

    function details() {
        return {}
    }

    spacing: 8

    Repeater {
        model: [
            { icon: "input-mouse", text: qsTr("Moving the pointer or typing pauses it.") },
            { icon: "process-stop", text: qsTr("Stop it at any time with the Stop button in the status bar.") }
        ]

        RowLayout {
            spacing: 10
            Layout.fillWidth: true
            Layout.leftMargin: 62

            FishUI.IconItem {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                source: modelData.icon
            }

            FishUI.Label {
                Layout.fillWidth: true
                text: modelData.text
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: FishUI.Theme.textColor
            }
        }
    }
}
