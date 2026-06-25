import { build } from "esbuild";

await build({
  entryPoints: ["src/index.ts"],
  outfile: "dist/skore-mcp.mjs",
  bundle: true,
  platform: "node",
  format: "esm",
  target: "node18",
  banner: {
    js: "#!/usr/bin/env node\nimport { createRequire } from 'module';\nconst require = createRequire(import.meta.url);",
  },
});
