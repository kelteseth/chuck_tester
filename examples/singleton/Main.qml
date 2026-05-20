import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SingletonExample

ApplicationWindow {
    id: mainWindow
    objectName: "mainWindow"
    width: 360
    height: 220
    visible: true
    title: "Tester Singleton Example"

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 12

        Label {
            objectName: "messageLabel"
            text: AppState.message
            font.pixelSize: 18
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            objectName: "counterLabel"
            text: "Counter: " + AppState.counter
            Layout.alignment: Qt.AlignHCenter
        }

        Button {
            objectName: "incBtn"
            text: "Increment"
            Layout.alignment: Qt.AlignHCenter
            onClicked: AppState.increment()
        }
    }
}
