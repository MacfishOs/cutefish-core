import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FishUI 1.0 as FishUI

// Picker for the screen-recording kind. The request carries `monitors` and,
// when the application may share windows, `windows`; the answer names the
// chosen one under the matching key.
ListView {
    id: choices

    readonly property string title: qsTr("Share your screen with “%1”").arg(prompt.application.name)
    readonly property string description: {
        var monitors = (prompt.request.monitors || []).length > 0
        var windows = (prompt.request.windows || []).length > 0
        if (monitors && windows)
            return qsTr("Choose a display or a window. What is shown on it will be visible to the application until it stops sharing.")
        if (windows)
            return qsTr("Choose a window. Its contents will be visible to the application until it stops sharing.")
        return qsTr("Choose a display. Everything shown on it, including other windows, will be visible to the application until it stops sharing.")
    }
    readonly property string accept: qsTr("Share")
    readonly property bool ready: currentIndex >= 0 && currentIndex < count

    signal accepted()

    function details() {
        var item = choices.model[currentIndex]
        if (item.kind === "window")
            return { "windows": [ item.identifier ] }
        return { "monitors": [ item.name ] }
    }

    function buildModel() {
        var rows = []
        var monitors = prompt.request.monitors || []
        for (var i = 0; i < monitors.length; ++i) {
            var m = monitors[i] || {}
            rows.push({ kind: "monitor", name: m.name || "", description: m.description || "",
                        width: m.width || 0, height: m.height || 0 })
        }
        var windows = prompt.request.windows || []
        for (var j = 0; j < windows.length; ++j) {
            var w = windows[j] || {}
            rows.push({ kind: "window", identifier: w.identifier || "", title: w.title || "",
                        appId: w.app_id || "" })
        }
        return rows
    }

    implicitHeight: Math.min(contentHeight, 5 * 52 + 4 * spacing + 2 * 24)
    model: buildModel()
    clip: true
    spacing: 4
    currentIndex: 0
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: true
    FishUI.ScrollBar.vertical: FishUI.ScrollBar {}

    section.property: "kind"
    section.criteria: ViewSection.FullString
    section.delegate: FishUI.Label {
        width: ListView.view.width
        height: 24
        leftPadding: 12
        verticalAlignment: Text.AlignVCenter
        text: section === "window" ? qsTr("Windows") : qsTr("Displays")
        font.pixelSize: 11
        color: FishUI.Theme.disabledTextColor
        // A single kind needs no heading.
        visible: (prompt.request.monitors || []).length > 0 && (prompt.request.windows || []).length > 0
    }

    delegate: Item {
        id: row
        width: ListView.view.width
        height: 52

        readonly property bool selected: choices.currentIndex === index
        // modelData is gone for a moment while the window tears down.
        readonly property var entry: modelData ? modelData : ({})
        readonly property bool isWindow: row.entry.kind === "window"
        readonly property string label: row.isWindow ? (row.entry.title || row.entry.appId || "")
                                                     : (row.entry.description || row.entry.name || "")
        readonly property string detail: row.isWindow ? (row.entry.appId || "")
                                                      : (row.entry.name ? row.entry.name + "  ·  " + row.entry.width + " × " + row.entry.height : "")
        readonly property string icon: row.isWindow ? (prompt.applicationIcon(row.entry.appId || "") || "window")
                                                    : "video-display"

        MouseArea {
            id: rowArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: choices.currentIndex = index
            onDoubleClicked: choices.accepted()
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
                source: row.icon
            }

            ColumnLayout {
                spacing: 1
                Layout.fillWidth: true

                FishUI.Label {
                    Layout.fillWidth: true
                    text: row.label
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    color: row.selected ? FishUI.Theme.highlightedTextColor : FishUI.Theme.textColor
                }

                FishUI.Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: row.detail
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
