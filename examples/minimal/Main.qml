import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: mainWindow
    objectName: "mainWindow"
    width: 400
    height: 300
    visible: true
    title: "Tester Minimal Example"

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 16

        Label {
            id: titleLabel
            objectName: "titleLabel"
            text: "Hello from Tester!"
            font.pixelSize: 24
            Layout.alignment: Qt.AlignHCenter
        }

        TextField {
            id: nameField
            objectName: "nameField"
            placeholderText: "Enter your name..."
            Layout.preferredWidth: 250
        }

        Button {
            objectName: "greetBtn"
            text: "Greet"
            Layout.alignment: Qt.AlignHCenter
            onClicked: titleLabel.text = "Hello, " + nameField.text + "!"
        }

        Button {
            objectName: "resetBtn"
            text: "Reset"
            Layout.alignment: Qt.AlignHCenter
            onClicked: {
                nameField.text = ""
                titleLabel.text = "Hello from Tester!"
            }
        }
    }
}
