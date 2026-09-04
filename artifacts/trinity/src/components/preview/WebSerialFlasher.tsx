import React, { useState } from 'react';
import { Usb } from 'lucide-react';

type Target = 'esp32' | 'pico' | 'stm32';

const TARGETS: Record<Target, { label: string; baud: number; hint: string }> = {
  esp32: { label: 'ESP32 (esptool)', baud: 115200, hint: 'Hold BOOT, tap RESET, then flash.' },
  pico: { label: 'Raspberry Pi Pico', baud: 115200, hint: 'Hold BOOTSEL while connecting USB.' },
  stm32: { label: 'STM32 (ROM bootloader)', baud: 57600, hint: 'Set BOOT0 high for UART bootloader.' },
};

type Props = {
  firmwareUrl?: string;
  filename?: string;
};

/** Browser WebSerial flashing scaffold for supported MCUs (Chrome/Edge). */
export function WebSerialFlasher({ firmwareUrl, filename = 'firmware.bin' }: Props) {
  const [target, setTarget] = useState<Target>('esp32');
  const [status, setStatus] = useState<string>('idle');
  const [log, setLog] = useState<string[]>([]);

  const append = (line: string) => setLog((prev) => [...prev.slice(-8), line]);

  const connectAndFlash = async () => {
    if (!('serial' in navigator)) {
      setStatus('unsupported');
      append('WebSerial is not available in this browser. Use Chrome or Edge.');
      return;
    }
    if (!firmwareUrl) {
      setStatus('missing-artifact');
      append('Generate firmware first to obtain a flashable artifact URL.');
      return;
    }
    try {
      setStatus('connecting');
      // @ts-expect-error WebSerial is not in default TS lib here
      const port = await navigator.serial.requestPort();
      const meta = TARGETS[target];
      await port.open({ baudRate: meta.baud });
      setStatus('transferring');
      append(`Connected to ${meta.label} @ ${meta.baud} baud`);
      const response = await fetch(firmwareUrl);
      if (!response.ok) throw new Error(`Firmware download failed (${response.status})`);
      const firmware = await response.arrayBuffer();
      append(`Loaded ${filename} (${firmware.byteLength} bytes)`);
      append(meta.hint);
      append('Transfer protocol stub complete — integrate esptool-js / picotool for production flashing.');
      await port.close();
      setStatus('completed');
    } catch (err) {
      setStatus('error');
      append(String(err));
    }
  };

  return (
    <div className="rounded border border-white/10 bg-black/30 p-3 font-mono text-xs">
      <div className="mb-2 flex items-center gap-2 text-primary">
        <Usb className="h-3.5 w-3.5" />
        <span className="uppercase tracking-widest">WebSerial Flash</span>
        <span className="text-muted-foreground">status: {status}</span>
      </div>
      <div className="mb-2 flex flex-wrap gap-2">
        {(Object.keys(TARGETS) as Target[]).map((key) => (
          <button
            key={key}
            type="button"
            onClick={() => setTarget(key)}
            className={`rounded border px-2 py-1 ${target === key ? 'border-primary text-primary' : 'border-white/10 text-muted-foreground'}`}
          >
            {TARGETS[key].label}
          </button>
        ))}
      </div>
      <button type="button" onClick={() => void connectAndFlash()} className="rounded bg-primary/20 px-3 py-1 text-primary hover:bg-primary hover:text-primary-foreground">
        Request device & flash
      </button>
      {log.length > 0 && (
        <pre className="mt-3 max-h-32 overflow-auto whitespace-pre-wrap text-[10px] text-muted-foreground">{log.join('\n')}</pre>
      )}
    </div>
  );
}
