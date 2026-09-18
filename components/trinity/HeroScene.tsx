/* eslint-disable react-hooks/set-state-in-effect */
'use client';

import { useEffect, useMemo, useRef, useState } from 'react';
import * as THREE from 'three';
import { Canvas, useFrame } from '@react-three/fiber';
import { OrbitControls, Grid, Line, RoundedBox, Text, ContactShadows } from '@react-three/drei';

/* ------------------------------------------------------------
   Shared drone geometry constants — units are millimetres.
   Wheelbase (motor-to-motor diagonal) = 50.00 mm.
   ------------------------------------------------------------ */
const ARM_DEGS = [45, 135, 225, 315];
const PLATE = 21;
const THICKNESS = 3.0;
const ARM_W = 4.6;
const BOSS_R = 4.2;
const OVERALL = 50;
const MOTOR_R = 2.9;
const MOTOR_H = 4.2;
const PROP_R = 13;
const PROP_Z = THICKNESS * 1.1 + MOTOR_H / 2 + 2.6; // hub height above frame origin
const CANOPY = [10.5, 10.5, 2.8] as const;

function armLayout() {
  const startR = (PLATE / 2) * Math.SQRT2;
  const endR = OVERALL / 2;
  const midR = (startR + endR) / 2;
  const len = endR - startR;
  return ARM_DEGS.map((deg) => {
    const theta = (deg * Math.PI) / 180;
    return {
      deg,
      x: midR * Math.cos(theta),
      y: midR * Math.sin(theta),
      rot: deg,
      len,
      mx: endR * Math.cos(theta),
      my: endR * Math.sin(theta),
    };
  });
}

/* One motor assembly: bell, shaft, LED ring, landing skid. */
function MotorUnit({ x, y, led }: { x: number; y: number; led: string }) {
  return (
    <group position={[x, y, THICKNESS * 0.45]}>
      {/* graphite motor mount cylinder */}
      <mesh rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[BOSS_R, BOSS_R, THICKNESS * 1.35, 24]} />
        <meshStandardMaterial color="#2A2E2B" roughness={0.55} metalness={0.32} />
      </mesh>
      {/* top plate rim */}
      <mesh rotation={[Math.PI / 2, 0, 0]} position={[0, 0, THICKNESS * 0.68]}>
        <torusGeometry args={[BOSS_R, 0.22, 12, 32]} />
        <meshStandardMaterial color="#1B1B1B" roughness={0.5} metalness={0.2} />
      </mesh>
      {/* motor bell */}
      <mesh position={[0, 0, THICKNESS * 1.1]}>
        <cylinderGeometry args={[MOTOR_R, MOTOR_R, MOTOR_H, 24]} />
        <meshStandardMaterial color="#1B1B1B" roughness={0.35} metalness={0.45} />
      </mesh>
      {/* bell cooling fins */}
      {[0.28, -0.28].map((off) => (
        <mesh key={off} rotation={[Math.PI / 2, 0, 0]} position={[0, 0, THICKNESS * 1.1 + off]}>
          <torusGeometry args={[MOTOR_R + 0.08, 0.12, 8, 24]} />
          <meshStandardMaterial color="#3A3E3B" roughness={0.4} metalness={0.5} />
        </mesh>
      ))}
      {/* shaft */}
      <mesh position={[0, 0, THICKNESS * 1.1 + MOTOR_H / 2 + 0.9]}>
        <cylinderGeometry args={[1.0, 1.0, 2.2, 16]} />
        <meshStandardMaterial color="#C8C8C0" metalness={0.6} roughness={0.25} />
      </mesh>
      {/* mounting screws */}
      {[0, 120, 240].map((ang) => {
        const r = 2.6;
        const sx = Math.cos((ang * Math.PI) / 180) * r;
        const sy = Math.sin((ang * Math.PI) / 180) * r;
        return (
          <mesh key={ang} position={[sx, sy, THICKNESS * 0.68 + 0.35]}>
            <cylinderGeometry args={[0.45, 0.45, 0.5, 12]} />
            <meshStandardMaterial color="#111111" roughness={0.8} />
          </mesh>
        );
      })}
      {/* status LED ring under the boss */}
      <mesh rotation={[Math.PI / 2, 0, 0]} position={[0, 0, -THICKNESS * 0.72]}>
        <torusGeometry args={[BOSS_R - 0.7, 0.28, 8, 28]} />
        <meshStandardMaterial color={led} emissive={led} emissiveIntensity={1.6} toneMapped={false} />
      </mesh>
    </group>
  );
}

/* Spinning propeller: two pitched blades + hub + translucent rotor disc. */
function Propeller({ x, y, dir, speed, spin, reduced }: { x: number; y: number; dir: 1 | -1; speed: number; spin: boolean; reduced: boolean }) {
  const bladesRef = useRef<THREE.Group>(null);
  useFrame((_, delta) => {
    if (!bladesRef.current) return;
    if (reduced) {
      bladesRef.current.rotation.z = 0.6 * dir;
      return;
    }
    if (spin) bladesRef.current.rotation.z += delta * speed * dir;
  });
  return (
    <group position={[x, y, PROP_Z]}>
      {/* swept rotor disc */}
      <mesh rotation={[Math.PI / 2, 0, 0]}>
        <circleGeometry args={[PROP_R, 40]} />
        <meshBasicMaterial color="#315B73" transparent opacity={spin && !reduced ? 0.055 : 0.02} side={THREE.DoubleSide} depthWrite={false} />
      </mesh>
      <mesh rotation={[Math.PI / 2, 0, 0]}>
        <ringGeometry args={[PROP_R - 0.35, PROP_R, 48]} />
        <meshBasicMaterial color="#315B73" transparent opacity={spin && !reduced ? 0.30 : 0.12} side={THREE.DoubleSide} depthWrite={false} />
      </mesh>
      <group ref={bladesRef}>
        {/* hub */}
        <mesh rotation={[Math.PI / 2, 0, 0]}>
          <cylinderGeometry args={[1.5, 1.3, 1.0, 16]} />
          <meshStandardMaterial color="#1B1B1B" roughness={0.4} metalness={0.5} />
        </mesh>
        {[0, Math.PI].map((a) => (
          <group key={a} rotation={[0, 0, a]}>
            <RoundedBox args={[PROP_R - 1, 1.7, 0.34]} radius={0.14} smoothness={2} position={[PROP_R / 2, 0, 0.15]} rotation={[0.20 * dir, 0, 0]}>
              <meshStandardMaterial color="#F4F4F1" roughness={0.5} metalness={0.05} transparent opacity={0.96} />
            </RoundedBox>
            {/* blade tip marker */}
            <mesh position={[PROP_R - 1.6, 0, 0.15]}>
              <boxGeometry args={[1.2, 1.7, 0.36]} />
              <meshStandardMaterial color="#315B73" roughness={0.5} />
            </mesh>
          </group>
        ))}
      </group>
    </group>
  );
}

/* Full drone = frame + canopy/battery + motors + props + dimensions. */
function Drone({
  overall = OVERALL,
  reduced = false,
  spin = true,
}: {
  overall?: number;
  reduced?: boolean;
  spin?: boolean;
}) {
  const hoverRef = useRef<THREE.Group>(null);
  const yawRef = useRef<THREE.Group>(null);
  const t = useRef(0);

  useFrame((_, delta) => {
    if (reduced) return;
    t.current += delta;
    const time = t.current;
    if (hoverRef.current) {
      // gentle hover bob + attitude sway
      hoverRef.current.position.z = Math.sin(time * 1.15) * 1.15 + 0.6;
      hoverRef.current.rotation.x = Math.sin(time * 0.7) * 0.028;
      hoverRef.current.rotation.y = Math.cos(time * 0.55) * 0.028;
    }
    if (yawRef.current) yawRef.current.rotation.y += delta * 0.11;
  });

  const arms = useMemo(() => armLayout(), []);
  const propSpeed = 26;

  return (
    <group ref={hoverRef}>
      <group ref={yawRef}>
        {/* center plate — rounded, matte white with subtle graphite edge */}
        <RoundedBox args={[PLATE, PLATE, THICKNESS]} radius={1.2} smoothness={4} position={[0, 0, 0]}>
          <meshStandardMaterial color="#F4F4F1" roughness={0.42} metalness={0.14} />
        </RoundedBox>
        <lineSegments>
          <edgesGeometry args={[new THREE.BoxGeometry(PLATE, PLATE, THICKNESS)]} />
          <lineBasicMaterial color="#1B1B1B" transparent opacity={0.10} />
        </lineSegments>
        {/* central hub ring */}
        <mesh rotation={[Math.PI / 2, 0, 0]} position={[0, 0, THICKNESS / 2 + 0.2]}>
          <ringGeometry args={[3.2, 3.6, 32]} />
          <meshStandardMaterial color="#D0D0CA" side={THREE.DoubleSide} transparent opacity={0.9} />
        </mesh>
        {/* diagonal stitch lines */}
        <Line points={[[-PLATE / 2, -PLATE / 2, THICKNESS / 2 + 0.01], [PLATE / 2, PLATE / 2, THICKNESS / 2 + 0.01]]} color="#D0D0CA" lineWidth={0.7} transparent opacity={0.6} />
        <Line points={[[-PLATE / 2, PLATE / 2, THICKNESS / 2 + 0.01], [PLATE / 2, -PLATE / 2, THICKNESS / 2 + 0.01]]} color="#D0D0CA" lineWidth={0.7} transparent opacity={0.6} />

        {/* arms */}
        {arms.map((a, i) => (
          <group key={i} position={[a.x, a.y, 0]} rotation={[0, 0, (a.rot * Math.PI) / 180]}>
            <RoundedBox args={[a.len, ARM_W, THICKNESS]} radius={0.7} smoothness={3}>
              <meshStandardMaterial color="#E9E9E4" roughness={0.48} metalness={0.10} />
            </RoundedBox>
            <lineSegments>
              <edgesGeometry args={[new THREE.BoxGeometry(a.len, ARM_W, THICKNESS)]} />
              <lineBasicMaterial color="#1B1B1B" transparent opacity={0.08} />
            </lineSegments>
            <mesh position={[0, 0, THICKNESS / 2 + 0.02]}>
              <planeGeometry args={[a.len - 6, 0.7]} />
              <meshStandardMaterial color="#D0D0CA" transparent opacity={0.55} />
            </mesh>
          </group>
        ))}

        {/* motors + LEDs — front pair red, rear pair green */}
        {arms.map((a, i) => (
          <MotorUnit key={`motor-${i}`} x={a.mx} y={a.my} led={i < 2 ? '#E5484D' : '#30C48D'} />
        ))}

        {/* propellers — CW / CCW alternating */}
        {arms.map((a, i) => (
          <Propeller
            key={`prop-${i}`}
            x={a.mx}
            y={a.my}
            dir={i % 2 === 0 ? 1 : -1}
            speed={propSpeed + i * 1.6}
            spin={spin}
            reduced={reduced}
          />
        ))}

        {/* canopy / flight-stack cover */}
        <group position={[0, 0, THICKNESS / 2 + CANOPY[2] / 2 + 0.15]}>
          <RoundedBox args={[CANOPY[0], CANOPY[1], CANOPY[2]]} radius={0.9} smoothness={3}>
            <meshStandardMaterial color="#1B1B1B" roughness={0.38} metalness={0.28} />
          </RoundedBox>
          {/* blue accent stripe */}
          <mesh position={[0, 0, CANOPY[2] / 2 + 0.011]}>
            <planeGeometry args={[CANOPY[0] - 2.4, 1.1]} />
            <meshStandardMaterial color="#315B73" roughness={0.4} emissive="#315B73" emissiveIntensity={0.25} />
          </mesh>
          {/* front camera lens */}
          <mesh position={[0, CANOPY[1] / 2 - 1.4, CANOPY[2] / 2 + 0.35]} rotation={[Math.PI / 2.6, 0, 0]}>
            <cylinderGeometry args={[1.35, 1.35, 0.7, 20]} />
            <meshStandardMaterial color="#0B0B0B" roughness={0.15} metalness={0.6} />
          </mesh>
          <mesh position={[0, CANOPY[1] / 2 - 1.4, CANOPY[2] / 2 + 0.72]} rotation={[Math.PI / 2.6, 0, 0]}>
            <cylinderGeometry args={[0.75, 0.75, 0.12, 16]} />
            <meshStandardMaterial color="#8FB6C9" metalness={0.9} roughness={0.1} emissive="#315B73" emissiveIntensity={0.4} />
          </mesh>
        </group>
        {/* battery pack under the plate */}
        <group position={[0, 0, -THICKNESS / 2 - 0.9]}>
          <RoundedBox args={[9.5, 6.5, 1.8]} radius={0.5} smoothness={3}>
            <meshStandardMaterial color="#2A2E2B" roughness={0.55} metalness={0.2} />
          </RoundedBox>
          <mesh position={[0, 0, -0.92]}>
            <planeGeometry args={[9.5, 0.9]} />
            <meshStandardMaterial color="#C8C8C0" roughness={0.6} />
          </mesh>
        </group>

        {/* axes — subtle */}
        <group position={[0, 0, THICKNESS + 1.2]}>
          <Line points={[[0, 0, 0], [7, 0, 0]]} color="#8A8A82" lineWidth={1} />
          <Line points={[[0, 0, 0], [0, 7, 0]]} color="#8A8A82" lineWidth={1} />
          <Text position={[8.2, 0, 0]} fontSize={1.2} color="#8A8A82" anchorX="left" anchorY="middle">
            X
          </Text>
          <Text position={[0, 8.2, 0]} fontSize={1.2} color="#8A8A82" anchorX="center" anchorY="bottom">
            Y
          </Text>
        </group>

        {/* engineering dimensions */}
        {!reduced && (
          <group>
            {/* wheelbase — diagonal between motor centers */}
            <Line points={[[-overall / 2 / Math.SQRT2, -overall / 2 / Math.SQRT2, 9.6], [overall / 2 / Math.SQRT2, overall / 2 / Math.SQRT2, 9.6]]} color="#315B73" lineWidth={1.1} />
            <Line points={[[-overall / 2 / Math.SQRT2, -overall / 2 / Math.SQRT2, 8.6], [-overall / 2 / Math.SQRT2, -overall / 2 / Math.SQRT2, 10.6]]} color="#315B73" lineWidth={1.4} />
            <Line points={[[overall / 2 / Math.SQRT2, overall / 2 / Math.SQRT2, 8.6], [overall / 2 / Math.SQRT2, overall / 2 / Math.SQRT2, 10.6]]} color="#315B73" lineWidth={1.4} />
            <Text
              position={[0, 0, 10.9]}
              rotation={[0, 0, Math.PI / 4]}
              fontSize={1.9}
              color="#315B73"
              anchorX="center"
              anchorY="bottom"
              outlineWidth={0.04}
              outlineColor="#FFFFFF"
              letterSpacing={0.05}
            >
              50.00 mm
            </Text>

            {/* propeller diameter on the 45° motor */}
            <group position={[arms[0].mx, arms[0].my, PROP_Z + 1.6]} rotation={[0, 0, Math.PI / 4]}>
              <Line points={[[-PROP_R, 0, 0], [PROP_R, 0, 0]]} color="#6D8998" lineWidth={1} />
              <Line points={[[-PROP_R, -1, 0], [-PROP_R, 1, 0]]} color="#6D8998" lineWidth={1.2} />
              <Line points={[[PROP_R, -1, 0], [PROP_R, 1, 0]]} color="#6D8998" lineWidth={1.2} />
              <Text position={[0, 0, 1.4]} fontSize={1.5} color="#6D8998" anchorX="center" anchorY="bottom" outlineWidth={0.03} outlineColor="#FFFFFF" letterSpacing={0.04}>
                Ø 26.00
              </Text>
            </group>
          </group>
        )}
      </group>
    </group>
  );
}

function SceneContent({ reduced, spin }: { reduced: boolean; spin: boolean }) {
  return (
    <>
      <ambientLight intensity={0.92} />
      <directionalLight position={[14, 18, 16]} intensity={1.05} color="#FFFFFF" castShadow={false} />
      <directionalLight position={[-12, -8, 10]} intensity={0.32} color="#DDE8F0" />
      <Drone reduced={reduced} spin={spin} />
      <Grid
        position={[0, 0, -2.4]}
        args={[80, 80]}
        cellSize={8}
        cellThickness={0.45}
        sectionSize={16}
        sectionThickness={0.9}
        sectionColor="#C8C8C0"
        cellColor="#E6E6E0"
        fadeDistance={52}
        infiniteGrid
      />
      <ContactShadows position={[0, 0, -2.2]} opacity={0.20} scale={70} blur={2.2} far={12} color="#111111" />
      <OrbitControls
        enablePan={false}
        enableZoom={false}
        autoRotate={false}
        minPolarAngle={Math.PI / 2.8}
        maxPolarAngle={Math.PI / 2.05}
        enableDamping
        dampingFactor={0.07}
        rotateSpeed={0.38}
      />
    </>
  );
}

export default function HeroScene() {
  const [reduced, setReduced] = useState(false);
  const [spin, setSpin] = useState(true);
  const [mounted, setMounted] = useState(false);
  const [webGLFailed, setWebGLFailed] = useState(false);

  useEffect(() => {
    setMounted(true);
    const mq = window.matchMedia('(prefers-reduced-motion: reduce)');
    setReduced(mq.matches);
    const onChange = (e: MediaQueryListEvent) => {
      setReduced(e.matches);
      if (e.matches) setSpin(false);
    };
    mq.addEventListener?.('change', onChange);

    try {
      const c = document.createElement('canvas');
      const gl = c.getContext('webgl') || c.getContext('experimental-webgl');
      if (!gl) setWebGLFailed(true);
    } catch {
      setWebGLFailed(true);
    }
    return () => mq.removeEventListener?.('change', onChange);
  }, []);

  if (!mounted) {
    return (
      <div
        style={{
          width: '100%',
          height: '100%',
          background: '#FFFFFF',
          display: 'grid',
          placeItems: 'center',
          fontFamily: 'var(--font-mono-plex), monospace',
          fontSize: '0.68rem',
          letterSpacing: '0.12em',
          color: '#5A5A56',
        }}
      >
        INITIALIZING GEOMETRY —
      </div>
    );
  }

  if (webGLFailed) {
    return <FallbackDiagram />;
  }

  return (
    <>
      <Canvas
        dpr={[1, 1.4]}
        camera={{ position: [68, -63, 50], fov: 28 }}
        gl={{ antialias: true, alpha: true, powerPreference: 'high-performance' }}
        frameloop={reduced || !spin ? 'demand' : 'always'}
        onCreated={({ gl }) => {
          gl.setClearColor('#FFFFFF', 1);
        }}
        style={{ background: '#FFFFFF' }}
      >
        <SceneContent reduced={reduced} spin={spin} />
      </Canvas>
      {/* props on/off toggle lives outside the canvas so it stays keyboard-accessible */}
      <button
        type="button"
        className="stage-spin-toggle"
        onClick={() => setSpin((s) => !s)}
        aria-pressed={spin}
        aria-label={spin ? 'Stop propellers' : 'Start propellers'}
      >
        {spin ? '◉ PROPS ON' : '◎ PROPS OFF'}
      </button>
    </>
  );
}

function FallbackDiagram() {
  return (
    <div
      style={{
        width: '100%',
        height: '100%',
        background: '#FFFFFF',
        display: 'grid',
        placeItems: 'center',
        padding: 24,
        position: 'relative',
      }}
      role="img"
      aria-label="Technical diagram fallback — quadcopter drone top view"
    >
      <svg viewBox="0 0 300 300" style={{ width: '78%', height: '78%', maxWidth: 420 }} aria-hidden="true">
        <rect x={1} y={1} width={298} height={298} fill="none" stroke="#D0D0CA" strokeWidth={0.8} />
        {Array.from({ length: 6 }).map((_, i) => (
          <g key={i} opacity={0.35}>
            <line x1={50 + i * 40} y1={20} x2={50 + i * 40} y2={280} stroke="#D0D0CA" strokeWidth={0.5} />
            <line x1={20} y1={50 + i * 40} x2={280} y2={50 + i * 40} stroke="#D0D0CA" strokeWidth={0.5} />
          </g>
        ))}
        <rect x={122} y={122} width={56} height={56} rx={3} fill="#F4F4F1" stroke="#1B1B1B" strokeWidth={1} />
        <g stroke="#1B1B1B" strokeWidth={0.9} fill="#E9E9E4">
          <rect x={150} y={70} width={14} height={52} rx={1.2} transform="rotate(45 157 96)" />
          <rect x={150} y={70} width={14} height={52} rx={1.2} transform="rotate(135 157 96)" />
          <rect x={150} y={70} width={14} height={52} rx={1.2} transform="rotate(225 157 96)" />
          <rect x={150} y={70} width={14} height={52} rx={1.2} transform="rotate(315 157 96)" />
        </g>
        <g fill="#2A2E2B" stroke="#1B1B1B" strokeWidth={1}>
          <circle cx={72} cy={72} r={10} />
          <circle cx={228} cy={72} r={10} />
          <circle cx={72} cy={228} r={10} />
          <circle cx={228} cy={228} r={10} />
        </g>
        <g stroke="#315B73" strokeWidth={0.7} fill="none" opacity={0.7}>
          <circle cx={72} cy={72} r={19} />
          <circle cx={228} cy={72} r={19} />
          <circle cx={72} cy={228} r={19} />
          <circle cx={228} cy={228} r={19} />
        </g>
        <circle cx={72} cy={72} r={4} fill="#1B1B1B" />
        <circle cx={228} cy={72} r={4} fill="#1B1B1B" />
        <circle cx={72} cy={228} r={4} fill="#1B1B1B" />
        <circle cx={228} cy={228} r={4} fill="#1B1B1B" />
        <rect x={141} y={141} width={18} height={18} rx={2} fill="#1B1B1B" />
        <line x1={30} y1={270} x2={270} y2={270} stroke="#315B73" strokeWidth={1} />
        <line x1={30} y1={266} x2={30} y2={274} stroke="#315B73" strokeWidth={1} />
        <line x1={270} y1={266} x2={270} y2={274} stroke="#315B73" strokeWidth={1} />
        <text x={150} y={282} textAnchor="middle" fontFamily="monospace" fontSize={7} letterSpacing={1} fill="#315B73">
          50.00 mm
        </text>
        <text x={150} y={18} textAnchor="middle" fontFamily="monospace" fontSize={6} letterSpacing={1.2} fill="#5A5A56">
          TRINITY / GEOMETRY ENGINE
        </text>
      </svg>
      <div
        style={{
          position: 'absolute',
          bottom: 18,
          right: 18,
          fontFamily: 'var(--font-mono-plex), monospace',
          fontSize: '0.58rem',
          letterSpacing: '0.08em',
          color: '#5A5A56',
        }}
      >
        2D FALLBACK · WEBGL UNAVAILABLE
      </div>
    </div>
  );
}
