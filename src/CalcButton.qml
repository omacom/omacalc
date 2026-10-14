import QtQuick

// One keypad button. Numbers sit almost flush with the page, the operator
// column lifts a step lighter, and equals inverts to the ink color.
Rectangle {
    id: control

    property string label
    property string keyValue: label
    property string kind: "number" // "number", "operator" or "equals"
    property string iconName
    property color pageColor: "#101010"
    property color inkColor: "#eeeeee"

    signal activated()

    Accessible.role: Accessible.Button
    Accessible.name: {
        if (iconName === "backspace") return "Backspace";
        if (label === "AC") return "All clear";
        if (label === "±") return "Toggle sign";
        if (label === "%") return "Percent";
        if (label === "÷") return "Divide";
        if (label === "×") return "Multiply";
        if (label === "−") return "Subtract";
        if (label === "+") return "Add";
        if (label === "=") return "Equals";
        return label;
    }
    Accessible.onPressAction: activated()

    function mixColors(base, tint, amount) {
        return Qt.rgba(
            base.r + (tint.r - base.r) * amount,
            base.g + (tint.g - base.g) * amount,
            base.b + (tint.b - base.b) * amount, 1);
    }

    readonly property real restingLift: kind === "operator" ? 0.16 : 0.05
    readonly property real activeLift: restingLift
        + (hitArea.pressed ? 0.09 : (hitArea.containsMouse ? 0.045 : 0))

    radius: Math.min(14, height * 0.18)
    color: kind === "equals"
        ? mixColors(inkColor, pageColor, hitArea.pressed ? 0.22 : (hitArea.containsMouse ? 0.1 : 0))
        : mixColors(pageColor, inkColor, activeLift)
    border.width: kind === "number" ? 1 : 0
    border.color: mixColors(pageColor, inkColor, 0.13)

    Text {
        anchors.centerIn: parent
        visible: control.iconName === ""
        text: control.label
        color: control.kind === "equals" ? control.pageColor : control.inkColor
        font.family: "iA Writer Mono S"
        font.pixelSize: Math.round(Math.min(parent.height * 0.42, parent.width * 0.3))
    }

    Canvas {
        id: backspaceIcon
        anchors.centerIn: parent
        width: Math.round(Math.min(parent.height * 0.42, parent.width * 0.3) * 1.3)
        height: Math.round(width * 0.72)
        visible: control.iconName === "backspace"

        onPaint: {
            var context = getContext("2d");
            var w = width;
            var h = height;
            var notch = w * 0.28;
            context.clearRect(0, 0, w, h);
            context.strokeStyle = String(control.inkColor);
            context.lineWidth = Math.max(1.4, w * 0.07);
            context.lineCap = "round";
            context.lineJoin = "round";

            // The key cap: a rectangle whose left edge tapers to a point.
            context.beginPath();
            context.moveTo(notch, 1);
            context.lineTo(w - 1, 1);
            context.lineTo(w - 1, h - 1);
            context.lineTo(notch, h - 1);
            context.lineTo(1, h / 2);
            context.closePath();
            context.stroke();

            // The x inside, nudged left of the body's center so the tapered
            // tip doesn't make it read as off-center.
            var cx = notch + (w - notch) * 0.435;
            var cy = h / 2;
            var arm = h * 0.18;
            context.beginPath();
            context.moveTo(cx - arm, cy - arm);
            context.lineTo(cx + arm, cy + arm);
            context.moveTo(cx + arm, cy - arm);
            context.lineTo(cx - arm, cy + arm);
            context.stroke();
        }

        Connections {
            target: control
            function onInkColorChanged() { backspaceIcon.requestPaint(); }
            function onWidthChanged() { backspaceIcon.requestPaint(); }
            function onHeightChanged() { backspaceIcon.requestPaint(); }
        }
    }

    MouseArea {
        id: hitArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: control.activated()
    }
}
