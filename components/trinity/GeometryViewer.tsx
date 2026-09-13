'use client';

import { Suspense, useEffect, useMemo, useState } from 'react';
import * as THREE from 'three';
import { Canvas } from '@react-three/fiber';
import { Grid, OrbitControls, useGLTF } from '@react-three/drei';
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
      <mesh>
        <boxGeometry args={[21, 21, 3.4]} />
        <meshStandardMaterial color={wireframe ? '#EDEDE8' : '#FFFFFF'} wireframe={wireframe} roughness={0.8} />
      </mesh>
      {[45, 135, 225, 315].map((deg) => {
        const r = 32 * Math.SQRT2;
        const x = (r / 2) * Math.cos((deg * Math.PI) / 180);
        const y = (r / 2) * Math.sin((deg * Math.PI) / 180);
        return (
          <group key={deg} position={[x, y, 0]} rotation={[0, 0, (deg * Math.PI) / 180]}>
            <mesh>
              <boxGeometry args={[22, 4.6, 3.4]} />
              <meshStandardMaterial color="#EDEDE8" wireframe={wireframe} />
            </mesh>
          </group>
        );
      })}
      {showAxes && <axesHelper args={[18]} />}
    </group>
  );
}

function GLBModel({ url, wireframe }: { url: string; wireframe: boolean }) {
  const gltf = useGLTF(url) as unknown as { scene: THREE.Group };
  useEffect(() => {
    gltf.scene.traverse((obj) => {
      const mesh = obj as THREE.Mesh;
      if ((mesh as unknown as { isMesh: boolean }).isMesh) {
        const mat = mesh.material as THREE.MeshStandardMaterial;
        if (mat) mat.wireframe = wireframe;
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
      <ambientLight intensity={1.0} />
      <directionalLight position={[10, 10, 10]} intensity={1.1} />
      <directionalLight position={[-8, -8, 6]} intensity={0.35} />
      {glbUrl ? <GLBModel url={glbUrl} wireframe={wireframe} /> : <ProceduralPreview wireframe={wireframe} showAxes={showAxes} />}
      <Grid
        position={[0, 0, -1.8]}
        args={[100, 100]}
        cellSize={5}
        cellThickness={0.5}
        sectionSize={20}
        sectionThickness={1}
        sectionColor="rgba(17,17,17,0.12)"
        cellColor="rgba(17,17,17,0.06)"
        fadeDistance={36}
        infiniteGrid
      />
      {showAxes && <axesHelper args={[20]} />}
      <OrbitControls enableDamping dampingFactor={0.08} minDistance={18} maxDistance={120} />
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

  const resetKey = `${wireframe}-${showAxes}-${glbUrl ?? 'preview'}`;
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
          key={resetKey}
          camera={{ position: [42, -40, 30], fov: 32 }}
          dpr={[1, 1.8]}
          gl={{ antialias: true, alpha: true }}
          style={{ background: '#0E0F0E' }}
          onCreated={({ gl }) => gl.setClearColor('#0E0F0E', 1)}
        >
          <Suspense fallback={null}>
            <ViewerScene glbUrl={glbUrl} wireframe={wireframe} showAxes={showAxes} />
          </Suspense>
        </Canvas>
      </div>

      <div className="viewer-controls">
        <button className={`ctrl-btn ${!wireframe ? 'is-active' : ''}`} onClick={() => setWireframe(false)} type="button">
          ◉ SOLID
        </button>
        <button className={`ctrl-btn ${wireframe ? 'is-active' : ''}`} onClick={() => setWireframe(true)} type="button">
          ◇ WIREFRAME
        </button>
        <button className={`ctrl-btn ${showAxes ? 'is-active' : ''}`} onClick={() => setShowAxes(!showAxes)} type="button">
          ⊙ AXES
        </button>
        <button
          className="ctrl-btn"
          onClick={() => {
            setWireframe(false);
            setShowAxes(true);
          }}
          type="button"
        >
          ↻ RESET
        </button>
      </div>

      <div className="viewer-bottom">
        <span>
          {overall.toFixed(2)} × {overall.toFixed(2)} MM
        </span>
        <span>
          TRIANGLES: {triangleCount ? triangleCount.toLocaleString() : '—'}
        </span>
        <strong>{result?.validation?.status ?? 'AWAITING'}</strong>
      </div>
    </section>
  );
}
