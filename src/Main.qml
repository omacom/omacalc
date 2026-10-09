import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 400
    height: 568
    minimumWidth: 300
    minimumHeight: 430
    visible: false
    title: "Omacalc"

    readonly property bool darkMode: backend.darkMode
    readonly property color pageColor: backend.themeBackground
    readonly property color inkColor: backend.themeForeground
    // Every hardcoded size in the interface is expressed at the 400 × 568
    // design size; resizing the window scales the whole face with it. The
    // launch size below already includes the desktop text scale. A scale
    // change while the window is open waits until the next launch.
    readonly property real uiScale: Math.min(width / 400, height / 568)

    function mixColors(base, tint, amount) {
        return Qt.rgba(
            base.r + (tint.r - base.r) * amount,
            base.g + (tint.g - base.g) * amount,
            base.b + (tint.b - base.b) * amount, 1);
    }
    readonly property color mutedColor: mixColors(pageColor, inkColor, 0.5)

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * uiScale));
    }

    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.accent: backend.themeAccent
    color: pageColor

    Shortcut {
        sequences: ["Ctrl+C", "Meta+C"]
        context: Qt.ApplicationShortcut
        onActivated: backend.copyResult()
    }

    Shortcut {
        sequences: ["Ctrl+V", "Meta+V"]
        context: Qt.ApplicationShortcut
        onActivated: backend.pasteNumber()
    }

    Shortcut {
        sequence: "Ctrl+Q"
        context: Qt.ApplicationShortcut
        onActivated: win.close()
    }

    Item {
        id: face
        anchors.fill: parent
        anchors.margins: win.scaledSize(20)
        focus: true

        Keys.onPressed: function(event) {
            if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                return;

            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                    || event.text === "=") {
                backend.pressKey("=");
            } else if (event.key === Qt.Key_Backspace) {
                backend.pressKey("backspace");
            } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Delete) {
                backend.pressKey("clear");
            } else if (event.text === "," || event.text === ".") {
                backend.pressKey(".");
            } else if (event.text === "s" || event.text === "S") {
                backend.pressKey("sign");
            } else if (event.text === "c" || event.text === "C") {
                backend.pressKey("clear");
            } else if (/^[0-9+\-*/%]$/.test(event.text)) {
                backend.pressKey(event.text);
            } else {
                return;
            }
            event.accepted = true;
        }

        Item {
            id: displayArea
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: divider.top
            anchors.leftMargin: win.scaledSize(8)
            anchors.rightMargin: win.scaledSize(8)
            anchors.bottomMargin: win.scaledSize(24)

            Text {
                id: expressionText
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                text: backend.expression
                color: win.mutedColor
                elide: Text.ElideLeft
                font.family: "iA Writer Mono S"
                font.pixelSize: win.scaledSize(21)
            }

            Text {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                horizontalAlignment: Text.AlignRight
                text: backend.display
                color: win.inkColor
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: win.scaledSize(22)
                font.family: "iA Writer Mono S"
                font.pixelSize: win.scaledSize(76)
            }
        }

        Rectangle {
            id: divider
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: keypad.top
            anchors.bottomMargin: win.scaledSize(22)
            height: 1
            color: win.mixColors(win.pageColor, win.inkColor, 0.16)
        }

        GridLayout {
            id: keypad
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Math.round(parent.height * 0.68)
            columns: 4
            rowSpacing: win.scaledSize(12)
            columnSpacing: win.scaledSize(12)

            Repeater {
                model: [
                    { label: "AC", key: "clear", kind: "number" },
                    { label: "±", key: "sign", kind: "number" },
                    { label: "%", key: "%", kind: "number" },
                    { label: "÷", key: "÷", kind: "operator" },
                    { label: "7", key: "7", kind: "number" },
                    { label: "8", key: "8", kind: "number" },
                    { label: "9", key: "9", kind: "number" },
                    { label: "×", key: "×", kind: "operator" },
                    { label: "4", key: "4", kind: "number" },
                    { label: "5", key: "5", kind: "number" },
                    { label: "6", key: "6", kind: "number" },
                    { label: "−", key: "-", kind: "operator" },
                    { label: "1", key: "1", kind: "number" },
                    { label: "2", key: "2", kind: "number" },
                    { label: "3", key: "3", kind: "number" },
                    { label: "+", key: "+", kind: "operator" },
                    { label: "0", key: "0", kind: "number" },
                    { label: ".", key: ".", kind: "number" },
                    { label: "", key: "backspace", kind: "number", icon: "backspace" },
                    { label: "=", key: "=", kind: "equals" }
                ]

                CalcButton {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    label: modelData.label
                    keyValue: modelData.key
                    kind: modelData.kind
                    iconName: modelData.icon === undefined ? "" : modelData.icon
                    pageColor: win.pageColor
                    inkColor: win.inkColor
                    onActivated: backend.pressKey(keyValue)
                }
            }
        }
    }

    Component.onCompleted: {
        // Chosen once. Hyprland reports a normal float as maximized, so a
        // saved rectangle was the tiled size and the next launch filled the
        // screen. Position stays with the compositor.
        var size = backend.defaultWindowSize();
        width = size.width;
        height = size.height;
        visible = true;
    }
}
