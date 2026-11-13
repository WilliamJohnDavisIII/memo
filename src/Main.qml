import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtMultimedia

ApplicationWindow {
    id: win
    width: 480
    height: 640
    minimumWidth: 360
    minimumHeight: 440
    visible: true
    title: "Memo"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property color dim: "#8a8a8f"
    property bool helpVisible: false
    // A question waiting on a yes: { kind, title, detail, keep, action, run }.
    property var pending: null
    readonly property bool confirming: pending !== null
    // Keeps the selection on the same memo when the list changes underneath.
    property string selectedPath: ""
    property string notice: ""
    property string playingPath: ""
    readonly property var current: list.currentIndex >= 0 && list.currentIndex < backend.memos.length
                                   ? backend.memos[list.currentIndex] : null

    function num(n) { return Number(n).toLocaleString(Qt.locale(), 'f', 0) }
    function clock(ms) {
        const s = Math.max(0, Math.floor(ms / 1000))
        const sec = s % 60
        return num(Math.floor(s / 60)) + ":" + (sec < 10 ? num(0) : "") + num(sec)
    }
    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate()
    }
    function dayOf(date) {
        // Reading translator.language re-renders dates when the language changes.
        translator.language
        const today = new Date()
        const yesterday = new Date(today.getFullYear(), today.getMonth(), today.getDate() - 1)
        if (sameDay(date, today)) return qsTr("Today")
        if (sameDay(date, yesterday)) return qsTr("Yesterday")
        return date.toLocaleDateString(Qt.locale(), Locale.LongFormat)
    }
    function timeOf(date) {
        translator.language
        return date.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
    }

    function togglePlay(memo) {
        if (!memo || backend.recording) return
        if (playingPath === memo.path) {
            player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
            return
        }
        playingPath = memo.path
        player.source = memo.url
        player.play()
    }
    function stopPlayback() {
        player.stop()
        player.source = ""
        playingPath = ""
    }
    function seek(ms) {
        if (playingPath !== "")
            player.position = Math.max(0, Math.min(player.duration, player.position + ms))
    }
    function describe(memo) { return dayOf(memo.created) + ", " + timeOf(memo.created) }
    function askTrash() {
        if (!current || backend.recording) return
        const memo = current
        pending = {
            kind: "trash",
            title: qsTr("Move this memo to the trash?"),
            detail: describe(memo) + (memo.transcript ? "\n" + qsTr("Its transcript goes too.") : ""),
            keep: qsTr("Keep"),
            action: qsTr("Move to trash"),
            run: () => {
                if (playingPath === memo.path) stopPlayback()
                const index = list.currentIndex
                backend.remove(memo.path)
                list.currentIndex = Math.min(index, backend.memos.length - 1)
            }
        }
    }
    function confirmPending() {
        const question = pending
        pending = null
        if (question) question.run()
    }
    function transcribeCurrent() {
        if (current && !current.transcript && !current.transcribing) backend.transcribe(current.path)
    }
    function copyTranscript() {
        if (!current || !current.transcript) return
        backend.copyText(current.transcript)
        notice = qsTr("Transcript copied")
        noticeTimer.restart()
    }
    function move(step) {
        if (backend.memos.length === 0) return
        list.currentIndex = Math.max(0, Math.min(backend.memos.length - 1, list.currentIndex + step))
    }

    Timer { id: noticeTimer; interval: 1600; onTriggered: win.notice = "" }

    MediaPlayer {
        id: player
        audioOutput: AudioOutput {}
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.EndOfMedia) win.playingPath = ""
    }

    Connections {
        target: backend
        function onRecordingChanged() {
            if (backend.recording && player.playbackState === MediaPlayer.PlayingState) player.pause()
        }
        function onMemosChanged() {
            const memos = backend.memos
            for (let i = 0; i < memos.length; ++i) {
                if (memos[i].path === win.selectedPath) {
                    list.currentIndex = i
                    return
                }
            }
            if (list.currentIndex < 0 && memos.length > 0) list.currentIndex = 0
        }
        // Cloud mode uploads the audio, so it asks before sending a memo off.
        function onCloudConsentNeeded(path) {
            win.pending = {
                kind: "upload",
                title: qsTr("Upload this memo to transcribe it?"),
                detail: qsTr("Superwhisper is in Cloud mode, so the audio goes to its servers. Switch it to an offline mode to keep memos on this computer."),
                keep: qsTr("Cancel"),
                action: qsTr("Upload"),
                run: () => backend.transcribe(path, true)
            }
        }
    }

    // Recording is never thrown away: closing mid-memo saves it, then quits.
    onClosing: (close) => {
        if (backend.recording || backend.saving) {
            close.accepted = false
            backend.stopThenQuit()
        }
    }

    Shortcut { sequence: "Space"; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: { win.helpVisible = false; backend.toggleRecording() } }
    Shortcut { sequences: ["Return", "Enter"]; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: win.togglePlay(win.current) }
    Shortcut { sequences: ["Down", "J"]; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: win.move(1) }
    Shortcut { sequences: ["Up", "K"]; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: win.move(-1) }
    Shortcut { sequences: ["Right", "L"]; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: win.seek(5000) }
    Shortcut { sequences: ["Left", "H"]; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: win.seek(-5000) }
    Shortcut { sequences: ["Delete", "Backspace", "D"]; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: win.askTrash() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", StandardKey.Redo]; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: backend.redo() }
    Shortcut { sequence: "O"; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: Qt.openUrlExternally("file://" + encodeURI(backend.directory)) }
    Shortcut { sequence: "?"; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: { win.pending = null; win.helpVisible = false; backend.clearError() } }
    Shortcut { sequences: ["Return", "Enter", "Y"]; enabled: win.confirming; context: Qt.ApplicationShortcut; onActivated: win.confirmPending() }
    // D twice deletes: the same key that asked also confirms.
    Shortcut { sequences: ["D", "Delete", "Backspace"]; enabled: win.confirming && win.pending.kind === "trash"; context: Qt.ApplicationShortcut; onActivated: win.confirmPending() }
    Shortcut { sequence: "N"; enabled: win.confirming; context: Qt.ApplicationShortcut; onActivated: win.pending = null }
    Shortcut { sequence: "T"; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: win.transcribeCurrent() }
    Shortcut { sequence: "C"; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: win.copyTranscript() }
    Shortcut { sequence: "Q"; enabled: !win.confirming; context: Qt.ApplicationShortcut; onActivated: win.close() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            // The one button: a dot to record, a square to stop.
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 262

                Rectangle {
                    id: halo
                    anchors.centerIn: ring
                    width: ring.width
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: theme.accent
                    opacity: 0
                    visible: backend.recording

                    ParallelAnimation {
                        running: backend.recording
                        loops: Animation.Infinite
                        NumberAnimation { target: halo; property: "width"; from: ring.width; to: ring.width + 44; duration: 1400; easing.type: Easing.OutCubic }
                        NumberAnimation { target: halo; property: "opacity"; from: 0.7; to: 0; duration: 1400; easing.type: Easing.OutCubic }
                    }
                }

                Rectangle {
                    id: ring
                    width: 128
                    height: width
                    radius: width / 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 20
                    color: ringMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                    border.width: 3
                    border.color: theme.accent
                    opacity: backend.saving ? 0.5 : 1

                    Rectangle {
                        anchors.centerIn: parent
                        width: backend.recording || backend.saving ? 44 : 96
                        height: width
                        radius: backend.recording || backend.saving ? Math.min(4, theme.radius) : width / 2
                        color: theme.accent
                        Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        Behavior on radius { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                    }

                    MouseArea {
                        id: ringMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.toggleRecording()
                    }
                }

                // The voice, live: newest on the right, a flat line while idle.
                Row {
                    id: wave
                    anchors.top: ring.bottom
                    anchors.topMargin: 22
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: 44
                    spacing: 3
                    Repeater {
                        model: backend.levels.length
                        Rectangle {
                            required property int index
                            anchors.verticalCenter: parent.verticalCenter
                            width: 3
                            height: Math.max(3, 44 * backend.levels[index])
                            radius: Math.min(1.5, theme.radius)
                            color: backend.recording ? theme.accent : Qt.rgba(1, 1, 1, 0.12)
                        }
                    }
                }

                Label {
                    anchors.top: wave.bottom
                    anchors.topMargin: 14
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: backend.saving ? qsTr("Saving…")
                        : backend.recording ? win.clock(backend.elapsed)
                        : qsTr("Space to record")
                    font.pixelSize: backend.recording ? 30 : 15
                    font.weight: backend.recording ? Font.Light : Font.Normal
                    color: backend.recording ? "white" : win.dim
                }
            }

            Label {
                Layout.fillWidth: true
                visible: backend.error !== ""
                text: backend.error
                color: "#ff6b6b"
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: backend.memos
                currentIndex: backend.memos.length > 0 ? 0 : -1
                keyNavigationEnabled: false
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                onCurrentIndexChanged: win.selectedPath = currentIndex >= 0 && currentIndex < backend.memos.length
                                       ? backend.memos[currentIndex].path : ""
                spacing: 2
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: (row.modelData.transcript || row.modelData.transcribing) ? 78 : 58

                    readonly property bool selected: ListView.isCurrentItem
                    readonly property bool active: win.playingPath === modelData.path

                    // Drag a memo straight into a chat, an email, or a file manager.
                    Drag.active: dragHandler.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.mimeData: { "text/uri-list": row.modelData.url.toString() }
                    DragHandler { id: dragHandler; target: null }

                    Rectangle {
                        anchors.fill: parent
                        radius: Math.min(8, theme.radius)
                        color: row.selected ? Qt.rgba(1, 1, 1, 0.07)
                             : rowMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.035) : "transparent"
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        height: 2
                        width: player.duration > 0 ? parent.width * player.position / player.duration : 0
                        visible: row.active
                        color: theme.accent
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 14

                        PlayGlyph {
                            playing: row.active && player.playbackState === MediaPlayer.PlayingState
                            color: row.selected || row.active ? theme.accent : win.dim
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                text: win.dayOf(row.modelData.created)
                                font.pixelSize: 15
                                color: "white"
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: win.timeOf(row.modelData.created)
                                font.pixelSize: 12
                                color: win.dim
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: row.modelData.transcript !== "" || row.modelData.transcribing
                                text: row.modelData.transcribing ? qsTr("Transcribing…")
                                                                 : row.modelData.transcript.replace(/\s+/g, " ")
                                font.pixelSize: 13
                                font.italic: row.modelData.transcribing
                                color: row.modelData.transcribing ? theme.accent : Qt.rgba(1, 1, 1, 0.7)
                                elide: Text.ElideRight
                                maximumLineCount: 1
                            }
                        }

                        Label {
                            text: row.active ? win.clock(player.position) + " / " + win.clock(player.duration)
                                : row.modelData.duration >= 0 ? win.clock(row.modelData.duration) : ""
                            font.pixelSize: 13
                            font.features: { "tnum": 1 }
                            color: row.active ? "white" : win.dim
                        }
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: list.currentIndex = row.index
                        onDoubleClicked: win.togglePlay(row.modelData)
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: qsTr("No memos yet")
                    color: win.dim
                    font.pixelSize: 15
                }
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: win.notice !== "" ? win.notice
                    : qsTr("%n memos", "", backend.memos.length) + "   ·   " + qsTr("? for shortcuts")
                color: Qt.rgba(1, 1, 1, 0.3)
                font.pixelSize: 12
            }
        }

        // Asks before anything can't be taken back quietly: a memo going to the
        // trash, or audio leaving the computer. Enter confirms, Escape doesn't.
        Rectangle {
            anchors.fill: parent
            visible: win.confirming
            color: Qt.rgba(0, 0, 0, 0.75)

            MouseArea { anchors.fill: parent; onClicked: win.pending = null }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 340)
                height: confirmColumn.implicitHeight + 48
                radius: theme.radius
                color: "#18181b"
                border.color: Qt.rgba(1, 1, 1, 0.08)

                MouseArea { anchors.fill: parent }

                ColumnLayout {
                    id: confirmColumn
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 6

                    Label {
                        Layout.fillWidth: true
                        text: win.pending ? win.pending.title : ""
                        color: "white"
                        font.pixelSize: 16
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: win.pending ? win.pending.detail : ""
                        color: win.dim
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        spacing: 8
                        Item { Layout.fillWidth: true }
                        Button {
                            text: (win.pending ? win.pending.keep : "") + "  (Esc)"
                            flat: true
                            focusPolicy: Qt.NoFocus
                            onClicked: win.pending = null
                        }
                        Button {
                            text: (win.pending ? win.pending.action : "") + "  (Enter)"
                            focusPolicy: Qt.NoFocus
                            onClicked: win.confirmPending()
                            contentItem: Label {
                                text: parent.text
                                color: theme.accentForeground
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                implicitHeight: 36
                                radius: Math.min(4, theme.radius)
                                color: theme.accent
                                opacity: parent.down ? 0.8 : 1
                            }
                        }
                    }
                }
            }
        }

        // Every shortcut, toggled with ?.
        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: Qt.rgba(0, 0, 0, 0.75)

            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, helpGrid.implicitWidth + 56)
                height: helpGrid.implicitHeight + 56
                radius: theme.radius
                color: "#18181b"
                border.color: Qt.rgba(1, 1, 1, 0.08)

                GridLayout {
                    id: helpGrid
                    anchors.centerIn: parent
                    columns: 2
                    columnSpacing: 28
                    rowSpacing: 10

                    Repeater {
                        model: [
                            ["Space", qsTr("Record / stop")],
                            ["Enter", qsTr("Play / pause")],
                            ["↑ ↓  J K", qsTr("Select")],
                            ["← →  H L", qsTr("Skip 5 seconds")],
                            ["T", qsTr("Transcribe")],
                            ["C", qsTr("Copy transcript")],
                            ["D  Del", qsTr("Move to trash")],
                            ["Ctrl+Z", qsTr("Undo")],
                            ["Ctrl+Shift+Z", qsTr("Redo")],
                            ["O", qsTr("Open folder")],
                            ["?", qsTr("Shortcuts")],
                            ["Q", qsTr("Quit")]
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.columnSpan: 2
                            spacing: 28
                            Label {
                                Layout.preferredWidth: 110
                                text: modelData[0]
                                color: theme.accent
                                font.family: "monospace"
                                font.pixelSize: 13
                            }
                            Label {
                                text: modelData[1]
                                color: "white"
                                font.pixelSize: 14
                            }
                        }
                    }
                }
            }
        }
    }
}
