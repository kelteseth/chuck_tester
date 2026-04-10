import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import TodoApp

Page {
    id: editPage
    objectName: "editPage"

    required property Todo todoModel
    required property int todoIndex

    readonly property bool isNew: todoIndex < 0

    header: ToolBar {
        objectName: "editToolbar"
        Material.foreground: "white"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 8

            ToolButton {
                objectName: "backBtn"
                text: "\u2190"
                font.pixelSize: 20
                onClicked: editPage.StackView.view.pop()
            }

            Label {
                text: isNew ? "New Todo" : "Edit Todo"
                font.pixelSize: 20
                font.weight: Font.Medium
                Layout.fillWidth: true
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 24

        Label {
            text: isNew ? "What needs to be done?" : "Update your todo"
            font.pixelSize: 14
            opacity: 0.6
        }

        TextField {
            id: titleField
            objectName: "titleField"
            Layout.fillWidth: true
            placeholderText: "Enter todo title..."
            text: isNew ? "" : todoModel.titleAt(todoIndex)
            font.pixelSize: 16
            Component.onCompleted: forceActiveFocus()

            Keys.onReturnPressed: saveAndPop()
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Button {
                objectName: "cancelBtn"
                text: "Cancel"
                flat: true
                onClicked: editPage.StackView.view.pop()
            }

            Item { Layout.fillWidth: true }

            Button {
                objectName: "saveBtn"
                text: "Save"
                highlighted: true
                enabled: titleField.text.length > 0
                Material.elevation: 2
                onClicked: saveAndPop()
            }
        }
    }

    function saveAndPop() {
        if (titleField.text.length === 0)
            return
        if (isNew)
            todoModel.add(titleField.text)
        else
            todoModel.setTitle(todoIndex, titleField.text)
        StackView.view.pop()
    }
}
