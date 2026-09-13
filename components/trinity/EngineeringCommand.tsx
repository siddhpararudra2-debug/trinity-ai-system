'use client';

import { useEffect, useRef, useState } from 'react';
import { executeRequirement, STAGE_LABELS, type ExecutionStage, type TrinityResult } from '@/lib/api/trinity';

type Props = {
  onResult: (result: TrinityResult | null, error: string | null) => void;
  onStageChange?: (stage: ExecutionStage) => void;
};

const EXAMPLES = [
  'Create a 50 mm quadcopter frame',
  'Create a 75 mm quadcopter frame',
  'Create a 60 mm drone frame',
];

const STAGES: ExecutionStage[] = ['analyzing', 'building', 'validating'];

export default function EngineeringCommand({ onResult, onStageChange }: Props) {
  const [text, setText] = useState('Create a 50 mm quadcopter frame');
  const [stage, setStage] = useState<ExecutionStage>('idle');
  const [busy, setBusy] = useState(false);
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const setStaged = (s: ExecutionStage) => {
    setStage(s);
    onStageChange?.(s);
  };

  useEffect(() => {
    return () => {
      if (timerRef.current) clearTimeout(timerRef.current);
    };
  }, []);

  async function run() {
    if (!text.trim() || busy) return;
    setBusy(true);
    onResult(null, null);
    setStaged('analyzing');

    // Simulate staged UI while awaiting single fetch
    let idx = 0;
    const tick = () => {
      if (idx < STAGES.length - 1) {
        idx += 1;
        setStaged(STAGES[idx]);
        timerRef.current = setTimeout(tick, 520);
      }
    };
    timerRef.current = setTimeout(tick, 520);

    try {
      const result = await executeRequirement(text.trim());
      if (timerRef.current) clearTimeout(timerRef.current);
      setStaged('complete');
      onResult(result, null);
      // keep complete briefly then idle
      setTimeout(() => setStaged('idle'), 2400);
    } catch (e) {
      if (timerRef.current) clearTimeout(timerRef.current);
      setStaged('idle');
      onResult(null, e instanceof Error ? e.message : 'Unable to execute request');
    } finally {
      setBusy(false);
    }
  }

  const onKeyDown = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
    if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') {
      e.preventDefault();
      run();
    }
  };

  const charCount = text.length;

  return (
    <section className="command-section" id="workspace" aria-labelledby="command-title">
      <div className="container">
        <div className="section-label">01 / REQUIREMENT ENGINE</div>
        <div className="command-head">
          <div>
            <h2 id="command-title">Describe what you want to build.</h2>
          </div>
          <p className="lede">
            Natural language → structured spec. The deterministic router maps your
            intent onto the CAD IR without a model in the loop.
          </p>
        </div>

        <div className="command-console">
          <div className="command-top">
            <strong>REQUIREMENT</strong>
            <span>TRINITY ENGINE · NATURAL LANGUAGE → ENGINEERING SPEC</span>
          </div>

          <div className="command-input-wrap">
            <label htmlFor="trinity-requirement" className="telemetry" style={{ display: 'block', marginBottom: 10 }}>
              ENGINEERING REQUIREMENT — V1 PARSER
            </label>
            <textarea
              id="trinity-requirement"
              className="command-textarea"
              value={text}
              onChange={(e) => setText(e.target.value)}
              onKeyDown={onKeyDown}
              placeholder="Create a 50 mm quadcopter frame with 2 mm arms…"
              rows={3}
              maxLength={1000}
              spellCheck={false}
              aria-describedby="char-count"
            />
            <div style={{ display: 'flex', justifyContent: 'space-between', marginTop: 10, fontFamily: 'var(--font-mono-plex), monospace', fontSize: '0.62rem', color: '#707070', letterSpacing: '0.06em' }}>
              <span>{charCount} / 1000</span>
              <span style={{ opacity: 0.6 }}>⌘ + ENTER to generate</span>
            </div>

            <div className="command-history" aria-label="Example requirements">
              {EXAMPLES.map((ex) => (
                <button key={ex} className="history-chip" onClick={() => setText(ex)} type="button">
                  {ex}
                </button>
              ))}
            </div>

            <div className="stages-bar" aria-hidden="true">
              {STAGES.map((s) => (
                <i
                  key={s}
                  className={
                    stage === s ? 'is-active' : stage === 'complete' || (stage !== 'idle' && STAGES.indexOf(s) < STAGES.indexOf(stage)) ? 'is-done' : ''
                  }
                />
              ))}
            </div>
          </div>

          <div className="command-bottom">
            <div className="command-meta">
              <span>
                <i style={{ width: 6, height: 6, borderRadius: '50%', background: busy ? '#315B73' : '#2D6A4F', display: 'inline-block' }} />
                {STAGE_LABELS[stage]}
              </span>
              <span>TRINITY / V1</span>
              <span style={{ opacity: 0.5 }}>DETERMINISTIC CORE</span>
            </div>
            <div className="command-actions">
              <span className="kbd-hint">⌘ ENTER</span>
              <button className="btn-generate" onClick={run} disabled={busy} type="button" aria-live="polite">
                {busy ? STAGE_LABELS[stage] : 'GENERATE →'}
              </button>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
}
