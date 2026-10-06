// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FishUI 1.0 as FishUI
import Cutefish.Application

FishUI.Window {
    id: root
    WindowHints.skipTaskbar: true

    // Wider only for the report, whose lines do not wrap.
    readonly property int contentWidth: detailsShown ? 520 : 400
    readonly property int contentHeight: mainLayout.implicitHeight + 32
    readonly property bool hasReport: crash.report.length > 0
    // The session restarts its own processes.
    readonly property bool canReopen: !crash.sessionProcess
    property bool detailsShown: false

    width: contentWidth
    height: contentHeight
    minimumWidth: contentWidth
    maximumWidth: contentWidth
    minimumHeight: contentHeight
    maximumHeight: contentHeight
    visible: false
    title: titleLabel.text
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

    Shortcut { sequence: "Escape"; onActivated: crash.close() }
    Shortcut { sequence: "Return"; onActivated: root.accept() }

    function accept() {
        if (root.canReopen)
            crash.reopen()
        else
            crash.close()
    }

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
        anchors.margins: 16
        spacing: 12

        RowLayout {
            spacing: 14
            Layout.fillWidth: true

            FishUI.IconItem {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                Layout.alignment: Qt.AlignTop
                source: crash.icon
            }

            ColumnLayout {
                spacing: 4
                Layout.fillWidth: true

                FishUI.Label {
                    id: titleLabel
                    Layout.fillWidth: true
                    text: qsTr("“%1” quit unexpectedly").arg(crash.name)
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font: Qt.font({ family: FishUI.Theme.fontFamily, pixelSize: 14, weight: Font.DemiBold })
                    color: FishUI.Theme.textColor
                }

                FishUI.Label {
                    Layout.fillWidth: true
                    text: {
                        var reason = crash.reason.length > 0 ? qsTr("Reason: %1").arg(crash.reason) : ""
                        if (!crash.sessionProcess)
                            return reason
                        var state = qsTr("It is restarted automatically.")
                        return reason.length > 0 ? state + "\n" + reason : state
                    }
                    visible: text.length > 0
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    color: FishUI.Theme.disabledTextColor
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 220
            visible: root.detailsShown && root.hasReport
            radius: FishUI.Theme.smallRadius
            color: FishUI.Theme.alternateBackgroundColor

            Flickable {
                id: flickable
                anchors.fill: parent
                anchors.margins: 8
                contentWidth: reportText.implicitWidth
                contentHeight: reportText.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                FishUI.ScrollBar.vertical: FishUI.ScrollBar {}
                FishUI.ScrollBar.horizontal: FishUI.ScrollBar {}

                TextEdit {
                    id: reportText
                    text: crash.report
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    font.family: crash.fixedFont
                    font.pixelSize: 11
                    color: FishUI.Theme.textColor
                    selectionColor: FishUI.Theme.highlightColor
                    selectedTextColor: FishUI.Theme.highlightedTextColor
                    renderType: FishUI.Theme.renderType
                }
            }
        }

        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            Layout.topMargin: 8

            FishUI.ConfirmDialogButton {
                visible: root.hasReport
                text: root.detailsShown ? qsTr("Hide Details") : qsTr("Show Details")
                onClicked: root.detailsShown = !root.detailsShown
            }

            Item { Layout.fillWidth: true }

            FishUI.ConfirmDialogButton {
                visible: root.canReopen
                text: qsTr("Close")
                onClicked: crash.close()
            }

            FishUI.ConfirmDialogButton {
                primary: true
                focus: true
                text: root.canReopen ? qsTr("Reopen") : qsTr("OK")
                onClicked: root.accept()
            }
        }
    }
}
