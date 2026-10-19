'use client';

import { useState } from 'react';
import {
  executeEngine,
  getArtifactUrl,
  uploadImage,
  TrinityApiError,
  type ArtifactOut,
  type TrinityResult,
} from '@/lib/api/trinity';

/**
 * Interactive consoles for the bridged native engines. Each panel follows the
 * workspace pattern: form -> POST /api/execute -> TrinityResult rendered as
 * spec | result | validation | artifacts. When the native backend is not
 * connected the API answers success:false with a capability_unavailable
 * error, which is shown truthfully.
 *
 * Every panel below was built against the live engine, not its docstring:
 *   research/search        -> hits, hit_count
 *   simulation/simulate    -> result.result.{method,step_count,final_state}
 *   pcb/generate|add_net|validate|export -> design, rules, kicad_pcb
 *   robotics/kinematics    -> end_effector.{position,rotation}, frames
 *   robotics/trajectory    -> goal_reached, sample_count, trajectory[]
 *   firmware/create..build -> project, threaded step to step
 *   vision/image_inspect   -> upload an artifact, then read
 *                             input.metadata.{width,height,format,channels}
 *
 * Where an operation exists natively but is not reachable through the
 * bridge, the panel says so instead of shipping a button that always fails.
 */

type PanelState = {
  busy: boolean;
  result: TrinityResult | null;
  error: string | null;
};

const idle: PanelState = { busy: false, result: null, error: null };

function errorText(e: unknown): string {
  if (e instanceof TrinityApiError) {
    const hints = [...e.suggestions, ...e.examples].slice(0, 3);
    return hints.length > 0 ? `${e.message} — try: ${hints.join(', ')}` : e.message;
  }
  return e instanceof Error ? e.message : 'Request failed';
}

/** "0.5, -0.3" -> [0.5, -0.3]. Returns null when anything is not a finite number,
 *  so the panel can say which field is wrong instead of sending NaN. */
function parseNumbers(raw: string): number[] | null {
  const parts = raw.split(/[,\s]+/).filter(Boolean);
  const values = parts.map(Number);
  return values.length > 0 && values.every(Number.isFinite) ? values : null;
}

function Spec({ rows }: { rows: Array<[string, unknown]> }) {
  return (
    <dl className="ws-spec">
      {rows.map(([k, v]) => (
        <div key={k}>
          <dt>{k}</dt>
          <dd>{typeof v === 'number' ? Number(v.toFixed(4)) : String(v ?? '—')}</dd>
        </div>
      ))}
    </dl>
  );
}

function Field({ label, value, onChange, hint }: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  hint?: string;
}) {
  return (
    <label className="ws-field">
      <span>{label}</span>
      <input
        className="ws-input"
        value={value}
        placeholder={hint}
        onChange={(e) => onChange(e.target.value)}
      />
    </label>
  );
}

function ResultHead({ result }: { result: TrinityResult }) {
  return (
    <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap', alignItems: 'center' }}>
      <span className={`status-pill ${result.success ? 'is-live' : 'is-failed'}`}>
        {result.success ? 'SUCCESS' : 'FAILED'}
      </span>
      {result.validation && (
        <span className="status-pill">{result.validation.status}</span>
      )}
      <span className="mono" style={{ fontSize: '0.62rem', color: 'var(--trinity-gray)' }}>
        JOB {result.job_id.slice(0, 8)}
      </span>
    </div>
  );
}

function Artifacts({ result }: { result: TrinityResult }) {
  if (result.artifacts.length === 0) return null;
  return (
    <ul className="ws-checks">
      {result.artifacts.map((a) => (
        <li key={a.artifact_id}>
          <b>▸</b>
          <span>
            {a.type.toUpperCase()} · {(a.size_bytes / 1024).toFixed(1)} KB · {a.checksum.slice(0, 12)}
            {' · '}
            <a href={getArtifactUrl(a.artifact_id)} target="_blank" rel="noopener noreferrer">
              DOWNLOAD
            </a>
          </span>
        </li>
      ))}
    </ul>
  );
}

function ResearchPanel() {
  const [query, setQuery] = useState('indexes documents');
  const [state, setState] = useState<PanelState>(idle);

  async function run() {
    if (!query.trim() || state.busy) return;
    setState({ busy: true, result: null, error: null });
    try {
      const result = await executeEngine('research', 'search', { query: query.trim() });
      setState({ busy: false, result, error: result.success ? null : (result.errors[0]?.message ?? 'Search failed') });
    } catch (e) {
      setState({ busy: false, result: null, error: errorText(e) });
    }
  }

  const hits = (state.result?.result.hits ?? []) as Array<Record<string, unknown>>;
  const hitCount = state.result?.result.hit_count;

  return (
    <article className="ws-panel">
      <h2>RESEARCH · LOCAL INDEX</h2>
      <p className="ws-empty">Deterministic BM25-lite search over the natively indexed documents.</p>
      <div className="ws-form" style={{ marginTop: 14 }}>
        <Field label="QUERY" value={query} onChange={setQuery} hint="indexes documents" />
        <div>
          <button className="btn-generate" onClick={run} disabled={state.busy} type="button">
            {state.busy ? 'SEARCHING…' : 'SEARCH →'}
          </button>
        </div>
      </div>
      {state.error && <p className="ws-error">{state.error}</p>}
      {state.result?.success && (
        <div style={{ marginTop: 16, display: 'grid', gap: 12 }}>
          <ResultHead result={state.result} />
          <p className="mono" style={{ fontSize: '0.7rem', color: 'var(--trinity-gray)' }}>
            {typeof hitCount === 'number' ? hitCount : 0} HIT{(hitCount === 1 ? '' : 'S')}
          </p>
          <ul className="ws-checks">
            {hits.map((hit, i) => (
              <li key={String(hit.doc_id ?? i)}>
                <b>▸</b>
                <span>
                  <strong>{String(hit.title ?? hit.doc_id ?? 'untitled')}</strong>
                  {' · '}score {typeof hit.score === 'number' ? hit.score.toFixed(3) : String(hit.score ?? '—')}
                  {typeof hit.snippet === 'string' && hit.snippet ? (
                    <><br />{hit.snippet}</>
                  ) : null}
                </span>
              </li>
            ))}
          </ul>
          {hits.length === 0 && (
            <p className="ws-empty">Empty index — the native side has nothing indexed under this backend yet.</p>
          )}
        </div>
      )}
    </article>
  );
}

function SimulationPanel() {
  const [duration, setDuration] = useState('1.0');
  const [velocity, setVelocity] = useState('2.0');
  const [accel, setAccel] = useState('1.0');
  const [state, setState] = useState<PanelState>(idle);

  async function run() {
    if (state.busy) return;
    setState({ busy: true, result: null, error: null });
    try {
      const result = await executeEngine('simulation', 'simulate', {
        duration_s: Number(duration),
        initial_velocity_m_s: Number(velocity),
        acceleration_m_s2: Number(accel),
      });
      setState({ busy: false, result, error: result.success ? null : (result.errors[0]?.message ?? 'Simulation failed') });
    } catch (e) {
      setState({ busy: false, result: null, error: errorText(e) });
    }
  }

  const summary = state.result?.result.result as Record<string, unknown> | undefined;
  const finalState = (summary?.final_state ?? {}) as Record<string, unknown>;
  const finalPos = (finalState.position ?? {}) as Record<string, unknown>;

  return (
    <article className="ws-panel">
      <h2>SIMULATION · LINEAR MOTION</h2>
      <p className="ws-empty">Deterministic closed-form integration through the native engine. CSV + JSON artifacts.</p>
      <div className="ws-form" style={{ marginTop: 14 }}>
        <Field label="DURATION_S" value={duration} onChange={setDuration} hint="1.0" />
        <Field label="INITIAL_VELOCITY_M_S" value={velocity} onChange={setVelocity} hint="2.0" />
        <Field label="ACCELERATION_M_S2" value={accel} onChange={setAccel} hint="1.0" />
        <div>
          <button className="btn-generate" onClick={run} disabled={state.busy} type="button">
            {state.busy ? 'RUNNING…' : 'SIMULATE →'}
          </button>
        </div>
      </div>
      {state.error && <p className="ws-error">{state.error}</p>}
      {state.result?.success && summary && (
        <div style={{ marginTop: 16, display: 'grid', gap: 12 }}>
          <ResultHead result={state.result} />
          <dl className="ws-spec">
            {([
              ['METHOD', summary.method],
              ['STEPS', summary.step_count],
              ['DT_S', summary.dt],
              ['DURATION_S', summary.duration_s],
              ['FINAL_X_M', finalPos.x],
            ] as Array<[string, unknown]>).map(([k, v]) => (
              <div key={k}>
                <dt>{k}</dt>
                <dd>{String(v ?? '—')}</dd>
              </div>
            ))}
          </dl>
          <Artifacts result={state.result} />
        </div>
      )}
    </article>
  );
}

function PcbPanel() {
  const [width, setWidth] = useState('50');
  const [height, setHeight] = useState('40');
  const [preset, setPreset] = useState('esp32');
  const [projectName, setProjectName] = useState('trinity_board');
  const [nets, setNets] = useState<string[]>([]);
  const [state, setState] = useState<PanelState>(idle);
  // Kept separately so VALIDATE and EXPORT stay available after a failing check.
  const [design, setDesign] = useState<Record<string, unknown> | null>(null);
  // Tracked separately from `result`, which every later call replaces. Reading
  // "exported" off the current result made both the badge AND the download
  // link vanish the moment you ran VALIDATE, even though the file was still
  // on disk and still downloadable.
  const [kicad, setKicad] = useState<{ type: string; sizeBytes: number; url: string } | null>(null);

  const PRESETS: Record<string, Array<Record<string, string>>> = {
    none: [],
    esp32: [{ part: 'esp32' }],
    'esp32+regulator': [{ part: 'esp32' }, { part: 'regulator' }],
  };

  async function generate() {
    if (state.busy) return;
    setState({ busy: true, result: null, error: null });
    setNets([]);
    setKicad(null);
    try {
      const result = await executeEngine('pcb', 'generate', {
        width_mm: Number(width),
        height_mm: Number(height),
        components: PRESETS[preset] ?? [],
      });
      const next = (result.result as Record<string, unknown> | undefined)?.design;
      const initial = typeof next === 'object' && next !== null ? (next as Record<string, unknown>) : null;
      setDesign(initial);
      if (!result.success || !initial) {
        setState({ busy: false, result, error: result.errors[0]?.message ?? 'Board generation failed' });
        return;
      }
      // create_board leaves every power pin on no net, which its own DRC then
      // rejects — so a board with components could never be exported. Wire
      // each distinct power-pin name into its own net here, and show what was
      // created rather than doing it silently.
      const netted = await autoNet(initial);
      setDesign(netted.design);
      setNets(netted.nets);
      setState({
        busy: false,
        result,
        error: netted.error
          ? `Board generated, but automatic net wiring failed: ${netted.error}`
          : null,
      });
    } catch (e) {
      setState({ busy: false, result: null, error: errorText(e) });
    }
  }

  /** One net per distinct power-pin name. Returns the design unchanged when
   *  there is nothing to wire — a bare board must not gain phantom nets.
   *  On failure, retain the last usable design and explain which net failed. */
  async function autoNet(board: Record<string, unknown>): Promise<{
    design: Record<string, unknown>;
    nets: string[];
    error: string | null;
  }> {
    const parts = (board.components ?? []) as Array<Record<string, unknown>>;
    const groups = new Map<string, string[]>();
    for (const part of parts) {
      const ref = String(part.ref ?? '');
      for (const pin of (part.pins ?? []) as Array<Record<string, unknown>>) {
        if (pin.electrical !== 'power_in' && pin.electrical !== 'power_out') continue;
        const name = String(pin.name ?? '');
        if (!name) continue;
        const list = groups.get(name) ?? [];
        list.push(`${ref}.${String(pin.number ?? '')}`);
        groups.set(name, list);
      }
    }
    if (groups.size === 0) return { design: board, nets: [], error: null };

    let current = board;
    const created: string[] = [];
    for (const [name, pins] of groups) {
      const response = await executeEngine('pcb', 'add_net', {
        design: current,
        name,
        pins,
      });
      if (!response.success) {
        return {
          design: current,
          nets: created,
          error: response.errors[0]?.message ?? `Could not add net ${name}`,
        };
      }
      const next = (response.result as Record<string, unknown> | undefined)?.design;
      if (typeof next !== 'object' || next === null) {
        return {
          design: current,
          nets: created,
          error: `Native backend returned no design after adding net ${name}`,
        };
      }
      current = next as Record<string, unknown>;
      created.push(`${name} (${pins.length})`);
    }
    return { design: current, nets: created, error: null };
  }

  async function validate() {
    if (!design || state.busy) return;
    setState({ busy: true, result: state.result, error: null });
    try {
      const result = await executeEngine('pcb', 'validate', { design });
      // A failed check still returns the rule list — surface it, do not swallow it.
      setState({ busy: false, result, error: null });
    } catch (e) {
      setState({ busy: false, result: state.result, error: errorText(e) });
    }
  }

  async function exportKicad() {
    if (!design || state.busy) return;
    setState({ busy: true, result: state.result, error: null });
    try {
      const result = await executeEngine('pcb', 'export', { design, project: projectName.trim() });
      if (result.success) {
        const file = result.artifacts.find((a) => a.type === 'kicad_pcb');
        setKicad(
          file
            ? {
                type: file.type,
                sizeBytes: file.size_bytes,
                url: getArtifactUrl(file.artifact_id),
              }
            : null,
        );
      }
      setState({
        busy: false,
        result,
        error: result.success
          ? null
          : (result.errors[0]?.message ?? 'Export refused'),
      });
    } catch (e) {
      setState({ busy: false, result: state.result, error: errorText(e) });
    }
  }

  const board = (state.result?.result ?? {}) as Record<string, unknown>;
  const components = (design?.components ?? []) as Array<Record<string, unknown>>;
  const rules = (board.rules ?? []) as Array<Record<string, unknown>>;
  const failed = rules.filter((r) => r.passed === false);
  const ranValidate = typeof board.passed === 'boolean';
  const componentCount =
    typeof board.component_count === 'number' ? board.component_count : components.length;

  return (
    <article className="ws-panel">
      <h2>PCB · BOARD + VALIDATE + EXPORT</h2>
      <p className="ws-empty">
        Create a board through the native PCB engine, wire its power pins into nets, run the
        design-rule check, then export a real KiCad board file.
      </p>
      <div className="ws-form" style={{ marginTop: 14 }}>
        <Field label="WIDTH_MM" value={width} onChange={setWidth} hint="50" />
        <Field label="HEIGHT_MM" value={height} onChange={setHeight} hint="40" />
        <label className="ws-field">
          <span>COMPONENTS</span>
          <select className="ws-input" value={preset} onChange={(e) => setPreset(e.target.value)}>
            <option value="none">bare board</option>
            <option value="esp32">ESP32-WROOM-32</option>
            <option value="esp32+regulator">ESP32 + SOT-223 regulator</option>
          </select>
        </label>
        <Field label="PROJECT_NAME" value={projectName} onChange={setProjectName} hint="trinity_board" />
        <div style={{ display: 'flex', gap: 10, flexWrap: 'wrap' }}>
          <button className="btn-generate" onClick={generate} disabled={state.busy} type="button">
            {state.busy ? 'WORKING…' : 'GENERATE →'}
          </button>
          <button
            className="btn-generate"
            onClick={validate}
            disabled={state.busy || !design}
            type="button"
            title="Validate the generated board"
          >
            VALIDATE →
          </button>
          <button
            className="btn-generate"
            onClick={exportKicad}
            disabled={state.busy || !design}
            type="button"
            title="Export a KiCad .kicad_pcb file. The engine refuses to export a board that fails its own checks."
          >
            EXPORT KICAD →
          </button>
        </div>
      </div>
      {state.error && <p className="ws-error">{state.error}</p>}
      {state.result && (
        <div style={{ marginTop: 16, display: 'grid', gap: 12 }}>
          <ResultHead result={state.result} />
          <p className="mono" style={{ fontSize: '0.7rem', color: 'var(--trinity-gray)' }}>
            {componentCount} COMPONENTS
            {ranValidate
              ? ` · ${board.passed ? `ALL ${rules.length} RULES PASSED` : `${failed.length} OF ${rules.length} RULES FAILED`}`
              : ''}
            {kicad ? ` · KICAD EXPORTED (${(kicad.sizeBytes / 1024).toFixed(1)} KB)` : ''}
          </p>
          {/* Nets wired automatically after generation — shown, not hidden. */}
          {nets.length > 0 && (
            <ul className="ws-checks">
              {nets.map((net, i) => (
                <li key={i}>
                  <b>▸</b>
                  <span>NET {net}</span>
                </li>
              ))}
            </ul>
          )}
          {/* The exported file, pinned independently of the current result. */}
          {kicad && (
            <ul className="ws-checks">
              <li>
                <b>▸</b>
                <span>
                  {kicad.type.toUpperCase()} · {(kicad.sizeBytes / 1024).toFixed(1)} KB ·{' '}
                  <a href={kicad.url} target="_blank" rel="noopener noreferrer">
                    DOWNLOAD
                  </a>
                </span>
              </li>
            </ul>
          )}
          {/* DRC output renders whether it passed or failed — that is the point of the check. */}
          {rules.length > 0 && (
            <ul className="ws-checks">
              {rules.map((r, i) => (
                <li key={i}>
                  <b>{r.passed === false ? '!' : '✓'}</b>
                  <span>
                    <strong>{String(r.rule ?? `rule-${i}`)}</strong>
                    {typeof r.severity === 'string' ? ` · ${r.severity}` : ''}
                    {typeof r.message === 'string' && r.message ? (
                      <><br />{r.message}</>
                    ) : null}
                  </span>
                </li>
              ))}
            </ul>
          )}
          {components.length > 0 && (
            <ul className="ws-checks">
              {components.map((c, i) => (
                <li key={i}>
                  <b>▸</b>
                  <span>
                    {String(c.footprint ?? c.part ?? `part-${i}`)}
                    {' · '}
                    {Array.isArray(c.pins) ? `${c.pins.length} pins` : 'part'}
                    {typeof c.layer === 'string' ? ` · ${c.layer}` : ''}
                  </span>
                </li>
              ))}
            </ul>
          )}
          <Artifacts result={state.result} />
        </div>
      )}
    </article>
  );
}

function RoboticsPanel() {
  const [angles, setAngles] = useState('0.5, -0.3');
  const [start, setStart] = useState('0.0, 0.0');
  const [goal, setGoal] = useState('1.0, -1.0');
  const [duration, setDuration] = useState('1.0');
  const [state, setState] = useState<PanelState>(idle);

  const parsedAngles = parseNumbers(angles);
  const parsedStart = parseNumbers(start);
  const parsedGoal = parseNumbers(goal);

  async function call(operation: string, parameters: Record<string, unknown>) {
    if (state.busy) return;
    setState({ busy: true, result: null, error: null });
    try {
      const result = await executeEngine('robotics', operation, parameters);
      setState({
        busy: false,
        result,
        error: result.success ? null : (result.errors[0]?.message ?? 'Robotics operation failed'),
      });
    } catch (e) {
      setState({ busy: false, result: null, error: errorText(e) });
    }
  }

  const data = (state.result?.result ?? {}) as Record<string, unknown>;
  const ee = (data.end_effector ?? {}) as Record<string, unknown>;
  const pos = (ee.position ?? {}) as Record<string, unknown>;
  const frames = (data.frames ?? []) as Array<Record<string, unknown>>;

  return (
    <article className="ws-panel">
      <h2>ROBOTICS · KINEMATICS</h2>
      <p className="ws-empty">
        Forward kinematics and joint-space trajectories on the native Denavit-Hartenberg
        chain. The engine&apos;s two-link default is used unless dh_params are supplied.
      </p>
      <div className="ws-form" style={{ marginTop: 14 }}>
        <Field label="JOINT_ANGLES (RAD)" value={angles} onChange={setAngles} hint="0.5, -0.3" />
        <div>
          <button
            className="btn-generate"
            type="button"
            disabled={state.busy || !parsedAngles}
            onClick={() => call('kinematics', { joint_angles: parsedAngles ?? [] })}
          >
            {state.busy ? 'WORKING…' : 'FORWARD KINEMATICS →'}
          </button>
        </div>
        <Field label="JOINT_START" value={start} onChange={setStart} hint="0.0, 0.0" />
        <Field label="JOINT_GOAL" value={goal} onChange={setGoal} hint="1.0, -1.0" />
        <Field label="DURATION_S" value={duration} onChange={setDuration} hint="1.0" />
        <div>
          <button
            className="btn-generate"
            type="button"
            disabled={state.busy || !parsedStart || !parsedGoal}
            onClick={() =>
              call('trajectory_plan', {
                joint_start: parsedStart ?? [],
                joint_goal: parsedGoal ?? [],
                duration_s: Number(duration),
              })
            }
          >
            PLAN TRAJECTORY →
          </button>
        </div>
      </div>
      {(parsedAngles === null || parsedStart === null || parsedGoal === null) && (
        <p className="ws-error">Joint fields need comma or space separated numbers.</p>
      )}
      {state.error && <p className="ws-error">{state.error}</p>}
      {state.result?.success && (
        <div style={{ marginTop: 16, display: 'grid', gap: 12 }}>
          <ResultHead result={state.result} />
          {typeof data.end_effector === 'object' ? (
            <>
              <Spec
                rows={[
                  ['JOINT_COUNT', data.joint_count],
                  ['CHAIN_SOURCE', data.chain_source],
                  ['EE_X_M', pos.x],
                  ['EE_Y_M', pos.y],
                  ['EE_Z_M', pos.z],
                  ['UNITS', (data.units as Record<string, unknown>)?.length],
                ]}
              />
              <ul className="ws-checks">
                {frames.map((f, i) => (
                  <li key={i}>
                    <b>▸</b>
                    <span>
                      {String(f.name ?? `link${i}`)}
                      {' · '}
                      {(['x', 'y', 'z'] as const).map((axis) => {
                        const p = f.position as Record<string, unknown>;
                        return `${axis.toUpperCase()} ${typeof p?.[axis] === 'number' ? p[axis].toFixed(4) : '—'}`;
                      }).join('  ')}
                    </span>
                  </li>
                ))}
              </ul>
            </>
          ) : (
            <Spec
              rows={[
                ['GOAL_REACHED', String(data.goal_reached ?? '—')],
                ['START_REACHED', String(data.start_reached ?? '—')],
                ['JOINT_COUNT', data.joint_count],
                ['SAMPLE_COUNT', data.sample_count],
                ['DT_S', data.dt],
                ['DURATION_S', data.duration_s],
                ['INTEGRATOR', data.integrator],
                ['FINAL', JSON.stringify(data.final_positions ?? [])],
              ]}
            />
          )}
          <Artifacts result={state.result} />
        </div>
      )}
    </article>
  );
}

const MCU_CHOICES = ['ESP32', 'STM32F401RE', 'RP2040'] as const;

type Step = { op: string; label: string; ok: boolean; detail: string };

function FirmwarePanel() {
  const [name, setName] = useState('trinity_demo');
  const [mcu, setMcu] = useState<string>('ESP32');
  const [peripheral, setPeripheral] = useState('UART1');
  const [kind, setKind] = useState('uart');
  const [pinA, setPinA] = useState('GPIO17');
  const [pinB, setPinB] = useState('GPIO16');
  const [state, setState] = useState<PanelState>(idle);
  const [steps, setSteps] = useState<Step[]>([]);
  const [project, setProject] = useState<Record<string, unknown> | null>(null);
  const [validateReport, setValidateReport] = useState<{ passed: boolean; rules: unknown } | null>(null);

  async function run(operation: string, parameters: Record<string, unknown>) {
    const result = await executeEngine('firmware', operation, parameters);
    if (!result.success) {
      setSteps((prev) => [
        ...prev,
        { op: operation, label: operation, ok: false, detail: result.errors[0]?.message ?? 'failed' },
      ]);
      setState({ busy: false, result, error: result.errors[0]?.message ?? 'Step failed' });
      throw new Error(result.errors[0]?.message ?? 'Step failed');
    }
    const data = result.result as Record<string, unknown>;
    const next = (data.project ?? null) as Record<string, unknown> | null;
    if (next) setProject(next);
    setSteps((prev) => [
      ...prev,
      { op: operation, label: operation, ok: true, detail: summarise(operation, data) },
    ]);
    return { result, project: next };
  }

  async function pipeline() {
    if (state.busy || !name.trim()) return;
    setState({ busy: true, result: null, error: null });
    setSteps([]);
    setProject(null);
    setValidateReport(null);
    try {
      const created = await run('create', { name: name.trim() });
      let current = created.project;
      if (!current) throw new Error('create returned no project');

      const selected = await run('select_mcu', { project: current, mcu });
      current = selected.project ?? current;

      // SPI is refused by the native engine, so it is not offered here.
      if (kind === 'uart' || kind === 'i2c') {
        const configured = await run('configure_peripheral', {
          project: current,
          peripheral,
          kind,
          tx: pinA,
          rx: pinB,
          ...(kind === 'i2c' ? { sda: pinA, scl: pinB } : { baud: 115200 }),
        });
        current = configured.project ?? current;
      } else {
        const configured = await run('configure_peripheral', {
          project: current,
          peripheral,
          kind: 'pwm',
          pin: pinA,
          freq_hz: 1000,
          duty: 128,
        });
        current = configured.project ?? current;
      }

      const generated = await run('generate', { project: current });
      current = generated.project ?? current;
      await run('validate', { project: current });
      const checked = await executeEngine('firmware', 'validate', { project: current });
      const report = (checked.result ?? {}) as Record<string, unknown>;
      setValidateReport({
        passed: report.passed === true,
        rules: report.rules,
      });
      setState({ busy: false, result: generated.result, error: null });
    } catch {
      // run() already recorded the failing step and set the error message.
      setState((prev) => ({ ...prev, busy: false }));
    }
  }

  async function build() {
    if (state.busy || !project) return;
    setState({ busy: true, result: null, error: null });
    try {
      const result = await executeEngine('firmware', 'build', { project });
      const data = result.result as Record<string, unknown>;
      setState({
        busy: false,
        result,
        error: result.success
          ? null
          : (result.errors[0]?.message ?? 'Build refused') +
            (data.toolchain ? ` (toolchain: ${String(data.toolchain)})` : ''),
      });
    } catch (e) {
      setState({ busy: false, result: null, error: errorText(e) });
    }
  }

  const mcuInfo = (project?.mcu ?? {}) as Record<string, unknown>;
  const sources = (project?.sources ?? []) as Array<Record<string, unknown>>;
  // Tracked separately: the pipeline's final state.result is the GENERATE
  // response, not VALIDATE's, so reading these off it showed "—" next to a
  // VALIDATED label even when every rule had passed.
  const validation = validateReport?.rules;
  const validated = validateReport?.passed;

  return (
    <article className="ws-panel">
      <h2>FIRMWARE · PIPELINE</h2>
      <p className="ws-empty">
        Walks the native chain end to end — create, select MCU, configure a peripheral, generate
        sources, validate. Each step hands its project to the next; nothing is kept between calls.
      </p>
      <div className="ws-form" style={{ marginTop: 14 }}>
        <Field label="PROJECT_NAME" value={name} onChange={setName} hint="trinity_demo" />
        <label className="ws-field">
          <span>MCU</span>
          <select className="ws-input" value={mcu} onChange={(e) => setMcu(e.target.value)}>
            {MCU_CHOICES.map((choice) => (
              <option key={choice} value={choice}>{choice}</option>
            ))}
          </select>
        </label>
        <label className="ws-field">
          <span>PERIPHERAL</span>
          <select className="ws-input" value={peripheral} onChange={(e) => setPeripheral(e.target.value)}>
            <option value="UART0">UART0</option>
            <option value="UART1">UART1</option>
            <option value="I2C0">I2C0</option>
            <option value="I2C1">I2C1</option>
            <option value="PWM">PWM</option>
          </select>
        </label>
        <label className="ws-field">
          <span>KIND</span>
          <select className="ws-input" value={kind} onChange={(e) => setKind(e.target.value)}>
            <option value="uart">uart</option>
            <option value="i2c">i2c</option>
            <option value="pwm">pwm</option>
          </select>
        </label>
        <Field
          label={kind === 'pwm' ? 'PIN' : kind === 'i2c' ? 'SDA' : 'TX'}
          value={pinA}
          onChange={setPinA}
          hint={kind === 'pwm' ? 'GPIO2' : 'GPIO17'}
        />
        {kind !== 'pwm' && (
          <Field
            label={kind === 'i2c' ? 'SCL' : 'RX'}
            value={pinB}
            onChange={setPinB}
            hint={kind === 'i2c' ? 'GPIO22' : 'GPIO16'}
          />
        )}
        <div style={{ display: 'flex', gap: 10, flexWrap: 'wrap' }}>
          <button className="btn-generate" type="button" disabled={state.busy || !name.trim()} onClick={pipeline}>
            {state.busy ? 'RUNNING…' : 'RUN PIPELINE →'}
          </button>
          <button className="btn-generate" type="button" disabled={state.busy || !project} onClick={build}>
            BUILD →
          </button>
        </div>
      </div>
      {state.error && <p className="ws-error">{state.error}</p>}

      {steps.length > 0 && (
        <ul className="ws-checks">
          {steps.map((step, i) => (
            <li key={i}>
              <b>{step.ok ? '✓' : '!'}</b>
              <span>
                <strong>{step.label}</strong>
                <br />
                {step.detail}
              </span>
            </li>
          ))}
        </ul>
      )}

      {project && (
        <Spec
          rows={[
            ['NAME', project.name],
            ['MCU', mcuInfo.model || '—'],
            ['ARCH', mcuInfo.architecture || '—'],
            ['CLOCK_HZ', project.clock_hz],
            ['PINS_MAPPED', Array.isArray(project.pin_mappings) ? project.pin_mappings.length : 0],
            ['SOURCES', sources.length],
            ['VALIDATED', validated === undefined ? '—' : String(validated)],
            ['RULES', Array.isArray(validation) ? validation.length : '—'],
          ]}
        />
      )}

      {sources.length > 0 && (
        <ul className="ws-checks">
          {sources.map((source, i) => (
            <li key={i}>
              <b>▸</b>
              <span>{String(source.path ?? `file-${i}`)}</span>
            </li>
          ))}
        </ul>
      )}

      {state.result && <Artifacts result={state.result} />}
      <p className="ws-empty" style={{ marginTop: 14 }}>
        Build compiles for real with the MCU toolchain. Without {String(
          (project?.build as Record<string, unknown> | undefined)?.toolchain || 'the toolchain',
        )}{' '}
        installed it refuses with a capability error — it never fakes a compile.
      </p>
    </article>
  );
}

/** One-line outcome for a pipeline step, taken from that step's own result. */
function summarise(operation: string, data: Record<string, unknown>): string {
  switch (operation) {
    case 'create':
      return `project "${String(data.name ?? '')}" created`;
    case 'select_mcu':
      return `${String(data.mcu ?? '')} at ${Number(data.clock_hz ?? 0) / 1e6} MHz`;
    case 'configure_peripheral':
      return `${String(data.peripheral ?? '')} configured as ${String(data.kind ?? '')}`;
    case 'generate':
      return `${String(data.file_count ?? 0)} source files written`;
    case 'validate':
      return data.passed ? 'all rules passed' : 'rules failed';
    default:
      return '';
  }
}

type VisionOp =
  | 'image_inspect'
  | 'image_resize'
  | 'image_grayscale'
  | 'image_edges'
  | 'image_stats';

const VISION_LABEL: Record<VisionOp, string> = {
  image_inspect: 'INSPECT',
  image_resize: 'RESIZE',
  image_grayscale: 'GRAYSCALE',
  image_edges: 'EDGE DETECT',
  image_stats: 'STATISTICS',
};

const round2 = (n: unknown) =>
  typeof n === 'number' ? Math.round(n * 100) / 100 : String(n ?? '—');

/** Per-channel triples arrive as arrays; a scalar stays scalar. */
const fmtChannels = (v: unknown) => {
  if (Array.isArray(v)) return v.map((n) => round2(n)).join(' / ');
  return round2(v);
};

function VisionPanel() {
  const [uploaded, setUploaded] = useState<ArtifactOut | null>(null);
  const [source, setSource] = useState<ArtifactOut | null>(null);
  const [fileName, setFileName] = useState('');
  const [op, setOp] = useState<VisionOp>('image_inspect');
  const [width, setWidth] = useState('640');
  const [height, setHeight] = useState('480');
  const [low, setLow] = useState('50');
  const [high, setHigh] = useState('150');
  const [state, setState] = useState<PanelState>(idle);

  async function pick(file: File) {
    if (state.busy) return;
    setState({ busy: true, result: null, error: null });
    setFileName(file.name);
    try {
      // Uploads go through ArtifactManager, so the file is checksummed and
      // tracked like any engine output before it is ever read.
      const ref = await uploadImage(file);
      setUploaded(ref);
      setSource(ref);
      setState({ busy: false, result: null, error: null });
    } catch (e) {
      setUploaded(null);
      setSource(null);
      setState({ busy: false, result: null, error: errorText(e) });
    }
  }

  async function run() {
    if (!source || state.busy) return;

    const parameters: Record<string, unknown> = { artifact_id: source.artifact_id };
    if (op === 'image_resize') {
      // Checked here rather than at the API: OpenCV would throw on a
      // zero or negative extent, and the resulting error names OpenCV
      // instead of the field the user got wrong.
      const w = Number(width);
      const h = Number(height);
      if (!Number.isInteger(w) || w < 1 || !Number.isInteger(h) || h < 1) {
        setState({
          busy: false,
          result: null,
          error: 'WIDTH_PX and HEIGHT_PX must be whole pixels of at least 1.',
        });
        return;
      }
      parameters.width = w;
      parameters.height = h;
    }
    if (op === 'image_edges') {
      parameters.low = Number(low);
      parameters.high = Number(high);
    }

    setState({ busy: true, result: null, error: null });
    try {
      const result = await executeEngine('vision', op, parameters);
      setState({
        busy: false,
        result,
        error: result.success ? null : (result.errors[0]?.message ?? 'Vision operation failed'),
      });
      // Chain: the produced image becomes the next operation's input, so
      // resize -> grayscale -> edges is a pipeline rather than five
      // disconnected buttons that each start over from the upload.
      const produced = result.artifacts?.find((a) => a.type === 'Image');
      if (produced) setSource(produced);
    } catch (e) {
      setState({ busy: false, result: null, error: errorText(e) });
    }
  }

  const payload = (state.result?.result ?? {}) as Record<string, unknown>;
  const inMeta = ((payload.input ?? {}) as Record<string, unknown>).metadata as
    | Record<string, unknown>
    | undefined;
  const outMeta = ((payload.output ?? {}) as Record<string, unknown>).metadata as
    | Record<string, unknown>
    | undefined;
  const stats = (payload.statistics ?? {}) as Record<string, unknown>;

  // An inspect has no output to report; a transform's whole point is the
  // new image. Showing input dimensions for a resize would claim nothing
  // changed.
  const isTransform = op === 'image_resize' || op === 'image_grayscale' || op === 'image_edges';

  const dimensionRows: [string, unknown][] = isTransform
    ? [
        ['FORMAT', outMeta?.format],
        ['WIDTH_PX', outMeta?.width],
        ['HEIGHT_PX', outMeta?.height],
        ['CHANNELS', outMeta?.channels],
        ['SIZE_BYTES', outMeta?.size_bytes],
      ]
    : [
        ['FORMAT', inMeta?.format],
        ['WIDTH_PX', inMeta?.width],
        ['HEIGHT_PX', inMeta?.height],
        ['CHANNELS', inMeta?.channels],
        ['SIZE_BYTES', inMeta?.size_bytes],
      ];

  // What the transform started from, so "1280 → 640" is visible rather than
  // implied.
  const wasRows: [string, unknown][] = isTransform
    ? [
        ['WAS_WIDTH_PX', inMeta?.width],
        ['WAS_HEIGHT_PX', inMeta?.height],
        ['WAS_CHANNELS', inMeta?.channels],
      ]
    : [];

  return (
    <article className="ws-panel">
      <h2>VISION · IMAGE</h2>
      <p className="ws-empty">
        Upload an image, then run it through the native OpenCV processors. The upload is stored
        as a checksummed artifact and the engine is given its id, never a raw path. Each
        transform feeds the next operation.
      </p>
      <div className="ws-form" style={{ marginTop: 14 }}>
        <label className="ws-field">
          <span>IMAGE_FILE</span>
          <input
            className="ws-input"
            type="file"
            accept="image/png,image/jpeg,image/bmp"
            disabled={state.busy}
            onChange={(e) => {
              const file = e.target.files?.[0];
              if (file) pick(file);
            }}
          />
        </label>

        <label className="ws-field">
          <span>OPERATION</span>
          <select
            className="ws-input"
            value={op}
            disabled={state.busy || !source}
            onChange={(e) => setOp(e.target.value as VisionOp)}
          >
            <option value="image_inspect">INSPECT · metadata only</option>
            <option value="image_resize">RESIZE · to WIDTH_PX × HEIGHT_PX</option>
            <option value="image_grayscale">GRAYSCALE · single channel</option>
            <option value="image_edges">EDGE DETECT · Canny</option>
            <option value="image_stats">STATISTICS · per-channel</option>
          </select>
        </label>

        {op === 'image_resize' && (
          <>
            <label className="ws-field">
              <span>WIDTH_PX</span>
              <input
                className="ws-input"
                value={width}
                inputMode="numeric"
                disabled={state.busy}
                onChange={(e) => setWidth(e.target.value)}
              />
            </label>
            <label className="ws-field">
              <span>HEIGHT_PX</span>
              <input
                className="ws-input"
                value={height}
                inputMode="numeric"
                disabled={state.busy}
                onChange={(e) => setHeight(e.target.value)}
              />
            </label>
          </>
        )}

        {op === 'image_edges' && (
          <>
            <label className="ws-field">
              <span>CANNY_LOW</span>
              <input
                className="ws-input"
                value={low}
                inputMode="numeric"
                disabled={state.busy}
                onChange={(e) => setLow(e.target.value)}
              />
            </label>
            <label className="ws-field">
              <span>CANNY_HIGH</span>
              <input
                className="ws-input"
                value={high}
                inputMode="numeric"
                disabled={state.busy}
                onChange={(e) => setHigh(e.target.value)}
              />
            </label>
          </>
        )}

        <div>
          <button className="btn-generate" type="button" disabled={state.busy || !source} onClick={run}>
            {state.busy ? 'RUNNING…' : `${VISION_LABEL[op]} →`}
          </button>
        </div>
      </div>
      {state.error && <p className="ws-error">{state.error}</p>}

      {uploaded && (
        <Spec
          rows={[
            ['FILE', fileName || '—'],
            ['ARTIFACT', uploaded.artifact_id.slice(0, 8)],
            ['STORED_BYTES', uploaded.size_bytes],
            ['CHECKSUM', uploaded.checksum.slice(0, 16)],
            ...(source && source.artifact_id !== uploaded.artifact_id
              ? ([['INPUT_ARTIFACT', source.artifact_id.slice(0, 8)]] as [string, unknown][])
              : []),
          ]}
        />
      )}

      {state.result?.success && (
        <div style={{ marginTop: 16, display: 'grid', gap: 12 }}>
          <ResultHead result={state.result} />

          {op === 'image_stats' ? (
            <Spec
              rows={[
                ['WIDTH_PX', stats.width],
                ['HEIGHT_PX', stats.height],
                ['CHANNELS', stats.channels],
                ['MEAN', fmtChannels(stats.mean)],
                ['MIN', fmtChannels(stats.min)],
                ['MAX', fmtChannels(stats.max)],
                ['OVERALL_MEAN', round2(stats.overall_mean)],
                ['OVERALL_MIN', round2(stats.overall_min)],
                ['OVERALL_MAX', round2(stats.overall_max)],
              ]}
            />
          ) : (
            <Spec rows={[...dimensionRows, ...wasRows]} />
          )}
          <Artifacts result={state.result} />
        </div>
      )}
      <p className="ws-empty" style={{ marginTop: 14 }}>
        Needs an OpenCV build of Trinity.exe — the plain build reports the capability as
        unavailable rather than guessing.
      </p>
    </article>
  );
}

export default function BridgedEngines() {
  return (
    <section className="section-sm" id="bridged-engines" aria-labelledby="bridged-title">
      <div className="container">
        <div className="section-head">
          <div>
            <p className="eyebrow">Bridged engines</p>
            <h2 id="bridged-title">
              Six more engines, one subprocess away.
            </h2>
          </div>
          <p className="lede">
            Research, simulation, PCB, robotics, firmware and vision run in the native backend
            and stream back through the same envelope — result, artifacts, validation, job id.
            When the native backend is not connected these consoles say so instead of faking
            output.
          </p>
        </div>
        <div className="workspace-grid">
          <ResearchPanel />
          <SimulationPanel />
          <PcbPanel />
          <RoboticsPanel />
          <FirmwarePanel />
          <VisionPanel />
        </div>
      </div>
    </section>
  );
}
