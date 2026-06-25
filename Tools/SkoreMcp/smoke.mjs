const BASE = (process.env.SKORE_MCP_URL ?? "http://127.0.0.1:8090").replace(/\/+$/, "");

async function hit(method, path, body) {
  const init = { method };
  if (body !== undefined) {
    init.headers = { "Content-Type": "application/json" };
    init.body = JSON.stringify(body);
  }
  let res;
  try {
    res = await fetch(BASE + path, init);
  } catch (e) {
    console.error(`  ${method} ${path} -> UNREACHABLE (${e.message})`);
    process.exitCode = 1;
    return;
  }
  const text = await res.text();
  console.log(`  ${method} ${path} -> ${res.status}`);
  console.log(text ? text.split("\n").map((l) => "    " + l).join("\n") : "    <empty>");
  if (!res.ok) process.exitCode = 1;
}

console.log(`Skore editor: ${BASE}\n`);
await hit("GET", "/api/ping");
console.log();
await hit("GET", "/api/types");
console.log();
await hit("GET", "/api/assets");
