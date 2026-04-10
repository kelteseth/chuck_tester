import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import TodoApp

Page {
    id: listPage
    objectName: "listPage"

    required property Todo todoModel

    header: ToolBar {
        objectName: "listToolbar"
        Material.foreground: "white"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 8

            Label {
                text: "Todos"
                font.pixelSize: 20
                font.weight: Font.Medium
                Layout.fillWidth: true
            }

            Label {
                text: todoModel.count > 0 ? todoModel.count + " items" : ""
                font.pixelSize: 14
                opacity: 0.7
            }
        }
    }

    ListView {
        id: todoList
        objectName: "todoList"
        anchors.fill: parent
        clip: true
        model: todoModel
        spacing: 0

        delegate: ItemDelegate {
            id: del
            objectName: "todoItem_" + index
            width: todoList.width
            height: 64

            required property string title
            required property bool done
            required property int index

            contentItem: RowLayout {
                spacing: 8

                CheckBox {
                    id: checkBox
                    objectName: "check_" + del.index
                    checked: del.done
                    onToggled: todoModel.toggle(del.index)
                }

                Label {
                    objectName: "label_" + del.index
                    text: del.title
                    font.pixelSize: 16
                    font.strikeout: del.done
                    opacity: del.done ? 0.45 : 1.0
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                RoundButton {
                    objectName: "editBtn_" + del.index
                    text: "\u270E"
                    flat: true
                    font.pixelSize: 16
                    implicitWidth: 40
                    implicitHeight: 40
                    onClicked: root.editTodo(del.index)
                }

                RoundButton {
                    objectName: "deleteBtn_" + del.index
                    text: "\u2715"
                    flat: true
                    font.pixelSize: 14
                    implicitWidth: 40
                    implicitHeight: 40
                    Material.foreground: Material.Red
                    onClicked: todoModel.remove(del.index)
                }
            }
        }

        // Empty-state placeholder
        Column {
            anchors.centerIn: parent
            spacing: 16
            visible: todoModel.count === 0

            Label {
                text: "\u2611"
                font.pixelSize: 64
                opacity: 0.15
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Label {
                text: "No todos yet"
                font.pixelSize: 18
                font.weight: Font.Medium
                opacity: 0.4
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Label {
                text: "Tap + to add one"
                font.pixelSize: 14
                opacity: 0.3
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }

    RoundButton {
        id: addBtn
        objectName: "addBtn"
        text: "+"
        font.pixelSize: 24
        width: 56
        height: 56
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 16
        anchors.bottomMargin: 16
        highlighted: true
        Material.elevation: 6

        onClicked: root.editTodo(-1)
    }
}
