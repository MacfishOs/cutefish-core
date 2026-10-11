// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FishUI as FishUI

// Creating these once loads and links their types for the application the
// booster turns into; the Component below is only compiled, since its types
// open windows.
Item {
    ColumnLayout {
        RowLayout {
            FishUI.Label { }
            FishUI.Icon { }
            FishUI.IconItem { }
        }
        FishUI.RoundedRectangle { }
        FishUI.Switch { }
        FishUI.TextField { }
        FishUI.ComboBox { }
        FishUI.Button { }
        FishUI.RoundImageButton { }
        FishUI.HeaderButton { }
        FishUI.TabButton { }
        FishUI.AlignedLabel { }
        FishUI.BusyIndicator { }
        FishUI.SegmentedControl { }
        FishUI.ProgressBar { }
        FishUI.Slider { }
        FishUI.CheckBox { }
        FishUI.RadioButton { }
        FishUI.TextArea { }
        ListView {
            FishUI.ScrollBar.vertical: FishUI.ScrollBar { }
        }
        GridView { }
        Flickable { }
    }

    Component {
        Item {
            FishUI.Window { }
            FishUI.DialogWindow { }
            FishUI.ConfirmDialog {
                FishUI.ConfirmDialogButton { }
            }
            FishUI.DesktopMenu {
                FishUI.MenuItem { }
                FishUI.MenuSeparator { }
            }
            FishUI.ToolTip { }
            FishUI.AboutDialog { }
            FishUI.TabView { }
            FishUI.StackView { }
            FishUI.Page { }
            FishUI.HeaderBackdrop { }
            FishUI.FloatingCard { }
            FishUI.OpacityMask { }
            FishUI.ColorOverlay { }
            FishUI.FastBlur { }
            FishUI.DropShadow { }
            FishUI.WheelHandler { }
        }
    }
}
