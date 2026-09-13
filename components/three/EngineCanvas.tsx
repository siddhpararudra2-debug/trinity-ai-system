'use client';

import { useEffect, useRef } from 'react';

/**
 * Hero 3D stage: a slowly rotating wireframe of Trinity's flagship artifact —
 * the parametric quadcopter frame (center plate, X-configuration arms, motor
 * mounts) — projected from pure math onto a 2D canvas. No WebGL, no deps:
 * the same spirit as the backend's dependency-free mesh kernel.
 */

type V3 = [number, number, number];
type Edge = [number, number];

type WireBox = { verts: V3[]; edges: Edge[] };

const ACCENT: [number, number, number] = [79, 214, 229];
const VIOLET: [number, number, number] = [139, 150, 255];
const PALE: [number, number, number] = [190, 214, 224];

// Frame proportions mirror app.engines.cad (default IR, mm).
const OVERALL = 50;
const PLATE = 21;
const THICK = 3.4;
const ARM_W = 4.6;
const BOSS = 8;
const ARM_ANGLES = [45, 135, 225, 315];

function box(center: V3, size: V3, rotZDeg = 0): WireBox {
  const [cx, cy, cz] = center;
  const [hx, hy, hz] = [size[0] / 2, size[1] / 2, size[2] / 2];
  const corners: V3[] = [
    [-hx, -hy, -hz], [hx, -hy, -hz], [hx, hy, -hz], [-hx, hy, -hz],
    [-hx, -hy, hz], [hx, -hy, hz], [hx, hy, hz], [-hx, hy, hz],
  ];
  const t = (rotZDeg * Math.PI) / 180;
  const cos = Math.cos(t);
  const sin = Math.sin(t);
  const verts = corners.map(
    ([x, y, z]) => [x * cos - y * sin + cx, x * sin + y * cos + cy, z + cz] as V3,
  );
  const edges: Edge[] = [
    [0, 1], [1, 2], [2, 3], [3, 0],
    [4, 5], [5, 6], [6, 7], [7, 4],
    [0, 4], [1, 5], [2, 6], [3, 7],
  ];
  return { verts, edges };
}

type Model = {
  parts: { box: WireBox; color: [number, number, number] }[];
  rings: { center: V3; radius: number; segments: number }[];
  arms: { from: V3; to: V3 }[];
  labels: { pos: V3; text: string }[];
};

function buildModel(): Model {
  const parts: Model['parts'] = [];
  const rings: Model['rings'] = [];
  const arms: Model['arms'] = [];
  const labels: Model['labels'] = [];

  // Center plate.
  parts.push({ box: box([0, 0, 0], [PLATE, PLATE, THICK]), color: ACCENT });

  const startR = (PLATE / 2) * Math.SQRT2;
  const endR = OVERALL / 2;
  const armLen = endR - startR;

  ARM_ANGLES.forEach((angleDeg, i) => {
    const theta = (angleDeg * Math.PI) / 180;
    const midR = (startR + endR) / 2;
    const armCenter: V3 = [midR * Math.cos(theta), midR * Math.sin(theta), 0];
    const motor: V3 = [endR * Math.cos(theta), endR * Math.sin(theta), 0];

    parts.push({
      box: box(armCenter, [armLen, ARM_W, THICK], angleDeg),
      color: PALE,
    });
    // Motor mount boss (raised block under each motor, mirroring the builder).
    parts.push({
      box: box([motor[0], motor[1], THICK * 0.65], [BOSS, BOSS, THICK * 1.6], angleDeg),
      color: VIOLET,
    });
    rings.push({ center: [motor[0], motor[1], THICK * 1.5], radius: BOSS / 2, segments: 16 });
    arms.push({ from: [startR * Math.cos(theta), startR * Math.sin(theta), 0], to: motor });
    labels.push({
      pos: [motor[0], motor[1], THICK * 3.4],
      text: `MOTOR 0${i + 1}`,
    });
  });

  return { parts, rings, arms, labels };
}

export default function EngineCanvas() {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    const model = buildModel();
    const modelEdges: { a: V3; b: V3; color: [number, number, number] }[] = [];
    model.parts.forEach(({ box: b, color }) => {
      b.edges.forEach(([i, j]) => modelEdges.push({ a: b.verts[i], b: b.verts[j], color }));
    });

    let width = 1;
    let height = 1;
    let dpr = 1;
    let raf = 0;
    const pointer = { x: 0, y: 0, tx: 0, ty: 0, active: false };

    // Background particle field (deterministic seed, like the repo's RNG use).
    let seed = 4451;
    const random = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
    const particles = Array.from({ length: 46 }, () => ({
      nx: 0.05 + random() * 0.9,
      ny: 0.06 + random() * 0.88,
      ox: random() * Math.PI * 2,
      oy: random() * Math.PI * 2,
      speed: 0.16 + random() * 0.4,
      size: 0.7 + random() * 1.5,
    }));

    const resize = () => {
      const rect = canvas.parentElement?.getBoundingClientRect();
      width = Math.max(1, rect?.width ?? 300);
      height = Math.max(1, rect?.height ?? 300);
      dpr = Math.min(window.devicePixelRatio || 1, 2);
      canvas.width = Math.round(width * dpr);
      canvas.height = Math.round(height * dpr);
      canvas.style.width = `${width}px`;
      canvas.style.height = `${height}px`;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      if (reducedMotion) draw(1600);
    };

    const onPointerMove = (e: PointerEvent) => {
      const rect = canvas.getBoundingClientRect();
      pointer.tx = ((e.clientX - rect.left) / rect.width - 0.5) * 2;
      pointer.ty = ((e.clientY - rect.top) / rect.height - 0.5) * 2;
      pointer.active = true;
    };
    const onPointerLeave = () => {
      pointer.active = false;
      pointer.tx = 0;
      pointer.ty = 0;
    };

    const project = (v: V3, yaw: number, pitch: number, scale: number): [number, number, number] => {
      const cosY = Math.cos(yaw);
      const sinY = Math.sin(yaw);
      const x1 = v[0] * cosY - v[1] * sinY;
      const z1 = v[0] * sinY + v[1] * cosY;
      const cosP = Math.cos(pitch);
      const sinP = Math.sin(pitch);
      const y2 = v[2] * cosP - z1 * sinP;
      const z2 = v[2] * sinP + z1 * cosP;
      const persp = 210;
      const f = persp / (persp + z2 + 60);
      return [width / 2 + x1 * scale * f, height / 2 + 8 + y2 * scale * f, z2];
    };

    const glowDot = (x: number, y: number, r: number, color: [number, number, number], alpha: number) => {
      const g = ctx.createRadialGradient(x, y, 0, x, y, r);
      g.addColorStop(0, `rgba(255,255,255,${alpha})`);
      g.addColorStop(0.3, `rgba(${color[0]},${color[1]},${color[2]},${alpha * 0.8})`);
      g.addColorStop(1, `rgba(${color[0]},${color[1]},${color[2]},0)`);
      ctx.fillStyle = g;
      ctx.beginPath();
      ctx.arc(x, y, r, 0, Math.PI * 2);
      ctx.fill();
    };

    const draw = (timeMs: number) => {
      const t = timeMs * 0.001;
      pointer.x += (pointer.tx - pointer.x) * 0.06;
      pointer.y += (pointer.ty - pointer.y) * 0.06;
      ctx.clearRect(0, 0, width, height);

      // --- drifting particle field -------------------------------------
      const pts = particles.map((p) => ({
        x: p.nx * width + Math.sin(t * p.speed + p.ox) * 8,
        y: p.ny * height + Math.cos(t * p.speed * 0.85 + p.oy) * 8,
      }));
      ctx.lineWidth = 0.7;
      const threshold = width < 640 ? 80 : 110;
      for (let i = 0; i < pts.length; i++) {
        for (let j = i + 1; j < pts.length; j++) {
          const dx = pts[i].x - pts[j].x;
          const dy = pts[i].y - pts[j].y;
          const dist = Math.hypot(dx, dy);
          if (dist < threshold) {
            const alpha = (1 - dist / threshold) * 0.14;
            ctx.strokeStyle = `rgba(146,180,199,${alpha})`;
            ctx.beginPath();
            ctx.moveTo(pts[i].x, pts[i].y);
            ctx.lineTo(pts[j].x, pts[j].y);
            ctx.stroke();
          }
        }
      }
      pts.forEach((p, i) => {
        ctx.fillStyle = i % 7 === 0 ? 'rgba(230,238,244,0.7)' : 'rgba(79,214,229,0.45)';
        ctx.beginPath();
        ctx.arc(p.x, p.y, particles[i].size, 0, Math.PI * 2);
        ctx.fill();
      });

      // --- 3D wireframe frame -------------------------------------------
      const yaw = 0.62 + t * 0.32 + pointer.x * 0.5;
      const pitch = -0.52 + pointer.y * 0.3;
      const scale = Math.min(width, height) / (OVERALL * 1.85);

      modelEdges.forEach(({ a, b, color }) => {
        const pa = project(a, yaw, pitch, scale);
        const pb = project(b, yaw, pitch, scale);
        const depth = ((pa[2] + pb[2]) / 2 + OVERALL / 2) / OVERALL; // 0..1
        const alpha = 0.22 + depth * 0.55;
        ctx.strokeStyle = `rgba(${color[0]},${color[1]},${color[2]},${alpha})`;
        ctx.lineWidth = depth > 0.55 ? 1.4 : 0.9;
        ctx.beginPath();
        ctx.moveTo(pa[0], pa[1]);
        ctx.lineTo(pb[0], pb[1]);
        ctx.stroke();
      });

      // Motor mount rings.
      model.rings.forEach((ring) => {
        ctx.beginPath();
        for (let s = 0; s <= ring.segments; s++) {
          const a = (s / ring.segments) * Math.PI * 2;
          const v: V3 = [
            ring.center[0] + Math.cos(a) * ring.radius,
            ring.center[1] + Math.sin(a) * ring.radius,
            ring.center[2],
          ];
          const p = project(v, yaw, pitch, scale);
          if (s === 0) ctx.moveTo(p[0], p[1]);
          else ctx.lineTo(p[0], p[1]);
        }
        ctx.strokeStyle = 'rgba(139,150,255,0.75)';
        ctx.lineWidth = 1.1;
        ctx.stroke();
      });

      // Pulses travelling from plate to motors (the generation pass).
      model.arms.forEach((arm, i) => {
        const u = (t * 0.32 + i / model.arms.length) % 1;
        const v: V3 = [
          arm.from[0] + (arm.to[0] - arm.from[0]) * u,
          arm.from[1] + (arm.to[1] - arm.from[1]) * u,
          0,
        ];
        const p = project(v, yaw, pitch, scale);
        glowDot(p[0], p[1], 12, ACCENT, 0.9);
      });

      // Sweeping dots around each motor ring.
      model.rings.forEach((ring, i) => {
        const a = t * 1.4 + (i * Math.PI) / 2;
        const v: V3 = [
          ring.center[0] + Math.cos(a) * ring.radius,
          ring.center[1] + Math.sin(a) * ring.radius,
          ring.center[2],
        ];
        const p = project(v, yaw, pitch, scale);
        glowDot(p[0], p[1], 7, VIOLET, 0.8);
      });

      // Telemetry labels.
      ctx.font = '600 8px ui-monospace, SFMono-Regular, Menlo, monospace';
      ctx.fillStyle = 'rgba(230,238,244,0.55)';
      model.labels.forEach((label) => {
        const p = project(label.pos, yaw, pitch, scale);
        ctx.fillText(label.text, p[0] + 8, p[1] - 6);
      });
      const origin = project([0, 0, -6], yaw, pitch, scale);
      ctx.fillStyle = 'rgba(79,214,229,0.8)';
      ctx.fillText('IR / QUADCOPTER_FRAME', origin[0] - 26, origin[1] + 18);

      if (!reducedMotion) raf = requestAnimationFrame(draw);
    };

    const ro = new ResizeObserver(resize);
    if (canvas.parentElement) ro.observe(canvas.parentElement);
    canvas.addEventListener('pointermove', onPointerMove);
    canvas.addEventListener('pointerleave', onPointerLeave);
    resize();
    if (!reducedMotion) raf = requestAnimationFrame(draw);

    const onVisibility = () => {
      if (document.hidden) cancelAnimationFrame(raf);
      else if (!reducedMotion) raf = requestAnimationFrame(draw);
    };
    document.addEventListener('visibilitychange', onVisibility);

    return () => {
      cancelAnimationFrame(raf);
      ro.disconnect();
      canvas.removeEventListener('pointermove', onPointerMove);
      canvas.removeEventListener('pointerleave', onPointerLeave);
      document.removeEventListener('visibilitychange', onVisibility);
    };
  }, []);

  return <canvas ref={canvasRef} className="stage-canvas" aria-hidden="true" />;
}
