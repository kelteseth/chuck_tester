import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: mainWindow
    width: 420
    height: 320
    visible: true
    title: "Tester Popup Example"

    Drawer {
        id: navDrawer
        objectName: "navDrawer"
        width: 200
        height: mainWindow.height
        edge: Qt.LeftEdge

        Label {
            objectName: "drawerLabel"
            anchors.centerIn: parent
            text: "Drawer content"
        }
    }

    Dialog {
        id: confirmDialog
        objectName: "confirmDialog"
        anchors.centerIn: parent
        title: "Confirm"
        standardButtons: Dialog.Ok | Dialog.Cancel

        Label {
            objectName: "confirmDialogMessage"
            text: "Proceed?"
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 12

        Button {
            objectName: "openDrawerBtn"
            text: "Open drawer"
            Layout.alignment: Qt.AlignHCenter
            onClicked: navDrawer.open()
        }

        Button {
            objectName: "openDialogBtn"
            text: "Open dialog"
            Layout.alignment: Qt.AlignHCenter
            onClicked: confirmDialog.open()
        }
    }
}
