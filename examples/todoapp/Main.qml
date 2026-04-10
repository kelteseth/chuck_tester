import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import TodoApp

ApplicationWindow {
    id: root
    objectName: "root"
    width: 420
    height: 640
    visible: true
    title: "Todo App"

    Material.theme: Material.Light
    Material.accent: Material.Teal
    Material.primary: Material.Indigo

    Todo { id: todo }

    menuBar: MenuBar {
        objectName: "menuBar"

        Menu {
            title: "&File"

            Action {
                text: "&Save"
                shortcut: "Ctrl+S"
                onTriggered: todo.save()
            }
            Action {
                text: "&Load"
                shortcut: "Ctrl+L"
                onTriggered: todo.load()
            }
            MenuSeparator {}
            Action {
                text: "&Quit"
                shortcut: "Ctrl+Q"
                onTriggered: Qt.quit()
            }
        }
    }

    StackView {
        id: stack
        objectName: "stack"
        anchors.fill: parent

        initialItem: TodoListPage {
            todoModel: todo
        }
    }

    // Push the edit page from anywhere via this helper
    function editTodo(index: int) {
        stack.push(editComp, { todoModel: todo, todoIndex: index })
    }

    Component {
        id: editComp
        TodoEditPage {}
    }
}
