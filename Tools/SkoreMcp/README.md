# skore-mcp

An [MCP](https://modelcontextprotocol.io) server that lets an LLM read and edit
**Skore** project assets through the editor's embedded HTTP server.

```
LLM / MCP client  <-- stdio -->  skore-mcp (this)  <-- HTTP -->  Skore editor (HttpServerSettings)
```

The editor side is the source of truth: this server only translates MCP tool calls
into HTTP requests against the editor's asset API.

## Prerequisites

- Node.js >= 18
- The Skore editor running with a project open, and **Http Server** enabled
  (Editor Settings -> Editor/Http Server -> Enabled). Default port `8090`.

## Install & build

```bash
cd Tools/SkoreMcp
npm install
npm run build        # typecheck + esbuild -> dist/skore-mcp.mjs (one self-contained file)
```

When you build the engine, CMake builds this automatically and deploys the bundled
file next to the editor at `<build>/bin/Mcp/skore-mcp.mjs`. The bundle has no runtime
dependencies, so it runs anywhere with just `node skore-mcp.mjs` (no `node_modules`).

The easiest way to wire it into Claude Code is the editor's **Tools -> Install Skore MCP**
menu item, which writes the project `.mcp.json`, enables the Http Server, and shows the
config for other clients.

## Configuration

The editor URL is read from `SKORE_MCP_URL` (default `http://127.0.0.1:8090`).
Set it to match your `HttpServerSettings` Host/Port if you changed them.

## Verify the editor API (no MCP)

With the editor running and the server enabled:

```bash
npm run smoke        # or: node smoke.mjs
```

This hits `/api/ping`, `/api/types`, and `/api/assets` and prints the raw
responses, so you can confirm the editor side independently of the MCP layer.

## Register with an MCP client

Point your client (Claude Desktop, Claude Code, Cursor, ...) at the bundled entry
`skore-mcp.mjs` (self-contained, no `node_modules`). Example `mcpServers` block:

```json
{
  "mcpServers": {
    "skore": {
      "command": "node",
      "args": ["C:/dev/SkoreEngine/Skore/cmake-build-debug/bin/Mcp/skore-mcp.mjs"],
      "env": { "SKORE_MCP_URL": "http://127.0.0.1:8090" }
    }
  }
}
```

(For Claude Code: `claude mcp add skore -- node C:/dev/SkoreEngine/Skore/cmake-build-debug/bin/Mcp/skore-mcp.mjs`.)

## Tools

| Tool | Purpose |
| --- | --- |
| `skore_list_types` | List creatable asset types (use these names when creating). |
| `skore_list_material_nodes` | List every node type placeable in a MaterialGraph (typeId + pins/properties). |
| `skore_add_material_node` | Add a node to a `.matgraph`; returns the new node's uuid. |
| `skore_connect_material_nodes` | Wire a source output pin into a destination input pin. |
| `skore_list_assets` | List a directory's sub-folders and assets (`dir` optional → root). |
| `skore_get_asset` | Full metadata + data object (all fields) for one asset. |
| `skore_create_asset` | Create an asset of a given type in a directory. |
| `skore_create_directory` | Create a sub-directory. |
| `skore_update_asset` | Set fields on an asset's data object. |
| `skore_rename_asset` | Rename an asset or folder. |
| `skore_delete_asset` | Delete an asset or folder. |
| `skore_move_asset` | Move an asset/folder into a directory. |
| `skore_duplicate_asset` | Duplicate an asset. |
| `skore_save` | Persist pending changes to disk. |

## Addressing assets

Every `ref` / `dir` / `targetDir` argument accepts either:

- a **UUID** (stable across rename/move), or
- a **path id** like `Project:/Assets/Materials/Brick.material` (package name,
  then `:/`, then the path).

Omit the directory argument on list/create to target the project root.

## Field value encodings (`skore_update_asset`)

`fields` is a map of field name → value. Discover the field names and current
shapes with `skore_get_asset` (they appear under `object`).

| Field type | JSON value |
| --- | --- |
| bool / int / uint / float / string | the value directly |
| enum | a number |
| Vec2 / Vec3 / Vec4 / Quat | number array, e.g. `[x, y, z]` |
| Mat4 | array of 16 numbers |
| Color | `[r, g, b, a]` integers, 0–255 |
| TypeID | a type name |
| Reference | a uuid or path string |
| ReferenceArray | array of uuid/path strings |
| SubObject | nested object, optionally with `"_type"` |
| SubObjectList | array of objects (replaces the whole list) |
| Blob / Buffer | not supported |

## Authoring material graphs

A `.matgraph` asset is created with `skore_create_asset` (type `MaterialGraph`) and
already contains the permanent output/master node. To build the graph:

1. `skore_list_material_nodes` — the catalog of valid `typeId`s and their pin layouts.
2. `skore_add_material_node` — add nodes; each call returns the new node's `uuid`.
3. `skore_get_asset` — read the graph back; the output node is the entry in
   `object.Nodes` whose `Type` is `"output"` (its `_uuid` is what you connect into).
4. `skore_connect_material_nodes` — wire output pins into input pins by index.
   The output node's input pins are Base Color, Metallic, Roughness, Emissive,
   Normal, Ambient Occlusion, Opacity (indices 0–6). An input pin holds one
   connection; re-connecting it replaces the old edge.

## Persistence

Create/update/delete/rename/move/duplicate change the editor's **in-memory**
resources only (they show the unsaved `*` marker in the editor). Call
`skore_save` to flush them to disk. Every mutation is wrapped in a named
undo scope, so it can be undone in the editor (Ctrl+Z).

## Develop

```bash
npm run dev          # run from source via tsx (no build step)
```

Note: stdout carries the MCP JSON-RPC stream — this server logs only to stderr.
