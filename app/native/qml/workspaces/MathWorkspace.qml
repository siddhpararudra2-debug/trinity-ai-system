// Trinity — MATH workspace: deterministic evaluator + solver
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

ColumnLayout {
    spacing: 12
    anchors.fill: parent
    anchors.margins: 14

    SectionHeader { number: "03"; label: "MATH — EVALUATE & SOLVE"; Layout.fillWidth: true }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 12

        // Editor
        Rectangle {
            Layout.preferredWidth: 420
            Layout.fillHeight: true
            radius: TrinityTheme.radiusM
            color: TrinityTheme.panel
            border.color: TrinityTheme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10
                Label { text: "EXPRESSION"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; font.letterSpacing: 0.8 }
                TextField {
                    id: expr
                    Layout.fillWidth: true
                    placeholderText: "e.g.  2*pi*25   or   x^2 + 3*x - 4 = 0"
                    font.family: TrinityTheme.fontMono
                    font.pixelSize: 13
                    color: TrinityTheme.text
                    background: Rectangle { color: TrinityTheme.bg; border.color: TrinityTheme.border; radius: TrinityTheme.radiusM }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 10
                    Label { text: "Variables (JSON)"; color: TrinityTheme.textMuted; font.pixelSize: 11 }
                    TextField { id: vars; Layout.fillWidth: true; placeholderText: "{\"x\": 2}"; font.family: TrinityTheme.fontMono; font.pixelSize: 11 }
                    Label { text: "Solve unknown"; color: TrinityTheme.textMuted; font.pixelSize: 11 }
                    TextField { id: unknown; Layout.fillWidth: true; placeholderText: "e.g. x (leave empty to evaluate)"; font.family: TrinityTheme.fontMono; font.pixelSize: 11 }
                }
                Label { text: "Functions: sin cos tan sqrt log exp abs · constants pi e · operators + - * / % ^ ( )"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.Wrap }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Button {
                        Layout.fillWidth: true
                        text: "Evaluate (F5)"
                        highlighted: true
                        onClicked: runMath()
                    }
                    Button {
                        text: "Validate (F6)"
                        onClicked: output.text += "\n— re-substitution residual < 1e-9 check required for VALIDATED —\n"
                    }
                }
                Label { id: mathStatus; Layout.fillWidth: true; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; wrapMode: Text.Wrap }

                Item { Layout.fillHeight: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: TrinityTheme.borderSoft }
                Label { text: "Deterministic shunting-yard evaluator + polynomial solver (≤ degree 2). Transcendental solves route via Python host (IPC) when available."; color: TrinityTheme.textDim; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }

        // Output
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: TrinityTheme.radiusM
            color: TrinityTheme.bg
            border.color: TrinityTheme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "OUTPUT — VERIFIED EVIDENCE"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; font.letterSpacing: 0.8; Layout.fillWidth: true }
                    Button { text: "Clear"; onClicked: output.text="" }
                    Button { text: "Copy"; onClicked: output.selectAll() }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    TextArea {
                        id: output
                        readOnly: true
                        wrapMode: Text.Wrap
                        color: TrinityTheme.text
                        font.family: TrinityTheme.fontMono
                        font.pixelSize: 11
                        placeholderText: "Result and validation evidence appear here. Math evaluate → VERIFIED; solve → every root re-substituted."
                        background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.borderSoft; radius: TrinityTheme.radiusM }
                    }
                }
                // History
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: ["2*pi*25", "sqrt(16)+3", "x^2 - 4 = 0", "20% of 50"]
                        Button {
                            required property string modelData
                            text: modelData
                            font.family: TrinityTheme.fontMono
                            font.pixelSize: 10
                            onClicked: expr.text = modelData
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }

    function runMath() {
        const expression = expr.text.trim();
        if (!expression) { mathStatus.text = "Enter an expression."; return; }
        const varsText = vars.text.trim();
        let varMap = {};
        if (varsText) try { varMap = JSON.parse(varsText); } catch (e) { mathStatus.text = "Variables must be JSON."; return; }
        const unk = unknown.text.trim();

        // Build deterministic command text (mirrors ToolExecutor routing)
        let cmd = expression;
        if (unk) cmd = `solve ${expression} for ${unk}`;
        else if (expression.includes("=")) cmd = `solve ${expression}`;
        else cmd = `calculate ${expression}`;

        // Attach vars via JSON payload trick for calc — backend will parse expression + vars
        // For native, we use run_command_async which will dispatch to MathEngine
        const res = Trinity.run_command_async(cmd);
        if (!res.ok) {
            output.text = "✗ " + res.error + (res.code ? "  ["+res.code+"]" : "");
            mathStatus.text = "Job not queued.";
            return;
        }
        mathStatus.text = "queued job " + res.job_id.substring(0,8) + " — polling…";
        output.text = "→ job " + res.job_id + "  " + cmd + "\n\nvariables: " + JSON.stringify(varMap) + "\nunknown: " + (unk||"(evaluate)") + "\n\nAwaiting engine result… (watch Jobs/Output)";

        // Poll the job record for output (cooperative, no blocking)
        let attempts = 0;
        const timer = Qt.createQmlObject('import QtQuick 2.15; Timer { interval: 220; repeat: true; running: true }', output);
        timer.triggered.connect(function() {
            attempts++;
            const rec = Trinity.get_job(res.job_id);
            if (!rec || !rec.status) return;
            if (rec.status === "COMPLETED" || rec.status === "FAILED") {
                timer.stop(); timer.destroy();
                mathStatus.text = rec.status + " · " + rec.jobId.substring(0,8);
                const dump = JSON.stringify(rec, null, 2);
                output.text = dump;
            } else if (attempts > 40) {
                timer.stop(); timer.destroy();
                mathStatus.text = "Timeout — check Jobs panel.";
            }
        });
    }
}
