import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FishUI 1.0 as FishUI

// Display picker for the screen-recording kind. The request carries
// `monitors`; the answer names the chosen one.
ListView {
    id: displays

    readonly property string title: qsTr("“%1” wants to record the contents of your screen").arg(prompt.application.name)
    readonly property string description: qsTr("Choose the display to share. Everything shown on it, including other windows, will be visible to the application until it stops recording.")
    readonly property bool ready: currentIndex >= 0 && currentIndex < count

    signal accepted()

    function details() {
        return { "monitors": [ prompt.request.monitors[currentIndex].name ] }
    }

    implicitHeight: Math.min(count, 3) * 52 + Math.max(0, Math.min(count, 3) - 1) * spacing
    model: prompt.request.monitors
    clip: true
    spacing: 4
    currentIndex: 0
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: true
    ScrollBar.vertical: ScrollBar {}

    delegate: Item {
        id: row
        width: ListView.view.width
        height: 52

        readonly property bool selected: displays.currentIndex === index
        // modelData is gone for a moment while the window tears down.
        readonly property var monitor: modelData ? modelData : ({})
        readonly property string label: row.monitor.description ? row.monitor.description
                                                                : (row.monitor.name || "")

        MouseArea {
            id: rowArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: displays.currentIndex = index
            onDoubleClicked: displays.accepted()
        }

        Rectangle {
            anchors.fill: parent
            radius: FishUI.Theme.mediumRadius
            color: row.selected ? FishUI.Theme.highlightColor
                                : rowArea.containsMouse ? Qt.rgba(FishUI.Theme.textColor.r,
                                                                  FishUI.Theme.textColor.g,
                                                                  FishUI.Theme.textColor.b, 0.1)
                                                        : "transparent"
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 12

            FishUI.IconItem {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                source: "video-display"
            }

            ColumnLayout {
                spacing: 1
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    text: row.label
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    color: row.selected ? FishUI.Theme.highlightedTextColor : FishUI.Theme.textColor
                }

                Label {
                    Layout.fillWidth: true
                    text: row.monitor.name ? row.monitor.name + "  ·  " + row.monitor.width + " × " + row.monitor.height : ""
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    font.pixelSize: 11
                    opacity: 0.8
                    color: row.selected ? FishUI.Theme.highlightedTextColor : FishUI.Theme.disabledTextColor
                }
            }
        }
    }
}
