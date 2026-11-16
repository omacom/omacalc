import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 400
    height: 568
    minimumWidth: backend.scientific ? 600 : 300
    minimumHeight: 430
    visible: false
    title: qsTr("Calculator")

    readonly property bool darkMode: backend.darkMode
    readonly property color pageColor: backend.themeBackground
    readonly property color inkColor: backend.themeForeground
    // Every hardcoded size in the interface is expressed at the 400 × 568
    // design size; resizing the window scales the whole face with it. The
    // launch size below already includes the desktop text scale. A scale
    // change while the window is open waits until the next launch.
    // Scientific mode adds four key columns, doubling the design width.
    readonly property int designWidth: backend.scientific ? 800 : 400
    readonly property real uiScale: Math.min(width / designWidth, height / 568)

    // Grow or shrink sideways by the four columns, keeping the scale the face
    // is drawn at. A tiled window keeps its size and the keys narrow instead.
    function toggleScientific() {
        var scale = uiScale;
        backend.scientific = !backend.scientific;
        width = Math.max(minimumWidth, Math.round(designWidth * scale));
    }

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

        // The numpad without NumLock sends navigation keys. A calculator has no
        // use for those, and a compositor can hand a freshly focused window a
        // keymap that drops NumLock, so read the numpad as digits either way.
        readonly property var numpadKeys: ({
            [Qt.Key_Insert]: "0", [Qt.Key_End]: "1", [Qt.Key_Down]: "2",
            [Qt.Key_PageDown]: "3", [Qt.Key_Left]: "4", [Qt.Key_Clear]: "5",
            [Qt.Key_Right]: "6", [Qt.Key_Home]: "7", [Qt.Key_Up]: "8",
            [Qt.Key_PageUp]: "9", [Qt.Key_Delete]: "."
        })

        Keys.onPressed: function(event) {
            if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                return;

            if ((event.modifiers & Qt.KeypadModifier) && event.key in numpadKeys) {
                backend.pressKey(numpadKeys[event.key]);
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
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
            } else if (event.text === "x" || event.text === "X") {
                backend.pressKey("*");
            } else if (/^[0-9+\-*/%^()]$/.test(event.text)) {
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

            Rectangle {
                id: scientificToggle
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.leftMargin: -win.scaledSize(8)
                width: win.scaledSize(44)
                height: win.scaledSize(30)
                radius: height / 2
                color: backend.scientific
                    ? win.mixColors(win.pageColor, win.inkColor, toggleArea.containsMouse ? 0.24 : 0.16)
                    : win.mixColors(win.pageColor, win.inkColor, toggleArea.containsMouse ? 0.08 : 0)

                Accessible.role: Accessible.CheckBox
                Accessible.name: qsTr("Scientific")
                Accessible.checkable: true
                Accessible.checked: backend.scientific
                Accessible.onPressAction: win.toggleScientific()

                Text {
                    anchors.centerIn: parent
                    text: "fx"
                    color: backend.scientific ? win.inkColor : win.mutedColor
                    font.family: "iA Writer Mono S"
                    font.pixelSize: win.scaledSize(17)
                }

                MouseArea {
                    id: toggleArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: win.toggleScientific()
                }
            }

            Text {
                id: expressionText
                anchors.top: parent.top
                anchors.left: scientificToggle.right
                anchors.leftMargin: win.scaledSize(12)
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
                verticalAlignment: Text.AlignBottom
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
            columns: backend.scientific ? 8 : 4
            rowSpacing: win.scaledSize(12)
            columnSpacing: win.scaledSize(12)

            Repeater {
                // In scientific mode each row starts with four function keys.
                model: {
                    var basic = keypad.basicKeys;
                    if (!backend.scientific)
                        return basic;
                    var keys = [];
                    for (var row = 0; row < 5; ++row)
                        keys = keys.concat(keypad.scientificKeys.slice(row * 4, row * 4 + 4),
                                           basic.slice(row * 4, row * 4 + 4));
                    return keys;
                }

                CalcButton {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 1
                    label: modelData.key === "angle" ? (backend.degrees ? "deg" : "rad") : modelData.label
                    keyValue: modelData.key
                    kind: modelData.kind
                    accessibleName: modelData.name === undefined ? "" : modelData.name
                    iconName: modelData.icon === undefined ? "" : modelData.icon
                    pageColor: win.pageColor
                    inkColor: win.inkColor
                    onActivated: backend.pressKey(keyValue)
                }
            }

            readonly property var scientificKeys: [
                { label: "(", key: "(", kind: "function", name: qsTr("Open parenthesis") },
                { label: ")", key: ")", kind: "function", name: qsTr("Close parenthesis") },
                { label: "deg", key: "angle", kind: "function", name: qsTr("Degrees or radians") },
                { label: "1/x", key: "reciprocal", kind: "function" },
                { label: "sin", key: "sin", kind: "function" },
                { label: "cos", key: "cos", kind: "function" },
                { label: "tan", key: "tan", kind: "function" },
                { label: "x²", key: "square", kind: "function" },
                { label: "asin", key: "asin", kind: "function" },
                { label: "acos", key: "acos", kind: "function" },
                { label: "atan", key: "atan", kind: "function" },
                { label: "x³", key: "cube", kind: "function" },
                { label: "ln", key: "ln", kind: "function" },
                { label: "log", key: "log", kind: "function" },
                { label: "e^x", key: "exp", kind: "function" },
                { label: "√", key: "sqrt", kind: "function", name: qsTr("Square root") },
                { label: "π", key: "pi", kind: "function", name: "pi" },
                { label: "e", key: "e", kind: "function" },
                { label: "n!", key: "factorial", kind: "function", name: qsTr("Factorial") },
                { label: "x^y", key: "^", kind: "function", name: qsTr("Power") }
            ]

            readonly property var basicKeys: [
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
