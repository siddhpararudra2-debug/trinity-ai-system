/* eslint-disable react-hooks/set-state-in-effect */
'use client';

import { useEffect, useRef, useState } from 'react';
import * as THREE from 'three';
import { Canvas, useFrame } from '@react-three/fiber';
import { OrbitControls, Grid, Line, RoundedBox, Text, ContactShadows } from '@react-three/drei';

function QuadcopterFrame({
  overall = 50,
  reduced = false,
}: {
  overall?: number;
  reduced?: boolean;
}) {
  const groupRef = useRef<THREE.Group>(null);
  const plate = 21;
  const thickness = 3.0;
  const armW = 4.6;
  const bossR = 4.2;

  useFrame((_, delta) => {
    if (reduced) return;
    if (groupRef.current) groupRef.current.rotation.y += delta * 0.11;
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
      {/* center plate — rounded, matte white with subtle graphite edge */}
      <RoundedBox args={[plate, plate, thickness]} radius={1.2} smoothness={4} position={[0, 0, 0]}>
        <meshStandardMaterial color="#F4F4F1" roughness={0.42} metalness={0.14} />
      </RoundedBox>
      <lineSegments>
        <edgesGeometry args={[new THREE.BoxGeometry(plate, plate, thickness)]} />
        <lineBasicMaterial color="#1B1B1B" transparent opacity={0.10} />
      </lineSegments>
      {/* central hub ring */}
      <mesh rotation={[Math.PI / 2, 0, 0]} position={[0, 0, thickness / 2 + 0.2]}>
        <ringGeometry args={[3.2, 3.6, 32]} />
        <meshStandardMaterial color="#D0D0CA" side={THREE.DoubleSide} transparent opacity={0.9} />
      </mesh>
      {/* diagonal stitch lines */}
      <Line points={[[-plate / 2, -plate / 2, thickness / 2 + 0.01], [plate / 2, plate / 2, thickness / 2 + 0.01]]} color="#D0D0CA" lineWidth={0.7} transparent opacity={0.6} />
      <Line points={[[-plate / 2, plate / 2, thickness / 2 + 0.01], [plate / 2, -plate / 2, thickness / 2 + 0.01]]} color="#D0D0CA" lineWidth={0.7} transparent opacity={0.6} />

      {arms.map((a, i) => (
        <group key={i} position={[a.x, a.y, 0]} rotation={[0, 0, (a.rot * Math.PI) / 180]}>
          <RoundedBox args={[a.len, armW, thickness]} radius={0.7} smoothness={3}>
            <meshStandardMaterial color="#E9E9E4" roughness={0.48} metalness={0.10} />
          </RoundedBox>
          <lineSegments>
            <edgesGeometry args={[new THREE.BoxGeometry(a.len, armW, thickness)]} />
            <lineBasicMaterial color="#1B1B1B" transparent opacity={0.08} />
          </lineSegments>
          {/* arm center groove */}
          <mesh position={[0, 0, thickness / 2 + 0.02]}>
            <planeGeometry args={[a.len - 6, 0.7]} />
            <meshStandardMaterial color="#D0D0CA" transparent opacity={0.55} />
          </mesh>
        </group>
      ))}

      {arms.map((a, i) => (
        <group key={`boss-${i}`} position={[a.mx, a.my, thickness * 0.45]}>
          {/* graphite motor mount cylinder */}
          <mesh rotation={[Math.PI / 2, 0, 0]}>
            <cylinderGeometry args={[bossR, bossR, thickness * 1.35, 24]} />
            <meshStandardMaterial color="#2A2E2B" roughness={0.55} metalness={0.32} />
          </mesh>
          {/* top plate rim */}
          <mesh rotation={[Math.PI / 2, 0, 0]} position={[0, 0, thickness * 0.68]}>
            <torusGeometry args={[bossR, 0.22, 12, 32]} />
            <meshStandardMaterial color="#1B1B1B" roughness={0.5} metalness={0.2} />
          </mesh>
          {/* motor cylinder */}
          <mesh position={[0, 0, thickness * 1.1]}>
            <cylinderGeometry args={[2.9, 2.9, 4.2, 20]} />
            <meshStandardMaterial color="#1B1B1B" roughness={0.35} metalness={0.45} />
          </mesh>
          <mesh position={[0, 0, thickness * 1.1 + 2.4]}>
            <cylinderGeometry args={[1.0, 1.0, 1.6, 16]} />
            <meshStandardMaterial color="#C8C8C0" metalness={0.6} roughness={0.25} />
          </mesh>
          {/* 3 screw holes representation */}
          {[0, 120, 240].map((ang) => {
            const r = 2.6;
            const x = Math.cos((ang * Math.PI) / 180) * r;
            const y = Math.sin((ang * Math.PI) / 180) * r;
            return (
              <mesh key={ang} position={[x, y, thickness * 0.68 + 0.35]}>
                <cylinderGeometry args={[0.45, 0.45, 0.5, 12]} />
                <meshStandardMaterial color="#111111" roughness={0.8} />
              </mesh>
            );
          })}
        </group>
      ))}

      {/* axes — subtle */}
      <group position={[0, 0, thickness + 1.2]}>
        <Line points={[[0, 0, 0], [7, 0, 0]]} color="#8A8A82" lineWidth={1} />
        <Line points={[[0, 0, 0], [0, 7, 0]]} color="#8A8A82" lineWidth={1} />
        <Text position={[8.2, 0, 0]} fontSize={1.2} color="#8A8A82" anchorX="left" anchorY="middle">
          X
        </Text>
        <Text position={[0, 8.2, 0]} fontSize={1.2} color="#8A8A82" anchorX="center" anchorY="bottom">
          Y
        </Text>
      </group>

      {/* engineering dimension — extension lines + ticks + label */}
      {!reduced && (
        <group>
          {/* extension lines */}
          <Line points={[[-overall / 2, -overall / 2 - 0.5, 0], [-overall / 2, -overall / 2 - 5.5, 0]]} color="#5A5A56" lineWidth={1} />
          <Line points={[[overall / 2, -overall / 2 - 0.5, 0], [overall / 2, -overall / 2 - 5.5, 0]]} color="#5A5A56" lineWidth={1} />
          {/* dimension line */}
          <Line points={[[-overall / 2, -overall / 2 - 4.2, 0], [overall / 2, -overall / 2 - 4.2, 0]]} color="#315B73" lineWidth={1.1} />
          {/* ticks */}
          <Line points={[[-overall / 2, -overall / 2 - 5.2, 0], [-overall / 2, -overall / 2 - 3.2, 0]]} color="#315B73" lineWidth={1.4} />
          <Line points={[[overall / 2, -overall / 2 - 5.2, 0], [overall / 2, -overall / 2 - 3.2, 0]]} color="#315B73" lineWidth={1.4} />
          <Text
            position={[0, -overall / 2 - 4.2, 0.3]}
            fontSize={2.1}
            color="#315B73"
            anchorX="center"
            anchorY="bottom"
            outlineWidth={0.04}
            outlineColor="#FFFFFF"
            letterSpacing={0.05}
          >
            50.00 mm
          </Text>
        </group>
      )}
    </group>
  );
}

function SceneContent({ reduced }: { reduced: boolean }) {
  return (
    <>
      <ambientLight intensity={0.92} />
      <directionalLight position={[14, 18, 16]} intensity={1.05} color="#FFFFFF" castShadow={false} />
      <directionalLight position={[-12, -8, 10]} intensity={0.32} color="#DDE8F0" />
      <QuadcopterFrame reduced={reduced} />
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
      <ContactShadows position={[0, 0, -1.9]} opacity={0.18} scale={70} blur={2.2} far={8} color="#111111" />
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
  const [mounted, setMounted] = useState(false);
  const [webGLFailed, setWebGLFailed] = useState(false);

  useEffect(() => {
    setMounted(true);
    const mq = window.matchMedia('(prefers-reduced-motion: reduce)');
    setReduced(mq.matches);
    const onChange = (e: MediaQueryListEvent) => setReduced(e.matches);
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
    <Canvas
      dpr={[1, 1.4]}
      camera={{ position: [52, -48, 38], fov: 28 }}
      gl={{ antialias: true, alpha: true, powerPreference: 'high-performance' }}
      frameloop="demand"
      onCreated={({ gl }) => {
        gl.setClearColor('#FFFFFF', 1);
      }}
      style={{ background: '#FFFFFF' }}
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
        background: '#FFFFFF',
        display: 'grid',
        placeItems: 'center',
        padding: 24,
        position: 'relative',
      }}
      role="img"
      aria-label="Technical diagram fallback — quadcopter frame top view"
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
        <circle cx={72} cy={72} r={4} fill="#1B1B1B" />
        <circle cx={228} cy={72} r={4} fill="#1B1B1B" />
        <circle cx={72} cy={228} r={4} fill="#1B1B1B" />
        <circle cx={228} cy={228} r={4} fill="#1B1B1B" />
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
