import React, { useEffect, useRef, useState } from 'react';

type Props = {
  url?: string;
  label?: string;
};

/** Lightweight STL preview using Three.js when available. */
export function StlViewer({ url, label = 'CAD Preview' }: Props) {
  const mountRef = useRef<HTMLDivElement>(null);
  const [error, setError] = useState<string | null>(null);
  const [ready, setReady] = useState(false);

  useEffect(() => {
    if (!url || !mountRef.current) return;
    let disposed = false;
    let renderer: any;
    let frameId = 0;

    (async () => {
      try {
        const THREE = await import('three');
        const { STLLoader } = await import('three/examples/jsm/loaders/STLLoader.js');
        const container = mountRef.current!;
        const width = container.clientWidth || 420;
        const height = 280;
        const scene = new THREE.Scene();
        scene.background = new THREE.Color(0x0b1220);
        const camera = new THREE.PerspectiveCamera(45, width / height, 0.1, 1000);
        camera.position.set(0, 0, 80);
        renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
        renderer.setSize(width, height);
        container.innerHTML = '';
        container.appendChild(renderer.domElement);
        const light = new THREE.DirectionalLight(0xffffff, 1.2);
        light.position.set(1, 1, 2);
        scene.add(light);
        scene.add(new THREE.AmbientLight(0x404040, 0.8));
        const loader = new STLLoader();
        loader.load(
          url,
          (geometry) => {
            if (disposed) return;
            geometry.computeBoundingBox();
            const mesh = new THREE.Mesh(
              geometry,
              new THREE.MeshStandardMaterial({ color: 0x38bdf8, metalness: 0.2, roughness: 0.6 }),
            );
            geometry.center();
            scene.add(mesh);
            const animate = () => {
              if (disposed) return;
              mesh.rotation.y += 0.008;
              renderer.render(scene, camera);
              frameId = requestAnimationFrame(animate);
            };
            animate();
            setReady(true);
          },
          undefined,
          (err) => setError(String(err)),
        );
      } catch (err) {
        setError(String(err));
      }
    })();

    return () => {
      disposed = true;
      cancelAnimationFrame(frameId);
      renderer?.dispose?.();
    };
  }, [url]);

  if (!url) {
    return (
      <div className="rounded border border-dashed border-white/10 bg-black/20 p-4 text-xs text-muted-foreground">
        STL preview available when a `.stl` artifact is generated (Fusion worker export).
      </div>
    );
  }

  return (
    <div className="overflow-hidden rounded border border-white/10 bg-[#0b1220]">
      <div className="border-b border-white/10 px-3 py-2 text-[10px] uppercase tracking-widest text-primary">
        {label} {ready ? '· LIVE' : '· LOADING'}
      </div>
      {error && <div className="p-3 text-xs text-red-400">{error}</div>}
      <div ref={mountRef} className="h-[280px] w-full" />
    </div>
  );
}
