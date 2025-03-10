/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Templates 2.15 as T
import StudioTheme 1.0 as StudioTheme

T.ComboBox {
    id: myComboBox

    property alias actionIndicator: actionIndicator
    property alias labelColor: comboBoxInput.color

    // This property is used to indicate the global hover state
    property bool hover: (comboBoxInput.hover || actionIndicator.hover || popupIndicator.hover)
                         && myComboBox.enabled
    property bool edit: myComboBox.activeFocus && myComboBox.editable
    property bool open: comboBoxPopup.opened

    property bool dirty: false // user modification flag

    property alias actionIndicatorVisible: actionIndicator.visible
    property real __actionIndicatorWidth: StudioTheme.Values.actionIndicatorWidth
    property real __actionIndicatorHeight: StudioTheme.Values.actionIndicatorHeight

    property alias textInput: comboBoxInput

    signal compressedActivated(int index, int reason)

    enum ActivatedReason { EditingFinished, Other }

    width: StudioTheme.Values.defaultControlWidth
    height: StudioTheme.Values.defaultControlHeight

    leftPadding: actionIndicator.width
    rightPadding: popupIndicator.width + StudioTheme.Values.border
    font.pixelSize: StudioTheme.Values.myFontSize
    wheelEnabled: false

    onFocusChanged: {
        if (!myComboBox.focus)
            comboBoxPopup.close()
    }

    ActionIndicator {
        id: actionIndicator
        myControl: myComboBox
        x: 0
        y: 0
        width: actionIndicator.visible ? myComboBox.__actionIndicatorWidth : 0
        height: actionIndicator.visible ? myComboBox.__actionIndicatorHeight : 0
    }

    contentItem: ComboBoxInput {
        id: comboBoxInput
        myControl: myComboBox
        text: myComboBox.editText

        onEditingFinished: {
            // Only trigger the signal, if the value was modified
            if (myComboBox.dirty) {
                myTimer.stop()
                myComboBox.dirty = false
                myComboBox.compressedActivated(myComboBox.find(myComboBox.editText),
                                               ComboBox.ActivatedReason.EditingFinished)
            }
        }
        onTextEdited: myComboBox.dirty = true
    }

    indicator: CheckIndicator {
        id: popupIndicator
        myControl: myComboBox
        myPopup: myComboBox.popup
        x: comboBoxInput.x + comboBoxInput.width
        y: StudioTheme.Values.border
        width: StudioTheme.Values.checkIndicatorWidth - StudioTheme.Values.border
        height: StudioTheme.Values.checkIndicatorHeight - (StudioTheme.Values.border * 2)
    }

    background: Rectangle {
        id: comboBoxBackground
        color: StudioTheme.Values.themeControlBackground
        border.color: StudioTheme.Values.themeControlOutline
        border.width: StudioTheme.Values.border
        x: actionIndicator.width
        width: myComboBox.width - actionIndicator.width
        height: myComboBox.height
    }

    Timer {
        id: myTimer
        property int activatedIndex
        repeat: false
        running: false
        interval: 100
        onTriggered: myComboBox.compressedActivated(myTimer.activatedIndex,
                                                    ComboBox.ActivatedReason.Other)
    }

    onActivated: {
        myTimer.activatedIndex = index
        myTimer.restart()
    }

    delegate: ItemDelegate {
        id: myItemDelegate

        width: comboBoxPopup.width - comboBoxPopup.leftPadding - comboBoxPopup.rightPadding
               - (comboBoxPopupScrollBar.visible ? comboBoxPopupScrollBar.contentItem.implicitWidth
                                                   + 2 : 0) // TODO Magic number
        height: StudioTheme.Values.height - 2 * StudioTheme.Values.border
        padding: 0
        enabled: model.enabled === undefined ? true : model.enabled

        contentItem: Text {
            leftPadding: itemDelegateIconArea.width
            text: myComboBox.textRole ? (Array.isArray(myComboBox.model) ? modelData[myComboBox.textRole]
                                                                         : model[myComboBox.textRole]) : modelData
            color: {
                if (!myItemDelegate.enabled)
                    return StudioTheme.Values.themeTextColorDisabled

                return myItemDelegate.highlighted ? StudioTheme.Values.themeTextSelectedTextColor
                                                  : StudioTheme.Values.themeTextColor
            }
            font: myComboBox.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Item {
            id: itemDelegateIconArea
            width: myItemDelegate.height
            height: myItemDelegate.height

            T.Label {
                id: itemDelegateIcon
                text: StudioTheme.Constants.tickIcon
                color: myItemDelegate.highlighted ? StudioTheme.Values.themeTextSelectedTextColor
                                                  : StudioTheme.Values.themeTextColor
                font.family: StudioTheme.Constants.iconFont.family
                font.pixelSize: StudioTheme.Values.spinControlIconSizeMulti
                visible: myComboBox.currentIndex === index ? true : false
                anchors.fill: parent
                renderType: Text.NativeRendering
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        highlighted: myComboBox.highlightedIndex === index

        background: Rectangle {
            id: itemDelegateBackground
            x: 0
            y: 0
            width: myItemDelegate.width
            height: myItemDelegate.height
            color: myItemDelegate.highlighted ? StudioTheme.Values.themeInteraction : "transparent"
        }
    }

    popup: T.Popup {
        id: comboBoxPopup
        x: actionIndicator.width + StudioTheme.Values.border
        y: myComboBox.height
        width: myComboBox.width - actionIndicator.width - (StudioTheme.Values.border * 2)
        // TODO Setting the height on the popup solved the problem with the popup of height 0,
        // but it has the problem that it sometimes extend over the border of the actual window
        // and is then cut off.
        height: Math.min(contentItem.implicitHeight + comboBoxPopup.topPadding
                         + comboBoxPopup.bottomPadding,
                         myComboBox.Window.height - topMargin - bottomMargin,
                         StudioTheme.Values.maxComboBoxPopupHeight)
        padding: StudioTheme.Values.border
        margins: 0 // If not defined margin will be -1
        closePolicy: T.Popup.CloseOnPressOutside | T.Popup.CloseOnPressOutsideParent
                     | T.Popup.CloseOnEscape | T.Popup.CloseOnReleaseOutside
                     | T.Popup.CloseOnReleaseOutsideParent

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: myComboBox.popup.visible ? myComboBox.delegateModel : null
            currentIndex: myComboBox.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                id: comboBoxPopupScrollBar
            }
        }

        background: Rectangle {
            color: StudioTheme.Values.themePopupBackground
            border.width: 0
        }

        enter: Transition {}
        exit: Transition {}
    }

    states: [
        State {
            name: "default"
            when: myComboBox.enabled && !myComboBox.hover && !myComboBox.edit && !myComboBox.open
                  && !myComboBox.activeFocus
            PropertyChanges {
                target: myComboBox
                wheelEnabled: false
            }
            PropertyChanges {
                target: comboBoxInput
                selectByMouse: false
            }
            PropertyChanges {
                target: comboBoxBackground
                color: StudioTheme.Values.themeControlBackground
                border.color: StudioTheme.Values.themeControlOutline
            }
        },
        // This state is intended for ComboBoxes which aren't editable, but have focus e.g. via
        // tab focus. It is therefor possible to use the mouse wheel to scroll through the items.
        State {
            name: "focus"
            when: myComboBox.enabled && myComboBox.activeFocus && !myComboBox.editable
                  && !myComboBox.open
            PropertyChanges {
                target: myComboBox
                wheelEnabled: true
            }
            PropertyChanges {
                target: comboBoxInput
                focus: true
            }
        },
        State {
            name: "edit"
            when: myComboBox.enabled && myComboBox.edit && !myComboBox.open
            PropertyChanges {
                target: myComboBox
                wheelEnabled: true
            }
            PropertyChanges {
                target: comboBoxInput
                selectByMouse: true
                readOnly: false
            }
            PropertyChanges {
                target: comboBoxBackground
                color: StudioTheme.Values.themeControlBackgroundInteraction
                border.color: StudioTheme.Values.themeControlOutline
            }
            StateChangeScript {
                script: comboBoxPopup.close()
            }
        },
        State {
            name: "popup"
            when: myComboBox.enabled && myComboBox.open
            PropertyChanges {
                target: myComboBox
                wheelEnabled: true
            }
            PropertyChanges {
                target: comboBoxInput
                selectByMouse: false
                readOnly: true
            }
            PropertyChanges {
                target: comboBoxBackground
                color: StudioTheme.Values.themeControlBackgroundInteraction
                border.color: StudioTheme.Values.themeControlOutlineInteraction
            }
        },
        State {
            name: "disable"
            when: !myComboBox.enabled
            PropertyChanges {
                target: comboBoxBackground
                color: StudioTheme.Values.themeControlBackgroundDisabled
                border.color: StudioTheme.Values.themeControlOutlineDisabled
            }
        }
    ]

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape)
            myComboBox.focus = false
    }
}
