'use client';

import { Suspense, useEffect, useMemo, useState } from 'react';
import * as THREE from 'three';
import { Canvas } from '@react-three/fiber';
import { Grid, OrbitControls, useGLTF, ContactShadows, RoundedBox } from '@react-three/drei';
import type { TrinityResult } from '@/lib/api/trinity';
import { getArtifactUrl } from '@/lib/api/trinity';

function ProceduralPreview({
  wireframe,
  showAxes,
}: {
  wireframe: boolean;
  showAxes: boolean;
}) {
  return (
    <group>
      <RoundedBox args={[21, 21, 3.0]} radius={1.1} smoothness={3}>
        <meshStandardMaterial color={wireframe ? '#EDEDE8' : '#F4F4F1'} wireframe={wireframe} roughness={0.42} metalness={0.14} />
      </RoundedBox>
      {[45, 135, 225, 315].map((deg) => {
        const r = 32 * Math.SQRT2;
        const x = (r / 2) * Math.cos((deg * Math.PI) / 180);
        const y = (r / 2) * Math.sin((deg * Math.PI) / 180);
        return (
          <group key={deg} position={[x, y, 0]} rotation={[0, 0, (deg * Math.PI) / 180]}>
            <RoundedBox args={[20, 4.6, 3.0]} radius={0.6} smoothness={3}>
              <meshStandardMaterial color="#E9E9E4" wireframe={wireframe} roughness={0.45} />
            </RoundedBox>
          </group>
        );
      })}
      {/* tiny graphite mounts to match hero */}
      {[45, 135, 225, 315].map((deg) => {
        const endR = 25;
        const x = endR * Math.cos((deg * Math.PI) / 180);
        const y = endR * Math.sin((deg * Math.PI) / 180);
        return (
          <group key={`m-${deg}`} position={[x, y, 1.35]}>
            <mesh rotation={[Math.PI / 2, 0, 0]}>
              <cylinderGeometry args={[4.2, 4.2, 4.0, 20]} />
              <meshStandardMaterial color="#2A2E2B" wireframe={wireframe} roughness={0.55} metalness={0.32} />
            </mesh>
          </group>
        );
      })}
      {showAxes && <axesHelper args={[16]} />}
    </group>
  );
}

function GLBModel({ url, wireframe }: { url: string; wireframe: boolean }) {
  const gltf = useGLTF(url) as unknown as { scene: THREE.Group };
  useEffect(() => {
    gltf.scene.traverse((obj) => {
      const mesh = obj as THREE.Mesh;
      if ((mesh as unknown as { isMesh: boolean }).isMesh) {
        const mat = mesh.material as THREE.MeshStandardMaterial | THREE.MeshStandardMaterial[];
        const apply = (m: THREE.MeshStandardMaterial) => {
          m.wireframe = wireframe;
          m.needsUpdate = true;
        };
        if (Array.isArray(mat)) mat.forEach(apply);
        else if (mat) apply(mat);
      }
    });
  }, [gltf, wireframe]);
  return <primitive object={gltf.scene} scale={1} position={[0, 0, 0]} />;
}

function ViewerScene({
  glbUrl,
  wireframe,
  showAxes,
}: {
  glbUrl: string | null;
  wireframe: boolean;
  showAxes: boolean;
}) {
  return (
    <>
      <ambientLight intensity={0.95} />
      <directionalLight position={[12, 14, 14]} intensity={1.05} />
      <directionalLight position={[-10, -8, 8]} intensity={0.30} color="#DDE8F0" />
      {glbUrl ? <GLBModel url={glbUrl} wireframe={wireframe} /> : <ProceduralPreview wireframe={wireframe} showAxes={showAxes} />}
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
  const glbUrl = useMemo(() => {
    const glb = result?.artifacts.find((a) => a.type.toLowerCase() === 'glb');
    return glb ? getArtifactUrl(glb.artifact_id) : null;
  }, [result]);

  const triangleCount = result?.result.triangle_count as number | undefined;
  const overall = (result?.result.spec?.parameters as Record<string, number> | undefined)?.overall_size ?? 50;

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
          frameloop="demand"
          style={{ background: '#0E0F0E' }}
          onCreated={({ gl }) => gl.setClearColor('#0E0F0E', 1)}
        >
          <Suspense fallback={null}>
            <ViewerScene glbUrl={glbUrl} wireframe={wireframe} showAxes={showAxes} />
          </Suspense>
        </Canvas>
      </div>

      <div className="viewer-controls" role="toolbar" aria-label="Viewer controls">
        <button className={`ctrl-btn ${!wireframe ? 'is-active' : ''}`} onClick={() => setWireframe(false)} type="button" aria-pressed={!wireframe}>
          ◉ SOLID
        </button>
        <button className={`ctrl-btn ${wireframe ? 'is-active' : ''}`} onClick={() => setWireframe(true)} type="button" aria-pressed={wireframe}>
          ◇ WIREFRAME
        </button>
        <button className={`ctrl-btn ${showAxes ? 'is-active' : ''}`} onClick={() => setShowAxes(!showAxes)} type="button" aria-pressed={showAxes}>
          ⊙ AXES
        </button>
        <button
          className="ctrl-btn"
          onClick={() => {
            setWireframe(false);
            setShowAxes(true);
          }}
          type="button"
          aria-label="Reset viewer"
        >
          ↻ RESET
        </button>
      </div>

      <div className="viewer-bottom">
        <span>
          {overall.toFixed(2)} × {overall.toFixed(2)} MM
        </span>
        <span>TRIANGLES: {triangleCount ? triangleCount.toLocaleString() : '—'}</span>
        <strong>{result?.validation?.status ?? 'AWAITING'}</strong>
      </div>
    </section>
  );
}
