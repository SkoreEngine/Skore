import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

const transport = new StdioClientTransport({
  command: "node",
  args: ["dist/index.js"],
  env: { ...process.env },
});

const client = new Client({ name: "skore-mcp-test", version: "0.0.0" });

let failures = 0;
function check(label, cond, extra = "") {
  console.log(`  ${cond ? "PASS" : "FAIL"}  ${label}${extra ? "  " + extra : ""}`);
  if (!cond) failures++;
}

function approxEqual(a, b) {
  if (typeof a === "number" && typeof b === "number") return Math.abs(a - b) < 1e-3;
  if (Array.isArray(a) && Array.isArray(b)) return a.length === b.length && a.every((x, i) => approxEqual(x, b[i]));
  return JSON.stringify(a) === JSON.stringify(b);
}

async function call(name, args = {}) {
  const res = await client.callTool({ name, arguments: args });
  const text = res.content?.[0]?.text ?? "";
  let data;
  try { data = JSON.parse(text); } catch { data = { raw: text }; }
  if (res.isError) throw new Error(`tool ${name} error: ${text}`);
  return data;
}

await client.connect(transport);
console.log("connected to skore-mcp\n");

try {
  console.log("# protocol");
  const { tools } = await client.listTools();
  const names = tools.map((t) => t.name).sort();
  check("listTools returns 11 tools", tools.length === 11, names.join(", "));

  console.log("\n# read");
  const types = await call("skore_list_types");
  check("list_types has SceneResource", types.types?.some((t) => t.type === "SceneResource"));

  const root = await call("skore_list_assets");
  check("list_assets root returns entries", Array.isArray(root.entries), `(${root.entries?.length} entries)`);

  console.log("\n# create");
  const dir = await call("skore_create_directory", { name: "__mcp_test__" });
  check("create_directory ok", dir.isDirectory === true && !!dir.pathId, dir.pathId);
  const dirRef = dir.pathId;

  const created = await call("skore_create_asset", { type: "MaterialResource", parent: dirRef, name: "McpMat" });
  check("create_asset returns uuid", !!created.uuid, created.uuid);
  check("create_asset returns object with fields", !!created.object && !!created.object._type, created.object?._type);
  const uuid = created.uuid;

  console.log("\n# get + update");
  const got = await call("skore_get_asset", { ref: uuid });
  check("get_asset returns object", !!got.object);
  console.log("    object fields: " + Object.keys(got.object).filter((k) => !k.startsWith("_")).join(", "));

  const fields = {};
  for (const [k, v] of Object.entries(got.object)) {
    if (k.startsWith("_")) continue;
    if (typeof v === "boolean") fields[k] = !v;
    else if (typeof v === "number") fields[k] = v + 1;
    else if (Array.isArray(v) && v.length > 0 && v.every((x) => typeof x === "number")) fields[k] = v.map((x, i) => (i === 0 ? (x >= 200 ? x - 50 : x + 50) : x));
  }
  if (Object.keys(fields).length > 0) {
    const upd = await call("skore_update_asset", { ref: uuid, fields });
    check("update_asset ok", upd.ok === true, "updated=[" + (upd.updated || []).join(",") + "] skipped=[" + (upd.skipped || []).join(",") + "]");
    const after = await call("skore_get_asset", { ref: uuid });
    let changed = 0;
    for (const k of Object.keys(fields)) if (approxEqual(after.object[k], fields[k])) changed++;
    check("update_asset values round-tripped", changed === Object.keys(fields).length, `${changed}/${Object.keys(fields).length}`);
  } else {
    console.log("    (no scalar/array fields to mutate on this type — skipping value round-trip)");
  }

  console.log("\n# path-based ref");
  const byPath = await call("skore_get_asset", { ref: dirRef + "/McpMat.material" });
  check("get_asset by path id resolves same asset", byPath.uuid === uuid, byPath.uuid);

  console.log("\n# rename + duplicate + list");
  const renamed = await call("skore_rename_asset", { ref: uuid, name: "McpRenamed" });
  check("rename_asset ok", renamed.name === "McpRenamed", renamed.name);

  const dup = await call("skore_duplicate_asset", { ref: uuid });
  check("duplicate_asset returns new uuid", !!dup.uuid && dup.uuid !== uuid, dup.uuid);

  const listing = await call("skore_list_assets", { dir: dirRef });
  check("dir now lists 2 assets", (listing.entries || []).filter((e) => !e.isDirectory).length === 2);

  console.log("\n# delete + cleanup");
  await call("skore_delete_asset", { ref: dup.uuid });
  await call("skore_delete_asset", { ref: uuid });
  await call("skore_delete_asset", { ref: dirRef });
  const rootAfter = await call("skore_list_assets");
  check("__mcp_test__ removed from root", !(rootAfter.entries || []).some((e) => e.name === "__mcp_test__"));

  console.log("\n# error handling");
  let errored = false;
  try { await call("skore_get_asset", { ref: "00000000-0000-0000-0000-000000000000" }); } catch { errored = true; }
  check("get_asset on missing uuid reports error", errored);
} catch (e) {
  console.error("\nEXCEPTION:", e.message);
  failures++;
} finally {
  await client.close();
}

console.log(`\n${failures === 0 ? "ALL PASS" : failures + " FAILURE(S)"}`);
process.exit(failures === 0 ? 0 : 1);
