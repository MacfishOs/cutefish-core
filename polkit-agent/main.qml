import QtQuick 2.12
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3
import FishUI 1.0 as FishUI

Item {
    id: root

    property var heightValue: mainLayout.implicitHeight + 32

    width: 420
    height: heightValue

    Connections {
        target: confirmation
        function onFailure() {
            // Enter would otherwise send the rejected password again.
            passwordInput.text = ""
            doneButton.enabled = true
            passwordInput.enabled = true
            passwordInput.forceActiveFocus()
        }

        // Each further PAM question starts empty; a field still being typed
        // in is left alone.
        function onRequested() {
            if (!passwordInput.enabled)
                passwordInput.text = ""

            doneButton.enabled = true
            passwordInput.enabled = true
            passwordInput.forceActiveFocus()
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
        confirmation.rejected()
    }

    // An empty response would still count as an attempt, and pass on an
    // account without a password.
    function submit() {
        if (!passwordInput.text)
            return

        doneButton.enabled = false
        passwordInput.enabled = false
        confirmation.setConfirmationResult(passwordInput.text)
    }

    Rectangle {
        anchors.fill: parent
        radius: 16
        color: FishUI.Theme.secondBackgroundColor
    }

    DragHandler {
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        grabPermissions: TapHandler.CanTakeOverFromAnything
        onActiveChanged: if (active) { windowController.startSystemMove() }
    }

    FishUI.WindowController {
        id: windowController
        window: rootWindow
        restoresSize: false
    }

    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        Column {
            Layout.fillWidth: true
            spacing: 4

            FishUI.Label {
                width: root.width - 32
                text: "Authentication Required"
                textFormat: Text.PlainText
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: FishUI.Theme.textColor
            }

            FishUI.Label {
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

        FishUI.TextField {
            text: confirmation.identity
            enabled: false
            Layout.fillWidth: true
        }

        FishUI.TextField {
            id: passwordInput
            placeholderText: confirmation.prompt !== "" ? confirmation.prompt : qsTr("Password")
            echoMode: confirmation.echo ? TextInput.Normal : TextInput.Password
            Layout.fillWidth: true
            selectByMouse: true
            focus: true

            onAccepted: root.submit()

            Keys.onEscapePressed: {
                confirmation.rejected()
            }
        }

        FishUI.Label {
            Layout.fillWidth: true
            visible: confirmation.notice !== ""
            text: confirmation.notice
            textFormat: Text.PlainText
            font.pixelSize: 14
            wrapMode: Text.Wrap
            color: confirmation.noticeIsError ? FishUI.Theme.redColor : FishUI.Theme.disabledTextColor
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
                onClicked: root.submit()
            }
        }
    }
}
