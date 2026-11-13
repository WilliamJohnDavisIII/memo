import QtQuick
import QtQuick.Shapes

// A play triangle, or pause bars while playing.
Item {
    id: glyph
    property bool playing: false
    property color color: "white"
    implicitWidth: 14
    implicitHeight: 14

    Shape {
        anchors.fill: parent
        visible: !glyph.playing
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: glyph.color
            strokeColor: "transparent"
            startX: 2; startY: 0
            PathLine { x: 14; y: 7 }
            PathLine { x: 2; y: 14 }
            PathLine { x: 2; y: 0 }
        }
    }

    Row {
        anchors.centerIn: parent
        visible: glyph.playing
        spacing: 4
        Rectangle { width: 4; height: 14; color: glyph.color }
        Rectangle { width: 4; height: 14; color: glyph.color }
    }
}
