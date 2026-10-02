/* eslint-disable react-hooks/set-state-in-effect */
'use client';

import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Inter, Instrument_Serif } from 'next/font/google';
import './chat.css';
import {
  executeRequirement,
  generateCad,
  getArtifactUrl,
  getCadCatalog,
  TrinityApiError,
  type CadPart,
  type ParseSummary,
  type TrinityResult,
} from '@/lib/api/trinity';

const inter = Inter({ subsets: ['latin'], display: 'swap', axes: ['opsz'] });
const instrumentSerif = Instrument_Serif({ weight: '400', style: 'italic', subsets: ['latin'], display: 'swap' });

type Message = {
  id: number;
  role: 'user' | 'bot';
  content: React.ReactNode;
  delay?: string;
  result?: TrinityResult;
  parse?: ParseSummary;
  part?: CadPart;
  suggestions?: string[];
  examples?: string[];
};

type Mode = 'prompt' | 'form';

const INITIAL_MESSAGES: Message[] = [
  {
    id: 1,
    role: 'bot',
    delay: '0.42s',
    content: 'Describe a CAD part to generate, or switch to Form to enter exact dimensions. Trinity will show what it understood and create your selected files.',
  },
];

function humanize(value: string): string {
  return value.replaceAll('_', ' ').replace(/\b\w/g, (letter) => letter.toUpperCase());
}

function readStringList(value: unknown): string[] {
  if (!Array.isArray(value)) return [];
  return value.map((entry) => {
    if (typeof entry === 'string') return entry;
    if (entry && typeof entry === 'object') {
      const row = entry as Record<string, unknown>;
      return typeof row.name === 'string' ? row.name : typeof row.text === 'string' ? row.text : '';
    }
    return '';
  }).filter(Boolean);
}

function errorHints(result: TrinityResult): { suggestions: string[]; examples: string[] } {
  const details = result.errors?.[0]?.details;
  if (!details || typeof details !== 'object') return { suggestions: [], examples: [] };
  const row = details as Record<string, unknown>;
  return {
    suggestions: [...new Set([
      ...readStringList(row.suggestions),
      ...readStringList(row.nearest_parts),
      ...readStringList(row.matching_parts),
      ...readStringList(row.supported),
      ...readStringList(row.supported_types),
    ])],
    examples: [...new Set([
      ...readStringList(row.examples),
      ...readStringList(row.example_sentences),
    ])],
  };
}

function defaultValues(part: CadPart | undefined): Record<string, number> {
  return Object.fromEntries((part?.parameters ?? []).map((parameter) => [parameter.name, parameter.default]));
}

function valueText(value: unknown, unit?: string): string {
  if (Array.isArray(value)) return value.map((item) => String(item)).join(' × ');
  if (typeof value === 'number') return `${Number.isInteger(value) ? value : Number(value.toFixed(3))}${unit && unit !== 'count' ? ` ${unit}` : ''}`;
  if (typeof value === 'string') return value;
  if (value === null || value === undefined) return '—';
  return String(value);
}

function defaultNames(defaults: ParseSummary['defaults'] | ParseSummary['defaults_filled']): Set<string> {
  if (Array.isArray(defaults)) {
    return new Set(defaults.map((entry) => {
      if (typeof entry === 'string') return entry;
      if (typeof entry.name === 'string') return entry.name;
      if (typeof entry.parameter === 'string') return entry.parameter;
      return typeof entry.key === 'string' ? entry.key : '';
    }).filter(Boolean));
  }
  if (defaults && typeof defaults === 'object') return new Set(Object.keys(defaults));
  return new Set();
}

function UnderstoodPanel({ result, parse, part }: { result: TrinityResult; parse?: ParseSummary; part?: CadPart }) {
  const parsed = parse ?? result.result.parse;
  const extracted = parsed?.extracted_values ?? parsed?.values ?? {};
  const nestedParameters = extracted.parameters && typeof extracted.parameters === 'object'
    ? extracted.parameters as Record<string, unknown>
    : null;
  const specification = result.result.spec?.parameters ?? {};
  const values = nestedParameters ?? (Object.keys(extracted).length ? extracted : specification);
  const defaults = defaultNames(parsed?.defaults ?? parsed?.defaults_filled);
  const partName = String(extracted.type ?? extracted.object ?? extracted.part ?? result.result.type ?? part?.name ?? 'CAD part');
  const rule = parsed?.matched_rule ?? parsed?.rule;
  const ruleName = typeof rule === 'string' ? rule : rule && typeof rule.name === 'string' ? rule.name : undefined;
  const rows = Object.entries(values).filter(([key, value]) => {
    return !['type', 'object', 'part', 'units', 'parameters'].includes(key) && value !== undefined && value !== null;
  });

  return (
    <section className="understood-panel" aria-label="What Trinity understood">
      <div className="understood-heading">
        <span>UNDERSTOOD AS</span>
        {ruleName && <span className="understood-rule">{humanize(ruleName)}</span>}
      </div>
      <strong className="understood-part">{humanize(partName)}</strong>
      {rows.length > 0 && (
        <dl className="understood-values">
          {rows.map(([key, value]) => {
            const parameter = part?.parameters.find((entry) => entry.name === key);
            const unit = parameter?.unit ?? (key.endsWith('_mm') ? 'mm' : undefined);
            return (
              <div className="understood-value" key={key}>
                <dt>{humanize(key)}</dt>
                <dd>{valueText(value, unit)}{defaults.has(key) ? <span className="default-tag">DEFAULT</span> : null}</dd>
              </div>
            );
          })}
        </dl>
      )}
      {defaults.size > 0 && (
        <p className="understood-defaults">Defaults filled: {[...defaults].map(humanize).join(', ')}</p>
      )}
    </section>
  );
}

function ErrorHints({
  message,
  suggestions = [],
  examples = [],
  onChoose,
}: {
  message: string;
  suggestions?: string[];
  examples?: string[];
  onChoose: (value: string) => void;
}) {
  return (
    <div className="request-error">
      <p>{message}</p>
      {suggestions.length > 0 && (
        <div className="hint-group">
          <span>CATALOG SUGGESTIONS</span>
          <div className="hint-list">
            {suggestions.map((suggestion) => (
              <button key={suggestion} type="button" className="hint-chip" onClick={() => onChoose(`Create a ${suggestion}`)}>
                {humanize(suggestion)}
              </button>
            ))}
          </div>
        </div>
      )}
      {examples.length > 0 && (
        <div className="hint-group">
          <span>TRY AN EXAMPLE</span>
          <div className="hint-list">
            {examples.map((example) => (
              <button key={example} type="button" className="hint-chip" onClick={() => onChoose(example)}>{example}</button>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}

function ResultContent({ result }: { result: TrinityResult }) {
  const failure = result.errors?.[0]?.message;
  if (!result.success) return <span>{failure || 'Operation failed.'}</span>;
  const part = result.result.type ? humanize(String(result.result.type)) : 'CAD part';
  const triangles = result.result.triangle_count;
  return (
    <div className="result-content">
      <strong>{part} generated</strong>
      {typeof triangles === 'number' && <span>{triangles.toLocaleString()} triangles · {result.validation?.status ?? 'GENERATED'}</span>}
      {result.artifacts.length > 0 && (
        <div className="artifact-links" aria-label="Generated files">
          {result.artifacts.map((artifact) => (
            <a key={artifact.artifact_id} href={getArtifactUrl(artifact.artifact_id)} target="_blank" rel="noreferrer">
              {artifact.type.toUpperCase()} ↓
            </a>
          ))}
        </div>
      )}
    </div>
  );
}

export default function ChatPage() {
  const [messages, setMessages] = useState<Message[]>(INITIAL_MESSAGES);
  const [inputValue, setInputValue] = useState('');
  const [isLoading, setIsLoading] = useState(false);
  const [mode, setMode] = useState<Mode>('prompt');
  const [catalog, setCatalog] = useState<CadPart[]>([]);
  const [catalogLoading, setCatalogLoading] = useState(true);
  const [catalogError, setCatalogError] = useState('');
  const [selectedPartName, setSelectedPartName] = useState('');
  const [formValues, setFormValues] = useState<Record<string, number>>({});
  const [editedFields, setEditedFields] = useState<Set<string>>(new Set());
  const [outputs, setOutputs] = useState<string[]>(['stl', 'glb', 'json']);
  const [formError, setFormError] = useState<{ message: string; suggestions: string[]; examples: string[] } | null>(null);
  const historyRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  const selectedPart = useMemo(
    () => catalog.find((part) => part.name === selectedPartName) ?? catalog[0],
    [catalog, selectedPartName],
  );
  const examples = useMemo(() => catalog.flatMap((part) => {
    const partExamples = part.examples ?? part.example_sentences ?? [];
    return (partExamples.length ? partExamples : [`Create a ${part.name.replaceAll('_', ' ')}`])
      .slice(0, 1)
      .map((text) => ({ part: part.name, text }));
  }), [catalog]);

  useEffect(() => {
    document.title = 'Trinity AI';
    const wrapper = document.getElementById('chatWrapper');
    if (!wrapper) return;

    const handleAnimEnd = (e: AnimationEvent) => {
      const target = e.target as HTMLElement;
      if (
        target.classList.contains('appear') ||
        target.classList.contains('hero-photo') ||
        target.tagName === 'EM' ||
        target.classList.contains('badge-star')
      ) {
        target.classList.add('is-in');
      }
    };

    wrapper.addEventListener('animationend', handleAnimEnd);
    let r2: number;
    const r1 = requestAnimationFrame(() => {
      r2 = requestAnimationFrame(() => {
        let hasAnimation = false;
        document.getAnimations().forEach((anim) => {
          if (anim.playState === 'running' || anim.playState === 'finished') hasAnimation = true;
        });
        if (!hasAnimation) {
          document
            .querySelectorAll('#chatWrapper .appear, #chatWrapper .hero-photo, #chatWrapper h1 em, #chatWrapper .badge-star')
            .forEach((el) => el.classList.add('is-in'));
        }
      });
    });

    return () => {
      wrapper.removeEventListener('animationend', handleAnimEnd);
      cancelAnimationFrame(r1);
      cancelAnimationFrame(r2);
    };
  }, []);

  useEffect(() => {
    let alive = true;
    getCadCatalog()
      .then((parts) => {
        if (!alive) return;
        setCatalog(parts);
        setSelectedPartName(parts[0]?.name ?? '');
        setFormValues(defaultValues(parts[0]));
        setCatalogError(parts.length ? '' : 'The CAD catalog is empty.');
      })
      .catch((error: unknown) => {
        if (!alive) return;
        setCatalogError(error instanceof Error ? error.message : 'The CAD catalog could not be loaded.');
      })
      .finally(() => {
        if (alive) setCatalogLoading(false);
      });
    return () => { alive = false; };
  }, []);

  useEffect(() => {
    if (historyRef.current) historyRef.current.scrollTop = historyRef.current.scrollHeight;
  }, [messages, isLoading]);

  function addFailure(error: unknown) {
    const apiError = error instanceof TrinityApiError ? error : null;
    const message = error instanceof Error ? error.message : String(error);
    const fallbackSuggestions = catalog.slice(0, 5).map((part) => part.name);
    const fallbackExamples = examples.slice(0, 3).map((example) => example.text);
    setMessages((prev) => [...prev, {
      id: Date.now(),
      role: 'bot',
      delay: '0s',
      content: message,
      suggestions: apiError?.suggestions.length ? apiError.suggestions : fallbackSuggestions,
      examples: apiError?.examples.length ? apiError.examples : fallbackExamples,
    }]);
  }

  function appendResult(result: TrinityResult, extras: { parse?: ParseSummary; part?: CadPart } = {}) {
    const hints = errorHints(result);
    const fallbackSuggestions = catalog.slice(0, 5).map((part) => part.name);
    const fallbackExamples = examples.slice(0, 3).map((example) => example.text);
    setMessages((prev) => [...prev, {
      id: Date.now(),
      role: 'bot',
      delay: '0s',
      content: <ResultContent result={result} />,
      result,
      ...extras,
      ...(!result.success ? {
        suggestions: hints.suggestions.length ? hints.suggestions : fallbackSuggestions,
        examples: hints.examples.length ? hints.examples : fallbackExamples,
      } : {}),
    }]);
  }

  const handleSubmit = async (e?: React.FormEvent) => {
    if (e) e.preventDefault();
    if (!inputValue.trim() || isLoading) return;

    const text = inputValue.trim();
    setMessages((prev) => [...prev, { id: Date.now(), role: 'user', delay: '0s', content: text }]);
    setInputValue('');
    setIsLoading(true);
    try {
      appendResult(await executeRequirement(text));
    } catch (error: unknown) {
      addFailure(error);
    } finally {
      setIsLoading(false);
    }
  };

  const handleFormSubmit = async (e: React.FormEvent<HTMLFormElement>) => {
    e.preventDefault();
    if (!selectedPart || isLoading) return;
    setFormError(null);
    const part = selectedPart;
    const form = e.currentTarget;
    let hasInvalidParameter = false;
    form.querySelectorAll<HTMLInputElement>('.cad-fields-grid input').forEach((input) => {
      const parameter = part.parameters.find((entry) => entry.name === input.name);
      if (!parameter) return;
      const value = input.valueAsNumber;
      let message = '';
      if (parameter.min_exclusive && Number.isFinite(value) && value <= Number(parameter.min)) {
        message = `Enter a value greater than ${parameter.min} ${parameter.unit}.`;
      }
      if (parameter.allowed_values?.length && !parameter.allowed_values.includes(value)) {
        message = `Choose one of: ${parameter.allowed_values.join(', ')}.`;
      }
      input.setCustomValidity(message);
      if (message) hasInvalidParameter = true;
    });
    if (!form.reportValidity() || hasInvalidParameter) return;
    const parameters = { ...defaultValues(part), ...formValues };
    const filledDefaults = part.parameters
      .filter((parameter) => !editedFields.has(parameter.name))
      .map((parameter) => parameter.name);
    const display = `${humanize(part.name)} · ${part.parameters.map((parameter) => `${humanize(parameter.name)} ${valueText(parameters[parameter.name], parameter.unit)}`).join(' · ')}`;
    const parse: ParseSummary = {
      matched_rule: 'Direct form input',
      extracted_values: { type: part.name, parameters },
      defaults: filledDefaults,
    };
    setMessages((prev) => [...prev, { id: Date.now(), role: 'user', delay: '0s', content: display }]);
    setIsLoading(true);
    try {
      const result = await generateCad({ type: part.name, parameters, outputs });
      appendResult(result, { parse, part });
    } catch (error: unknown) {
      const apiError = error instanceof TrinityApiError ? error : null;
      setFormError({
        message: error instanceof Error ? error.message : String(error),
        suggestions: apiError?.suggestions ?? [],
        examples: apiError?.examples ?? [],
      });
      addFailure(error);
    } finally {
      setIsLoading(false);
    }
  };

  const chooseHint = (value: string) => {
    setMode('prompt');
    setInputValue(value);
    window.setTimeout(() => inputRef.current?.focus(), 0);
  };

  const changePart = (name: string) => {
    const part = catalog.find((entry) => entry.name === name);
    setSelectedPartName(name);
    setFormValues(defaultValues(part));
    setEditedFields(new Set());
    setFormError(null);
  };

  const toggleOutput = (output: string) => {
    setOutputs((current) => current.includes(output)
      ? current.filter((entry) => entry !== output)
      : [...current, output]);
  };

  return (
    <div
      id="chatWrapper"
      className={inter.className}
      style={{ '--font-instrument': instrumentSerif.style.fontFamily } as React.CSSProperties}
    >
      <div className="grain"></div>
      <div className="hero-photo">
        <video
          src="https://d8j0ntlcm91z4.cloudfront.net/user_38xzZboKViGWJOttwIXH07lWA1P/hf_20260818_072341_50851634-bbc3-4c33-9acc-7647d4db44aa.mp4"
          autoPlay
          loop
          muted
          playsInline
        ></video>
      </div>
      <div className="page">
        <main id="top" className={`hero ${mode === 'form' ? 'cad-form-open' : ''}`}>
          <div
            className={`chat-container ${mode === 'form' ? 'is-form-mode' : ''}`}
            style={{ height: '75vh', alignItems: 'center', justifyContent: 'flex-end', display: 'flex', flexDirection: 'column' }}
          >
            <h1
              className="appear appear--soft"
              style={{ '--d': '0.1s', fontSize: '56px', fontWeight: 300, marginBottom: 'auto', letterSpacing: '-0.02em', paddingTop: '2vh' } as React.CSSProperties}
            >
              Trinity
            </h1>

            <div className="chat-history" style={{ width: '100%' }} ref={historyRef} aria-live="polite">
      {messages.map((message) => (
                <div
                  key={message.id}
                  className={`chat-message ${message.role} appear appear--soft`}
                  style={{ '--d': message.delay } as React.CSSProperties}
                >
                  <div className="message-content message-bubble">
                    {message.content}
                    {message.result?.success && (
                      <UnderstoodPanel
                        result={message.result}
                        parse={message.parse}
                        part={message.part ?? catalog.find((part) => part.name === String(message.result?.result.type ?? ''))}
                      />
                    )}
                    {!message.result?.success && (message.suggestions?.length || message.examples?.length) ? (
                      <ErrorHints message="Try one of these catalog matches." suggestions={message.suggestions} examples={message.examples} onChoose={chooseHint} />
                    ) : null}
                  </div>
                </div>
              ))}
              {isLoading && (
                <div className="chat-message bot appear appear--soft is-in" style={{ '--d': '0s' } as React.CSSProperties}>
                  <div className="message-content message-bubble"><span style={{ opacity: 0.5 }}>Processing...</span></div>
                </div>
              )}
            </div>

            <div className="composer-area">
              <div className="input-tabs" role="tablist" aria-label="CAD input method">
                <button type="button" role="tab" aria-selected={mode === 'prompt'} className={`input-tab ${mode === 'prompt' ? 'is-active' : ''}`} onClick={() => setMode('prompt')}>
                  Prompt
                </button>
                <button type="button" role="tab" aria-selected={mode === 'form'} className={`input-tab ${mode === 'form' ? 'is-active' : ''}`} onClick={() => setMode('form')}>
                  Form
                </button>
              </div>

              {mode === 'prompt' ? (
                <>
                  <form className="chat-input-wrapper" onSubmit={handleSubmit}>
                    <input
                      ref={inputRef}
                      type="text"
                      className="chat-input"
                      placeholder="Describe a part to generate..."
                      value={inputValue}
                      onChange={(e) => setInputValue(e.target.value)}
                      disabled={isLoading}
                      aria-label="Describe a CAD part"
                    />
                    <button type="submit" className="chat-send-btn" aria-label="Send message" disabled={isLoading || !inputValue.trim()}>
                      <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
                        <line x1="22" y1="2" x2="11" y2="13"></line>
                        <polygon points="22 2 15 22 11 13 2 9 22 2"></polygon>
                      </svg>
                    </button>
                  </form>
                  {examples.length > 0 && (
                    <div className="example-row" aria-label="Example prompts">
                      <span className="example-label">TRY</span>
                      {examples.slice(0, 5).map((example) => (
                        <button key={example.part} type="button" className="example-chip" onClick={() => chooseHint(example.text)}>
                          {humanize(example.part)}
                        </button>
                      ))}
                    </div>
                  )}
                </>
              ) : (
                <form className="cad-form-panel" onSubmit={handleFormSubmit}>
                  {catalogLoading ? (
                    <p className="catalog-status">Loading the CAD catalog…</p>
                  ) : catalogError ? (
                    <p className="catalog-status is-error">{catalogError}</p>
                  ) : selectedPart ? (
                    <>
                      <div className="cad-form-top">
                        <label className="cad-field cad-part-select">
                          <span>PART</span>
                          <select value={selectedPart.name} onChange={(event) => changePart(event.target.value)}>
                            {catalog.map((part) => <option value={part.name} key={part.name}>{humanize(part.name)}</option>)}
                          </select>
                        </label>
                        <p className="cad-part-description">{selectedPart.description ?? 'Parametric CAD part'}</p>
                      </div>

                      <div className="cad-fields-grid">
                        {selectedPart.parameters.map((parameter) => (
                          <label className="cad-field" key={parameter.name}>
                            <span>{parameter.label ?? humanize(parameter.name)} <small>{parameter.unit}</small></span>
                            <input
                              type="number"
                              inputMode="decimal"
                              name={parameter.name}
                              step={parameter.integer || parameter.unit === 'count' ? 1 : 'any'}
                              min={parameter.allowed_values?.length ? Math.min(...parameter.allowed_values) : parameter.min ?? undefined}
                              max={parameter.allowed_values?.length ? Math.max(...parameter.allowed_values) : parameter.max ?? undefined}
                              title={parameter.description}
                              value={formValues[parameter.name] ?? parameter.default}
                              onChange={(event) => {
                                const value = event.target.valueAsNumber;
                                const invalidMinimum = parameter.min_exclusive && Number.isFinite(value) && value <= Number(parameter.min);
                                const invalidAllowed = parameter.allowed_values?.length && !parameter.allowed_values.includes(value);
                                event.target.setCustomValidity(invalidMinimum
                                  ? `Enter a value greater than ${parameter.min} ${parameter.unit}.`
                                  : invalidAllowed ? `Choose one of: ${parameter.allowed_values?.join(', ')}.` : '');
                                setFormValues((current) => ({ ...current, [parameter.name]: Number.isFinite(value) ? value : parameter.default }));
                                setEditedFields((current) => new Set(current).add(parameter.name));
                              }}
                            />
                          </label>
                        ))}
                      </div>

                      <fieldset className="cad-outputs">
                        <legend>OUTPUTS</legend>
                        {(['stl', 'glb', 'json'] as const).map((output) => (
                          <label key={output}>
                            <input type="checkbox" checked={outputs.includes(output)} onChange={() => toggleOutput(output)} />
                            <span>{output.toUpperCase()}</span>
                          </label>
                        ))}
                      </fieldset>

                      {formError && (
                        <ErrorHints
                          message={formError.message}
                          suggestions={formError.suggestions}
                          examples={formError.examples}
                          onChoose={chooseHint}
                        />
                      )}

                      <button type="submit" className="cad-generate-btn" disabled={isLoading || outputs.length === 0}>
                        {isLoading ? 'GENERATING…' : 'GENERATE PART'}
                        <span aria-hidden="true">↗</span>
                      </button>
                    </>
                  ) : (
                    <p className="catalog-status">No CAD parts are available yet.</p>
                  )}
                </form>
              )}
            </div>
          </div>
        </main>
      </div>
    </div>
  );
}
