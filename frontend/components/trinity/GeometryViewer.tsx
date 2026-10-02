/* eslint-disable react-hooks/set-state-in-effect */
'use client';

import { Suspense, useEffect, useMemo, useRef, useState } from 'react';
import * as THREE from 'three';
import { Canvas, useFrame } from '@react-three/fiber';
import { ContactShadows, Grid, OrbitControls, RoundedBox, useGLTF } from '@react-three/drei';
import type { TrinityResult } from '@/lib/api/trinity';
import { getArtifactUrl } from '@/lib/api/trinity';

type Dimensions = [number, number, number];

function readDimensions(result: TrinityResult | null): Dimensions {
  const bounds = result?.result.bounding_box_mm;
  if (Array.isArray(bounds) && bounds.length >= 2 && Array.isArray(bounds[0]) && Array.isArray(bounds[1])) {
    return [0, 1, 2].map((axis) => Math.max(0.01, Math.abs(Number(bounds[1][axis]) - Number(bounds[0][axis])))) as Dimensions;
  }
  if (bounds && typeof bounds === 'object') {
    const record = bounds as Record<string, unknown>;
    const min = record.min;
    const max = record.max;
    if (Array.isArray(min) && Array.isArray(max)) {
      return [0, 1, 2].map((axis) => Math.max(0.01, Math.abs(Number(max[axis]) - Number(min[axis])))) as Dimensions;
    }
  }

  const parameters = result?.result.spec?.parameters as Record<string, unknown> | undefined;
  const dimension = (keys: string[], fallback: number) => {
    for (const key of keys) {
      const value = parameters?.[key];
      if (typeof value === 'number' && value > 0 && Number.isFinite(value)) return value;
    }
    return fallback;
  };
  return [
    dimension(['width', 'length'], 28),
    dimension(['depth', 'height'], 20),
    dimension(['thickness', 'wall_thickness'], 10),
  ];
}

function ProceduralPreview({
  dimensions,
  wireframe,
  showAxes,
  spin,
}: {
  dimensions: Dimensions;
  wireframe: boolean;
  showAxes: boolean;
  spin: boolean;
}) {
  const yawRef = useRef<THREE.Group>(null);
  useFrame((_, delta) => {
    if (yawRef.current && spin) yawRef.current.rotation.z += delta * 0.22;
  });

  const largest = Math.max(...dimensions);
  const scale = 34 / largest;
  const previewSize = dimensions.map((size) => Math.max(2.2, size * scale)) as Dimensions;
  const radius = Math.min(...previewSize) * 0.08;

  return (
    <group ref={yawRef}>
      <RoundedBox args={previewSize} radius={radius} smoothness={3}>
        <meshStandardMaterial color={wireframe ? '#EDEDE8' : '#F4F4F1'} wireframe={wireframe} roughness={0.42} metalness={0.14} />
      </RoundedBox>
      {showAxes && <axesHelper args={[19]} />}
    </group>
  );
}

function GLBModel({ url, wireframe, spin }: { url: string; wireframe: boolean; spin: boolean }) {
  const gltf = useGLTF(url) as unknown as { scene: THREE.Group };
  const model = useMemo(() => gltf.scene.clone(true), [gltf.scene]);
  const turntable = useRef<THREE.Group>(null);

  useEffect(() => {
    const bounds = new THREE.Box3().setFromObject(model);
    const size = bounds.getSize(new THREE.Vector3());
    const center = bounds.getCenter(new THREE.Vector3());
    const largest = Math.max(size.x, size.y, size.z, 0.001);
    const scale = 42 / largest;
    model.scale.setScalar(scale);
    model.position.set(-center.x * scale, -center.y * scale, -center.z * scale);
  }, [model]);

  useEffect(() => {
    model.traverse((object) => {
      const mesh = object as THREE.Mesh;
      if (!mesh.isMesh) return;
      const configure = (material: THREE.Material) => {
        const cloned = material.clone();
        if ('wireframe' in cloned) (cloned as THREE.MeshStandardMaterial).wireframe = wireframe;
        cloned.needsUpdate = true;
        return cloned;
      };
      mesh.material = Array.isArray(mesh.material)
        ? mesh.material.map(configure)
        : configure(mesh.material);
    });
  }, [model, wireframe]);

  useFrame((_, delta) => {
    if (turntable.current && spin) turntable.current.rotation.z += delta * 0.22;
  });

  return <group ref={turntable}><primitive object={model} /></group>;
}

function ViewerScene({
  glbUrl,
  dimensions,
  wireframe,
  showAxes,
  spin,
}: {
  glbUrl: string | null;
  dimensions: Dimensions;
  wireframe: boolean;
  showAxes: boolean;
  spin: boolean;
}) {
  return (
    <>
      <ambientLight intensity={0.95} />
      <directionalLight position={[12, 14, 14]} intensity={1.05} />
      <directionalLight position={[-10, -8, 8]} intensity={0.30} color="#DDE8F0" />
      {glbUrl
        ? <GLBModel url={glbUrl} wireframe={wireframe} spin={spin} />
        : <ProceduralPreview dimensions={dimensions} wireframe={wireframe} showAxes={showAxes} spin={spin} />}
      <Grid
        position={[0, 0, -1.85]}
        args={[100, 100]}
        cellSize={5}
        cellThickness={0.45}
        sectionSize={20}
        sectionThickness={1}
        sectionColor="rgba(255,255,255,0.09)"
        cellColor="rgba(255,255,255,0.04)"
        fadeDistance={38}
        infiniteGrid
      />
      <ContactShadows position={[0, 0, -1.82]} opacity={0.28} scale={56} blur={2.4} far={9} color="#000000" />
      {showAxes && <axesHelper args={[18]} />}
      <OrbitControls
        enablePan={false}
        enableZoom
        zoomSpeed={0.6}
        minDistance={18}
        maxDistance={88}
        enableDamping
        dampingFactor={0.08}
        rotateSpeed={0.55}
      />
    </>
  );
}

export default function GeometryViewer({ result }: { result: TrinityResult | null }) {
  const [wireframe, setWireframe] = useState(false);
  const [showAxes, setShowAxes] = useState(true);
  const [spin, setSpin] = useState(true);
  const [reduced, setReduced] = useState(false);

  useEffect(() => {
    const mq = window.matchMedia('(prefers-reduced-motion: reduce)');
    setReduced(mq.matches);
    const onChange = (event: MediaQueryListEvent) => {
      setReduced(event.matches);
      if (event.matches) setSpin(false);
    };
    mq.addEventListener?.('change', onChange);
    return () => mq.removeEventListener?.('change', onChange);
  }, []);

  const glbUrl = useMemo(() => {
    const glb = result?.artifacts.find((artifact) => artifact.type.toLowerCase() === 'glb');
    return glb ? getArtifactUrl(glb.artifact_id) : null;
  }, [result]);
  const dimensions = useMemo(() => readDimensions(result), [result]);
  const triangleCount = result?.result.triangle_count;
  const spinning = spin && !reduced;
  const dimensionsLabel = dimensions.map((value) => Number(value.toFixed(2))).join(' × ');

  return (
    <section className="viewer-panel" aria-labelledby="viewer-title">
      <div className="viewer-top">
        <span id="viewer-title">TRINITY / GEOMETRY VIEWER</span>
        <strong style={{ color: glbUrl ? '#7fdba0' : 'rgba(255,255,255,0.6)' }}>{glbUrl ? 'GLB' : 'PREVIEW'}</strong>
      </div>

      <div className="viewer-canvas-wrap">
        <Canvas
          camera={{ position: [42, -40, 30], fov: 32 }}
          dpr={[1, 1.25]}
          gl={{ antialias: true, alpha: true, powerPreference: 'high-performance' }}
          frameloop={spinning ? 'always' : 'demand'}
          style={{ background: '#0E0F0E' }}
          onCreated={({ gl }) => gl.setClearColor('#0E0F0E', 1)}
        >
          <Suspense fallback={null}>
            <ViewerScene glbUrl={glbUrl} dimensions={dimensions} wireframe={wireframe} showAxes={showAxes} spin={spinning} />
          </Suspense>
        </Canvas>
      </div>

      <div className="viewer-controls" role="toolbar" aria-label="Viewer controls">
        <button className={`ctrl-btn ${!wireframe ? 'is-active' : ''}`} onClick={() => setWireframe(false)} type="button" aria-pressed={!wireframe}>◉ SOLID</button>
        <button className={`ctrl-btn ${wireframe ? 'is-active' : ''}`} onClick={() => setWireframe(true)} type="button" aria-pressed={wireframe}>◇ WIREFRAME</button>
        <button className={`ctrl-btn ${showAxes ? 'is-active' : ''}`} onClick={() => setShowAxes(!showAxes)} type="button" aria-pressed={showAxes}>⊙ AXES</button>
        <button
          className={`ctrl-btn ${spinning ? 'is-active' : ''}`}
          onClick={() => setSpin((current) => !current)}
          type="button"
          aria-pressed={spinning}
          aria-label={spinning ? 'Stop rotation' : 'Start rotation'}
        >
          ⟳ SPIN
        </button>
        <button
          className="ctrl-btn"
          onClick={() => { setWireframe(false); setShowAxes(true); setSpin(true); }}
          type="button"
          aria-label="Reset viewer"
        >
          ↻ RESET
        </button>
      </div>

      <div className="viewer-bottom">
        <span>{dimensionsLabel} MM</span>
        <span>TRIANGLES: {typeof triangleCount === 'number' ? triangleCount.toLocaleString() : '—'}</span>
        <strong>{result?.validation?.status ?? 'AWAITING'}</strong>
      </div>
    </section>
  );
}
