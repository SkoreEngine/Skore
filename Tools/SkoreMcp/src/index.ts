#!/usr/bin/env node
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { api } from "./client.js";

const server = new McpServer({ name: "skore-mcp", version: "0.1.0" });

const REF_NOTE = 'Reference an asset by its UUID or by a path id like "Project:/Assets/Foo.scene".';
const SAVE_NOTE = "Changes are kept in memory until skore_save is called.";

const FIELD_NOTE =
  "'fields' maps field name to value. Encodings: scalars (bool/number/string) as-is; " +
  "Vec2/Vec3/Vec4 and Quat as number arrays [x,y,z,w]; Mat4 as 16 numbers; " +
  "Color as [r,g,b,a] integers 0-255; Enum as a number; TypeID as a type name; " +
  "Reference as a uuid or path; ReferenceArray as an array of uuid/path strings; " +
  "SubObject as a nested object (optionally with a \"_type\" key); " +
  "SubObjectList as an array of such objects (replaces the whole list). " +
  "Blob and Buffer fields cannot be set. Inspect field names and shapes first with skore_get_asset.";

type ToolResult = { content: { type: "text"; text: string }[]; isError?: boolean };

async function run(fn: () => Promise<unknown>): Promise<ToolResult> {
  try {
    const data = await fn();
    return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    return { content: [{ type: "text", text: msg }], isError: true };
  }
}

server.registerTool(
  "skore_list_types",
  {
    title: "List asset types",
    description: "List the creatable Skore asset types (type, fullName, extension, desc). Use 'type' when creating assets.",
    inputSchema: {},
  },
  async () => run(() => api.types()),
);

server.registerTool(
  "skore_list_assets",
  {
    title: "List assets",
    description: `List the sub-directories and assets inside a directory. ${REF_NOTE} Omit 'dir' to list the project root.`,
    inputSchema: {
      dir: z.string().optional().describe("Directory ref (uuid or path). Defaults to the project root."),
    },
  },
  async ({ dir }) => run(() => api.listAssets(dir)),
);

server.registerTool(
  "skore_get_asset",
  {
    title: "Get asset",
    description: `Get an asset's metadata plus its full data object (every field) under "object". ${REF_NOTE}`,
    inputSchema: {
      ref: z.string().describe("Asset ref (uuid or path)."),
    },
  },
  async ({ ref }) => run(() => api.getAsset(ref)),
);

server.registerTool(
  "skore_create_asset",
  {
    title: "Create asset",
    description: `Create a new asset of the given type inside a directory. Use skore_list_types for valid type names. ${SAVE_NOTE}`,
    inputSchema: {
      type: z.string().describe("Asset type name (from skore_list_types)."),
      parent: z.string().optional().describe("Parent directory ref. Defaults to the project root."),
      name: z.string().optional().describe("Desired name (auto-uniquified if it collides). Optional."),
    },
  },
  async ({ type, parent, name }) => run(() => api.createAsset(type, parent, name)),
);

server.registerTool(
  "skore_create_directory",
  {
    title: "Create directory",
    description: `Create a sub-directory. ${SAVE_NOTE}`,
    inputSchema: {
      name: z.string().describe("Folder name."),
      parent: z.string().optional().describe("Parent directory ref. Defaults to the project root."),
    },
  },
  async ({ name, parent }) => run(() => api.createDirectory(name, parent)),
);

server.registerTool(
  "skore_update_asset",
  {
    title: "Update asset fields",
    description: `Set one or more fields on an asset's data object. ${FIELD_NOTE} ${SAVE_NOTE}`,
    inputSchema: {
      ref: z.string().describe("Asset ref (uuid or path)."),
      fields: z.record(z.any()).describe("Map of field name to new value."),
    },
  },
  async ({ ref, fields }) => run(() => api.updateAsset(ref, fields)),
);

server.registerTool(
  "skore_rename_asset",
  {
    title: "Rename asset",
    description: `Rename an asset or folder. ${SAVE_NOTE}`,
    inputSchema: {
      ref: z.string().describe("Asset ref (uuid or path)."),
      name: z.string().describe("New name (without extension)."),
    },
  },
  async ({ ref, name }) => run(() => api.renameAsset(ref, name)),
);

server.registerTool(
  "skore_delete_asset",
  {
    title: "Delete asset",
    description: `Delete an asset or folder. ${SAVE_NOTE}`,
    inputSchema: {
      ref: z.string().describe("Asset ref (uuid or path)."),
    },
  },
  async ({ ref }) => run(() => api.deleteAsset(ref)),
);

server.registerTool(
  "skore_move_asset",
  {
    title: "Move asset",
    description: `Move an asset or folder into a target directory. ${SAVE_NOTE}`,
    inputSchema: {
      ref: z.string().describe("Asset ref (uuid or path)."),
      targetDir: z.string().describe("Target directory ref (uuid or path)."),
    },
  },
  async ({ ref, targetDir }) => run(() => api.moveAsset(ref, targetDir)),
);

server.registerTool(
  "skore_duplicate_asset",
  {
    title: "Duplicate asset",
    description: `Duplicate an asset. ${SAVE_NOTE}`,
    inputSchema: {
      ref: z.string().describe("Source asset ref (uuid or path)."),
      parent: z.string().optional().describe("Target directory ref. Defaults to the source's directory."),
      name: z.string().optional().describe("Desired name for the copy. Optional."),
    },
  },
  async ({ ref, parent, name }) => run(() => api.duplicateAsset(ref, parent, name)),
);

server.registerTool(
  "skore_save",
  {
    title: "Save",
    description: "Persist all pending (in-memory) asset changes to disk. Call after create/update/delete/rename/move/duplicate.",
    inputSchema: {},
  },
  async () => run(() => api.save()),
);

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error(`skore-mcp ready (editor: ${api.baseUrl})`);
}

main().catch((e) => {
  console.error("skore-mcp fatal:", e);
  process.exit(1);
});
