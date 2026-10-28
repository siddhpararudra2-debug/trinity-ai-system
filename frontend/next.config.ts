import type { NextConfig } from "next";

// Static export is a packaging-mode choice, not a default: `next start`
// cannot serve an exported build, so the desktop dev/start flow, which
// still runs `next start`, keeps the normal output. The packager sets
// TRINITY_STATIC_EXPORT=1 before `next build` to emit an `out/` directory
// that the FastAPI backend serves directly, removing Node from the
// shipped bundle entirely.
const nextConfig: NextConfig = {
  ...(process.env.TRINITY_STATIC_EXPORT === "1"
    ? { output: "export" as const }
    : {}),
  turbopack: {
    root: __dirname,
  },
};

export default nextConfig;
