import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FishUI 1.0 as FishUI
import Cutefish.Application

// The shared frame: who is asking, the kind-specific content, and the three
// answers. Content components expose `title`, `description`, `ready` and
// `details()`; see ScreenRecordingContent.qml.
FishUI.Window {
    id: root
    WindowHints.skipTaskbar: true

    readonly property int contentWidth: 440
    readonly property int contentHeight: mainLayout.implicitHeight + 40

    width: contentWidth
    height: contentHeight
    minimumWidth: contentWidth
    maximumWidth: contentWidth
    minimumHeight: contentHeight
    maximumHeight: contentHeight
    visible: false
    title: content.item ? content.item.title : ""
    flags: Qt.Dialog | Qt.FramelessWindowHint
    minimizeButtonVisible: false
    closeButtonVisible: false
    header.height: 0
    contentTopMargin: 0
    background.opacity: 1
    background.color: FishUI.Theme.secondBackgroundColor
    // The background item reaches a grown window a frame or two after its buffer does.
    color: background.color

    function toggleMaximized() {}

    function allow() {
        if (content.item && content.item.ready)
            prompt.allow(content.item.details())
    }

    Shortcut { sequence: "Escape"; onActivated: prompt.deny() }
    Shortcut { sequence: "Return"; onActivated: root.allow() }

    // No title bar, so any empty part of the dialog drags it.
    DragHandler {
        target: null
        onActiveChanged: if (active && root.controller) root.controller.startSystemMove()
    }

    ColumnLayout {
        id: mainLayout
        // Not filled to the window: the window height follows this layout's implicit
        // height a polish later, and a filled layout would be spread over the old height.
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20
        spacing: 16

        RowLayout {
            spacing: 14
            Layout.fillWidth: true

            FishUI.IconItem {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                Layout.alignment: Qt.AlignTop
                source: prompt.application.icon ? prompt.application.icon : prompt.icon
            }

            ColumnLayout {
                spacing: 4
                Layout.fillWidth: true

                FishUI.Label {
                    Layout.fillWidth: true
                    text: content.item ? content.item.title : ""
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font: Qt.font({ family: FishUI.Theme.fontFamily, pixelSize: 14, weight: Font.DemiBold })
                    color: FishUI.Theme.textColor
                }

                FishUI.Label {
                    Layout.fillWidth: true
                    text: content.item ? content.item.description : ""
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    color: FishUI.Theme.disabledTextColor
                }
            }
        }

        Loader {
            id: content
            Layout.fillWidth: true
            source: "qrc:/permissions/" + prompt.content
            onLoaded: item.accepted.connect(function() { root.allow() })
        }

        RowLayout {
            spacing: 8
            Layout.fillWidth: true

            FishUI.ConfirmDialogButton {
                text: qsTr("Cancel")
                onClicked: prompt.deny()
            }

            Item { Layout.fillWidth: true }

            FishUI.ConfirmDialogButton {
                primary: true
                focus: true
                text: content.item ? content.item.accept : qsTr("Allow")
                enabled: content.item && content.item.ready
                onClicked: root.allow()
            }
        }
    }
}
