/* eslint-disable react-hooks/set-state-in-effect */
'use client';

import { useEffect, useRef, useState } from 'react';
import * as THREE from 'three';
import { Canvas, useFrame } from '@react-three/fiber';
import { OrbitControls, Grid, Line } from '@react-three/drei';

function QuadcopterFrame({
  overall = 50,
  reduced = false,
}: {
  overall?: number;
  reduced?: boolean;
}) {
  const groupRef = useRef<THREE.Group>(null);
  const plate = 21;
  const thickness = 3.4;
  const armW = 4.6;
  const boss = 8;

  useFrame((_, delta) => {
    if (reduced) return;
    if (groupRef.current) groupRef.current.rotation.y += delta * 0.22;
  });

  const startR = (plate / 2) * Math.SQRT2;
  const endR = overall / 2;

  const arms = [45, 135, 225, 315].map((deg) => {
    const theta = (deg * Math.PI) / 180;
    const midR = (startR + endR) / 2;
    const len = endR - startR;
    return {
      x: midR * Math.cos(theta),
      y: midR * Math.sin(theta),
      rot: deg,
      len,
      mx: endR * Math.cos(theta),
      my: endR * Math.sin(theta),
    };
  });

  return (
    <group ref={groupRef}>
      {/* center plate */}
      <mesh position={[0, 0, 0]}>
        <boxGeometry args={[plate, plate, thickness]} />
        <meshStandardMaterial color="#F9F9F7" roughness={0.85} metalness={0.06} />
      </mesh>
      <lineSegments>
        <edgesGeometry args={[new THREE.BoxGeometry(plate, plate, thickness)]} />
        <lineBasicMaterial color="#1B1B1B" transparent opacity={0.22} />
      </lineSegments>

      {arms.map((a, i) => (
        <group
          key={i}
          position={[a.x, a.y, 0]}
          rotation={[0, 0, (a.rot * Math.PI) / 180]}
        >
          <mesh>
            <boxGeometry args={[a.len, armW, thickness]} />
            <meshStandardMaterial color="#EDEDE8" roughness={0.8} metalness={0.08} />
          </mesh>
          <lineSegments>
            <edgesGeometry args={[new THREE.BoxGeometry(a.len, armW, thickness)]} />
            <lineBasicMaterial color="#1B1B1B" transparent opacity={0.14} />
          </lineSegments>
        </group>
      ))}

      {arms.map((a, i) => (
        <group key={`boss-${i}`} position={[a.mx, a.my, thickness * 0.45]}>
          <mesh>
            <boxGeometry args={[boss, boss, thickness * 1.5]} />
            <meshStandardMaterial color="#DDE4E8" roughness={0.75} metalness={0.12} />
          </mesh>
          <lineSegments>
            <edgesGeometry args={[new THREE.BoxGeometry(boss, boss, thickness * 1.5)]} />
            <lineBasicMaterial color="#315B73" transparent opacity={0.35} />
          </lineSegments>
          {/* ring */}
          <mesh rotation={[Math.PI / 2, 0, 0]} position={[0, 0, thickness * 0.75]}>
            <torusGeometry args={[boss / 2, 0.18, 12, 32]} />
            <meshStandardMaterial color="#315B73" transparent opacity={0.9} />
          </mesh>
        </group>
      ))}

      {/* axes tiny */}
      <group position={[0, 0, thickness + 1]}>
        <Line points={[[0, 0, 0], [8, 0, 0]]} color="#B42318" lineWidth={1.2} />
        <Line points={[[0, 0, 0], [0, 8, 0]]} color="#2D6A4F" lineWidth={1.2} />
      </group>

      {/* dimension line overlay - subtle */}
      {!reduced && (
        <group>
          <Line
            points={[
              [-overall / 2, -overall / 2 - 4, 0],
              [overall / 2, -overall / 2 - 4, 0],
            ]}
            color="#315B73"
            lineWidth={1}
            dashed
            dashScale={2}
            gapSize={1}
          />
        </group>
      )}

      {/* floating coordinate labels as small spheres */}
      {arms.map((a, i) => (
        <mesh key={`motor-${i}`} position={[a.mx, a.my, thickness * 1.4]}>
          <sphereGeometry args={[0.45, 12, 12]} />
          <meshStandardMaterial color="#315B73" emissive="#315B73" emissiveIntensity={0.25} />
        </mesh>
      ))}
    </group>
  );
}

function SceneContent({ reduced }: { reduced: boolean }) {
  return (
    <>
      <ambientLight intensity={1.1} />
      <directionalLight position={[12, 18, 12]} intensity={1.2} color="#FFFFFF" />
      <directionalLight position={[-12, -8, 8]} intensity={0.35} color="#DDE8F0" />
      <QuadcopterFrame reduced={reduced} />
      <Grid
        position={[0, 0, -2.2]}
        args={[80, 80]}
        cellSize={4}
        cellThickness={0.4}
        sectionSize={16}
        sectionThickness={0.8}
        sectionColor="#D0D0CA"
        cellColor="#E8E8E2"
        fadeDistance={42}
        infiniteGrid
      />
      <OrbitControls
        enablePan={false}
        enableZoom={false}
        autoRotate={!reduced}
        autoRotateSpeed={0.45}
        minPolarAngle={Math.PI / 2.8}
        maxPolarAngle={Math.PI / 2.1}
        enableDamping
        dampingFactor={0.06}
        rotateSpeed={0.45}
      />
    </>
  );
}

export default function HeroScene() {
  const [reduced, setReduced] = useState(false);
  const [mounted, setMounted] = useState(false);
  const [webGLFailed, setWebGLFailed] = useState(false);

  useEffect(() => {
    setMounted(true);
    const mq = window.matchMedia('(prefers-reduced-motion: reduce)');
    setReduced(mq.matches);
    const onChange = (e: MediaQueryListEvent) => setReduced(e.matches);
    mq.addEventListener?.('change', onChange);

    // WebGL detection
    try {
      const c = document.createElement('canvas');
      const gl =
        c.getContext('webgl') || c.getContext('experimental-webgl');
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
          background: '#F1F1ED',
          display: 'grid',
          placeItems: 'center',
          fontFamily: 'var(--font-mono-plex), monospace',
          fontSize: '0.7rem',
          letterSpacing: '0.1em',
          color: '#707070',
        }}
      >
        LOADING GEOMETRY —
      </div>
    );
  }

  if (webGLFailed) {
    return <FallbackDiagram />;
  }

  return (
    <Canvas
      dpr={[1, 1.8]}
      camera={{ position: [42, -38, 28], fov: 34 }}
      gl={{ antialias: true, alpha: true }}
      onCreated={({ gl }) => {
        gl.setClearColor('#F1F1ED', 1);
      }}
      style={{ background: '#F1F1ED' }}
    >
      <SceneContent reduced={reduced} />
    </Canvas>
  );
}

function FallbackDiagram() {
  return (
    <div
      style={{
        width: '100%',
        height: '100%',
        background: '#F1F1ED',
        display: 'grid',
        placeItems: 'center',
        padding: 24,
        position: 'relative',
      }}
      role="img"
      aria-label="Technical diagram fallback — quadcopter frame top view"
    >
      <svg
        viewBox="0 0 300 300"
        style={{ width: '78%', height: '78%', maxWidth: 420 }}
        aria-hidden="true"
      >
        <rect x={1} y={1} width={298} height={298} fill="none" stroke="#D0D0CA" strokeWidth={0.8} />
        {/* grid */}
        {Array.from({ length: 6 }).map((_, i) => (
          <g key={i} opacity={0.35}>
            <line x1={50 + i * 40} y1={20} x2={50 + i * 40} y2={280} stroke="#D0D0CA" strokeWidth={0.5} />
            <line x1={20} y1={50 + i * 40} x2={280} y2={50 + i * 40} stroke="#D0D0CA" strokeWidth={0.5} />
          </g>
        ))}
        {/* plate */}
        <rect x={122} y={122} width={56} height={56} fill="#FFFFFF" stroke="#111111" strokeWidth={1.1} />
        {/* arms */}
        <g stroke="#111111" strokeWidth={1} fill="#EDEDE8">
          <rect x={150} y={70} width={14} height={52} transform="rotate(45 157 96)" />
          <rect x={150} y={70} width={14} height={52} transform="rotate(135 157 96)" />
          <rect x={150} y={70} width={14} height={52} transform="rotate(225 157 96)" />
          <rect x={150} y={70} width={14} height={52} transform="rotate(315 157 96)" />
        </g>
        {/* motors */}
        <g fill="#FFFFFF" stroke="#315B73" strokeWidth={1}>
          <rect x={62} y={62} width={20} height={20} transform="rotate(45 72 72)" />
          <rect x={218} y={62} width={20} height={20} transform="rotate(135 228 72)" />
          <rect x={62} y={218} width={20} height={20} transform="rotate(225 72 228)" />
          <rect x={218} y={218} width={20} height={20} transform="rotate(315 228 228)" />
        </g>
        <circle cx={72} cy={72} r={10} fill="none" stroke="#315B73" strokeWidth={0.9} strokeDasharray="3 3" />
        <circle cx={228} cy={72} r={10} fill="none" stroke="#315B73" strokeWidth={0.9} strokeDasharray="3 3" />
        <circle cx={72} cy={228} r={10} fill="none" stroke="#315B73" strokeWidth={0.9} strokeDasharray="3 3" />
        <circle cx={228} cy={228} r={10} fill="none" stroke="#315B73" strokeWidth={0.9} strokeDasharray="3 3" />
        {/* dim line */}
        <line x1={30} y1={270} x2={270} y2={270} stroke="#315B73" strokeWidth={0.8} />
        <line x1={30} y1={266} x2={30} y2={274} stroke="#315B73" strokeWidth={0.8} />
        <line x1={270} y1={266} x2={270} y2={274} stroke="#315B73" strokeWidth={0.8} />
        <text x={150} y={282} textAnchor="middle" fontFamily="monospace" fontSize={7} letterSpacing={1} fill="#315B73">
          50.00 MM
        </text>
        <text x={150} y={18} textAnchor="middle" fontFamily="monospace" fontSize={6} letterSpacing={1.2} fill="#707070">
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
          color: '#707070',
        }}
      >
        2D FALLBACK · WEBGL UNAVAILABLE
      </div>
    </div>
  );
}
