import QtQuick 2.12
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3
import FishUI 1.0 as FishUI

Item {
    id: root

    property var heightValue: mainLayout.implicitHeight + 32

    width: 450
    height: heightValue

    Connections {
        target: confirmation
        function onFailure() {
            doneButton.enabled = true
            passwordInput.enabled = true
        }
    }

    onHeightValueChanged: {
        rootWindow.height = heightValue
        rootWindow.maximumHeight = heightValue
        rootWindow.minimumHeight = heightValue
        rootWindow.maximumWidth = root.width
        rootWindow.minimumWidth = root.width
    }

    Keys.enabled: true
    Keys.onEscapePressed: {
        confirmation.setConfirmationResult("")
    }

    Rectangle {
        id: _background
        anchors.fill: parent
        radius: 16
        color: FishUI.Theme.secondBackgroundColor
    }

    DragHandler {
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        grabPermissions: TapHandler.CanTakeOverFromAnything
        onActiveChanged: if (active) { windowHelper.startSystemMove(rootWindow) }
    }

    FishUI.WindowHelper {
        id: windowHelper
    }

    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        Column {
            Layout.fillWidth: true
            spacing: 4

            Label {
                width: root.width - 32
                text: "Authentication Required"
                textFormat: Text.PlainText
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: FishUI.Theme.textColor
            }

            Label {
                width: root.width - 32
                text: confirmation.message
                textFormat: Text.PlainText
                font.pixelSize: 14
                wrapMode: Text.Wrap
                lineHeight: 20
                lineHeightMode: Text.FixedHeight
                color: FishUI.Theme.disabledTextColor
            }
        }

        TextField {
            id: userTextField
            text: confirmation.identity
            enabled: false
            Layout.fillWidth: true
        }

        TextField {
            id: passwordInput
            placeholderText: qsTr("Password")
            echoMode: TextField.Password
            Layout.fillWidth: true
            selectByMouse: true
            focus: true

            onAccepted: {
                if (passwordInput.text)
                    confirmation.setConfirmationResult(passwordInput.text)
            }

            Keys.onEscapePressed: {
                confirmation.setConfirmationResult("")
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8

            FishUI.ConfirmDialogButton {
                text: qsTr("Cancel")
                onClicked: confirmation.rejected()
            }

            FishUI.ConfirmDialogButton {
                id: doneButton
                text: "Authenticate"
                primary: true
                onClicked: {
                    doneButton.enabled = false
                    passwordInput.enabled = false
                    confirmation.setConfirmationResult(passwordInput.text)
                }
            }
        }
    }
}
