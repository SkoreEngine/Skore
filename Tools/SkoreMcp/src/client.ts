const BASE = (process.env.SKORE_MCP_URL ?? "http://127.0.0.1:8090").replace(/\/+$/, "");

type Json = any;

async function req(method: string, path: string, body?: Json): Promise<Json> {
  let res: Response;
  try {
    res = await fetch(BASE + path, {
      method,
      headers: body !== undefined ? { "Content-Type": "application/json" } : undefined,
      body: body !== undefined ? JSON.stringify(body) : undefined,
    });
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    throw new Error(`cannot reach Skore editor at ${BASE} (${msg}). Is the editor running with the Http Server enabled?`);
  }

  const text = await res.text();
  let data: Json;
  try {
    data = text ? JSON.parse(text) : {};
  } catch {
    data = { raw: text };
  }

  if (!res.ok) {
    const detail = data && typeof data === "object" && data.error ? data.error : `HTTP ${res.status} ${res.statusText}`;
    throw new Error(`${method} ${path}: ${detail}`);
  }
  return data;
}

function qs(params: Record<string, string | undefined>): string {
  const entries = Object.entries(params).filter(([, v]) => v !== undefined && v !== "");
  if (entries.length === 0) return "";
  return "?" + entries.map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v as string)}`).join("&");
}

export const api = {
  baseUrl: BASE,
  ping: () => req("GET", "/api/ping"),
  types: () => req("GET", "/api/types"),
  materialNodes: () => req("GET", "/api/material/nodes"),
  addMaterialNode: (body: {
    ref: string;
    typeId: string;
    position?: number[];
    value?: number[];
    parameterName?: string;
    texture?: string;
  }) => req("POST", "/api/material/addNode", body),
  connectMaterialNodes: (body: {
    ref: string;
    outputNode: string;
    outputPin: number;
    inputNode: string;
    inputPin: number;
  }) => req("POST", "/api/material/connect", body),
  listAssets: (dir?: string) => req("GET", "/api/assets" + qs({ dir })),
  getAsset: (ref: string) => req("GET", "/api/asset" + qs({ ref })),
  createAsset: (type: string, parent?: string, name?: string) => req("POST", "/api/assets/create", { type, parent, name }),
  createDirectory: (name: string, parent?: string) => req("POST", "/api/assets/createDirectory", { name, parent }),
  updateAsset: (ref: string, fields: Record<string, unknown>) => req("POST", "/api/asset/update", { ref, fields }),
  renameAsset: (ref: string, name: string) => req("POST", "/api/assets/rename", { ref, name }),
  deleteAsset: (ref: string) => req("POST", "/api/assets/delete", { ref }),
  moveAsset: (ref: string, targetDir: string) => req("POST", "/api/assets/move", { ref, targetDir }),
  duplicateAsset: (ref: string, parent?: string, name?: string) => req("POST", "/api/assets/duplicate", { ref, parent, name }),
  save: () => req("POST", "/api/save"),
};
