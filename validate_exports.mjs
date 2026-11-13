// Independent validation of files written by the SculptForge engine.
// Shares no code with the engine: every format is re-parsed here from its public specification.
// Usage: node Tools/validate_exports.mjs <directory with exported files>
import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";

const dir = process.argv[2] ?? "build/test_output";
let failures = 0;
const rows = [];
function report(file, ok, info) {
  if (!ok) failures++;
  rows.push(`${ok ? "PASS" : "FAIL"}  ${file.padEnd(28)} ${info}`);
}

// ---- geometry checks shared by all formats ------------------------------------------------
// positions: Float32Array/Float64Array xyz, indices: array of vertex ids (3 per triangle)
function topology(positions, indices) {
  // weld by exact coordinate so unindexed formats (STL) can be checked for watertightness
  const ids = new Map();
  const remap = new Int32Array(positions.length / 3);
  for (let i = 0; i < positions.length / 3; i++) {
    const key = `${positions[i * 3]},${positions[i * 3 + 1]},${positions[i * 3 + 2]}`;
    let id = ids.get(key);
    if (id === undefined) { id = ids.size; ids.set(key, id); }
    remap[i] = id;
  }
  const edges = new Map();
  let volume = 0, degenerate = 0;
  const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
  for (let t = 0; t < indices.length; t += 3) {
    const v = [indices[t], indices[t + 1], indices[t + 2]];
    const p = v.map((i) => [positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]]);
    for (const q of p) for (let k = 0; k < 3; k++) { mn[k] = Math.min(mn[k], q[k]); mx[k] = Math.max(mx[k], q[k]); }
    const [a, b, c] = p;
    volume += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6;
    const w = v.map((i) => remap[i]);
    if (w[0] === w[1] || w[1] === w[2] || w[0] === w[2]) { degenerate++; continue; }
    for (let k = 0; k < 3; k++) {
      const x = w[k], y = w[(k + 1) % 3];
      const key = x < y ? x * 4294967296 + y : y * 4294967296 + x;
      const e = edges.get(key) ?? [0, 0];
      e[x < y ? 0 : 1]++;
      edges.set(key, e);
    }
  }
  let boundary = 0, nonManifold = 0, inconsistent = 0;
  for (const [f, r] of edges.values()) {
    if (f + r === 1) boundary++;
    else if (f + r > 2) nonManifold++;
    else if (f !== 1) inconsistent++;
  }
  return { triangles: indices.length / 3, uniqueVertices: ids.size, boundary, nonManifold, inconsistent, degenerate, volume,
    size: mx.map((v, k) => v - mn[k]), watertight: boundary === 0 && nonManifold === 0 && inconsistent === 0 };
}
const fmtTopo = (t) => `${t.triangles} tris, ${t.size.map((v) => v.toFixed(1)).join(" x ")}, volume ${t.volume.toFixed(1)}, ` +
  (t.watertight ? "watertight" : `open ${t.boundary} / non-manifold ${t.nonManifold} / flipped ${t.inconsistent}`);
const finite = (arr) => { for (const v of arr) if (!Number.isFinite(v)) return false; return true; };

// ---- STL (binary) --------------------------------------------------------------------------
function checkSTL(buf) {
  if (buf.length < 84) throw new Error("shorter than an STL header");
  const n = buf.readUInt32LE(80);
  if (buf.length !== 84 + n * 50) throw new Error(`size ${buf.length} does not match ${n} triangles`);
  const pos = new Float32Array(n * 9), idx = new Uint32Array(n * 3);
  let badNormals = 0;
  for (let t = 0; t < n; t++) {
    const o = 84 + t * 50;
    const nr = [buf.readFloatLE(o), buf.readFloatLE(o + 4), buf.readFloatLE(o + 8)];
    for (let k = 0; k < 9; k++) pos[t * 9 + k] = buf.readFloatLE(o + 12 + k * 4);
    for (let k = 0; k < 3; k++) idx[t * 3 + k] = t * 3 + k;
    const p = pos.subarray(t * 9, t * 9 + 9);
    const e1 = [p[3] - p[0], p[4] - p[1], p[5] - p[2]], e2 = [p[6] - p[0], p[7] - p[1], p[8] - p[2]];
    const c = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]];
    const l = Math.hypot(...c);
    if (l > 1e-12 && (c[0] * nr[0] + c[1] * nr[1] + c[2] * nr[2]) / l < 0.99) badNormals++;
  }
  if (!finite(pos)) throw new Error("non-finite coordinate");
  if (badNormals) throw new Error(`${badNormals} facet normals disagree with the vertex winding`);
  return topology(pos, idx);
}

// ---- OBJ -----------------------------------------------------------------------------------
function checkOBJ(text) {
  const pos = [], idx = [];
  let objects = 0, colours = 0, normals = 0;
  for (const line of text.split("\n")) {
    const p = line.trim().split(/\s+/);
    if (p[0] === "v") { pos.push(+p[1], +p[2], +p[3]); if (p.length >= 7) colours++; }
    else if (p[0] === "vn") normals++;
    else if (p[0] === "o") objects++;
    else if (p[0] === "f") {
      if (p.length !== 4) throw new Error("non-triangular face");
      for (let k = 1; k <= 3; k++) {
        const [v, , vn] = p[k].split("/").map(Number);
        if (!(v >= 1 && v <= pos.length / 3)) throw new Error("face index out of range");
        if (!(vn >= 1 && vn <= normals)) throw new Error("normal index out of range");
        idx.push(v - 1);
      }
    }
  }
  if (!finite(pos)) throw new Error("non-finite coordinate");
  const t = topology(pos, idx);
  return { ...t, extra: `${objects} objects, ${colours} coloured verts` };
}

// ---- PLY (binary little endian, as written by the engine) -----------------------------------
function checkPLY(buf) {
  const headEnd = buf.indexOf("end_header\n");
  if (headEnd < 0) throw new Error("no end_header");
  const head = buf.subarray(0, headEnd).toString("latin1").split("\n");
  if (head[0] !== "ply" || !head[1].startsWith("format binary_little_endian")) throw new Error("unexpected header");
  const sizes = { float: 4, uchar: 1, int: 4, uint: 4, double: 8, short: 2, ushort: 2, char: 1 };
  let nv = 0, nf = 0, stride = 0, cur = "";
  const props = [];
  for (const l of head) {
    const p = l.split(" ");
    if (p[0] === "element") { cur = p[1]; if (cur === "vertex") nv = +p[2]; if (cur === "face") nf = +p[2]; }
    if (p[0] === "property" && cur === "vertex") { props.push({ name: p[2], type: p[1], off: stride }); stride += sizes[p[1]]; }
  }
  let o = headEnd + "end_header\n".length;
  if (buf.length !== o + nv * stride + nf * 13) throw new Error("body size does not match the header counts");
  const px = props.find((p) => p.name === "x").off, hasColour = props.some((p) => p.name === "red");
  const pos = new Float32Array(nv * 3);
  for (let i = 0; i < nv; i++) for (let k = 0; k < 3; k++) pos[i * 3 + k] = buf.readFloatLE(o + i * stride + px + k * 4);
  o += nv * stride;
  const idx = new Uint32Array(nf * 3);
  for (let f = 0; f < nf; f++) {
    if (buf[o + f * 13] !== 3) throw new Error("non-triangular face");
    for (let k = 0; k < 3; k++) {
      const v = buf.readInt32LE(o + f * 13 + 1 + k * 4);
      if (v < 0 || v >= nv) throw new Error("face index out of range");
      idx[f * 3 + k] = v;
    }
  }
  if (!finite(pos)) throw new Error("non-finite coordinate");
  return { ...topology(pos, idx), extra: hasColour ? "per-vertex colour" : "no colour" };
}

// ---- GLB (glTF 2.0 binary) -----------------------------------------------------------------
function checkGLB(buf) {
  if (buf.readUInt32LE(0) !== 0x46546c67 || buf.readUInt32LE(4) !== 2) throw new Error("bad GLB header");
  if (buf.readUInt32LE(8) !== buf.length) throw new Error("declared length does not match the file size");
  const jl = buf.readUInt32LE(12);
  if (buf.readUInt32LE(16) !== 0x4e4f534a || jl % 4) throw new Error("bad JSON chunk");
  const g = JSON.parse(buf.subarray(20, 20 + jl).toString("utf8"));
  const bl = buf.readUInt32LE(20 + jl);
  if (buf.readUInt32LE(24 + jl) !== 0x004e4942 || 28 + jl + bl !== buf.length) throw new Error("bad BIN chunk");
  if (g.buffers[0].byteLength !== bl) throw new Error("buffer length mismatch");
  if (g.asset?.version !== "2.0") throw new Error("asset.version must be 2.0");
  const bin = buf.subarray(28 + jl);
  const csize = { 5121: 1, 5123: 2, 5125: 4, 5126: 4 }, ncomp = { SCALAR: 1, VEC3: 3, VEC4: 4 };
  const view = (ai) => {
    const a = g.accessors[ai], v = g.bufferViews[a.bufferView];
    const bytes = a.count * csize[a.componentType] * ncomp[a.type];
    if ((v.byteOffset ?? 0) % 4) throw new Error("bufferView is not 4-byte aligned");
    if ((v.byteOffset ?? 0) + bytes > bl || bytes > v.byteLength) throw new Error("accessor exceeds its buffer");
    return { a, off: v.byteOffset ?? 0 };
  };
  let tris = 0, watertightMeshes = 0;
  for (const node of g.nodes) {
    if (node.matrix && node.matrix.length !== 16) throw new Error("bad node matrix");
    const prim = g.meshes[node.mesh].primitives[0];
    const P = view(prim.attributes.POSITION), N = view(prim.attributes.NORMAL), C = view(prim.attributes.COLOR_0), I = view(prim.indices);
    if (N.a.count !== P.a.count || C.a.count !== P.a.count) throw new Error("attribute counts differ");
    if (!C.a.normalized || C.a.componentType !== 5121) throw new Error("COLOR_0 must be normalized ubyte");
    const pos = new Float32Array(P.a.count * 3);
    const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < pos.length; i++) { pos[i] = bin.readFloatLE(P.off + i * 4); mn[i % 3] = Math.min(mn[i % 3], pos[i]); mx[i % 3] = Math.max(mx[i % 3], pos[i]); }
    for (let k = 0; k < 3; k++)
      if (Math.abs(mn[k] - P.a.min[k]) > 1e-4 || Math.abs(mx[k] - P.a.max[k]) > 1e-4) throw new Error("POSITION min/max do not match the data");
    for (let i = 0; i < N.a.count; i++) {
      const l = Math.hypot(bin.readFloatLE(N.off + i * 12), bin.readFloatLE(N.off + i * 12 + 4), bin.readFloatLE(N.off + i * 12 + 8));
      if (Math.abs(l - 1) > 1e-3) throw new Error("normal is not unit length");
    }
    const idx = new Uint32Array(I.a.count);
    for (let i = 0; i < idx.length; i++) { idx[i] = bin.readUInt32LE(I.off + i * 4); if (idx[i] >= P.a.count) throw new Error("index out of range"); }
    const m = g.materials[prim.material].pbrMetallicRoughness;
    if (m.baseColorFactor.length !== 4 || !(m.metallicFactor >= 0 && m.metallicFactor <= 1) || !(m.roughnessFactor >= 0 && m.roughnessFactor <= 1))
      throw new Error("bad material factors");
    if (!finite(pos)) throw new Error("non-finite coordinate");
    const t = topology(pos, idx);
    tris += t.triangles;
    if (t.watertight) watertightMeshes++;
  }
  return { text: `${g.nodes.length} nodes, ${g.materials.length} materials, ${tris} tris, ${watertightMeshes}/${g.nodes.length} meshes watertight` };
}

// ---- ZIP / 3MF -----------------------------------------------------------------------------
function unzipStored(buf) {
  const eocd = buf.lastIndexOf(Buffer.from([0x50, 0x4b, 0x05, 0x06]));
  if (eocd < 0) throw new Error("no ZIP end-of-central-directory record");
  const count = buf.readUInt16LE(eocd + 10), cdSize = buf.readUInt32LE(eocd + 12), cdOff = buf.readUInt32LE(eocd + 16);
  if (cdOff + cdSize !== eocd) throw new Error("central directory size/offset mismatch");
  const files = new Map();
  let o = cdOff;
  for (let i = 0; i < count; i++) {
    if (buf.readUInt32LE(o) !== 0x02014b50) throw new Error("bad central directory entry");
    const method = buf.readUInt16LE(o + 10), crc = buf.readUInt32LE(o + 16), csize = buf.readUInt32LE(o + 20), usize = buf.readUInt32LE(o + 24);
    const nl = buf.readUInt16LE(o + 28), xl = buf.readUInt16LE(o + 30), cl = buf.readUInt16LE(o + 32), lo = buf.readUInt32LE(o + 42);
    const name = buf.subarray(o + 46, o + 46 + nl).toString("utf8");
    if (buf.readUInt32LE(lo) !== 0x04034b50) throw new Error(`bad local header for ${name}`);
    const lnl = buf.readUInt16LE(lo + 26), lxl = buf.readUInt16LE(lo + 28);
    if (buf.subarray(lo + 30, lo + 30 + lnl).toString("utf8") !== name) throw new Error("local/central name mismatch");
    const raw = buf.subarray(lo + 30 + lnl + lxl, lo + 30 + lnl + lxl + csize);
    const data = method === 0 ? raw : zlib.inflateRawSync(raw);
    if (data.length !== usize) throw new Error(`size mismatch for ${name}`);
    if (zlib.crc32(data) !== crc) throw new Error(`CRC mismatch for ${name}`);
    files.set(name, data);
    o += 46 + nl + xl + cl;
  }
  return files;
}

function check3MF(buf) {
  const files = unzipStored(buf);
  for (const need of ["[Content_Types].xml", "_rels/.rels", "3D/3dmodel.model"]) if (!files.has(need)) throw new Error(`missing part ${need}`);
  if (!files.get("_rels/.rels").toString().includes("/3D/3dmodel.model")) throw new Error("relationship does not point at the model");
  const xml = files.get("3D/3dmodel.model").toString("utf8");
  if (!/<model[^>]*unit="millimeter"/.test(xml)) throw new Error("model unit is not millimeter");
  if (!xml.includes("http://schemas.microsoft.com/3dmanufacturing/core/2015/02")) throw new Error("missing 3MF core namespace");
  const objects = [...xml.matchAll(/<object id="(\d+)"[^>]*>([\s\S]*?)<\/object>/g)];
  const items = [...xml.matchAll(/<item objectid="(\d+)"/g)].map((m) => m[1]);
  if (!objects.length || items.length !== objects.length) throw new Error("build items do not match objects");
  let tris = 0, watertight = 0;
  for (const [, id, body] of objects) {
    if (!items.includes(id)) throw new Error(`object ${id} is not in the build`);
    const pos = [], idx = [];
    for (const m of body.matchAll(/<vertex x="([^"]+)" y="([^"]+)" z="([^"]+)"\/>/g)) pos.push(+m[1], +m[2], +m[3]);
    for (const m of body.matchAll(/<triangle v1="(\d+)" v2="(\d+)" v3="(\d+)"\/>/g)) idx.push(+m[1], +m[2], +m[3]);
    if (!finite(pos)) throw new Error("non-finite coordinate");
    for (const i of idx) if (i >= pos.length / 3) throw new Error("triangle index out of range");
    const t = topology(pos, idx);
    tris += t.triangles;
    if (t.watertight && t.volume > 0) watertight++;
  }
  return { text: `${objects.length} objects, ${tris} tris, ${watertight}/${objects.length} closed and outward-facing, unit mm`, allClosed: watertight === objects.length };
}

// ---- native project ------------------------------------------------------------------------
function checkProject(buf) {
  if (buf.subarray(0, 8).toString("latin1") !== "SFORGE3D") throw new Error("bad magic");
  if (buf.subarray(buf.length - 8, buf.length - 3).toString("latin1") !== "SFEND") throw new Error("missing end marker");
  if (zlib.crc32(buf.subarray(0, buf.length - 12)) !== buf.readUInt32LE(buf.length - 12)) throw new Error("checksum mismatch");
  return { text: `version ${buf.readUInt32LE(8)}, ${buf.readUInt32LE(12)} objects, checksum ok` };
}

// ---- run -----------------------------------------------------------------------------------
const mustBeWatertight = new Set(["cat_print.stl", "cat_print.3mf", "head.stl", "acceptance_head.stl", "head.obj", "head.ply", "head.3mf"]);
// future_version.* is a structurally valid container with a too-new version number (the engine must refuse it; here it just parses)
const expectInvalid = (f) => f.startsWith("corrupt_") || f.startsWith("garbage") || f === "model.fbx";
// hand-written inputs for the importer tests (ASCII / polygon files), not files the engine wrote
const importFixtures = new Set(["ascii.stl", "ascii.ply", "quad.obj"]);
for (const file of fs.readdirSync(dir).sort()) {
  if (importFixtures.has(file)) continue;
  const ext = path.extname(file).toLowerCase();
  if (![".stl", ".obj", ".ply", ".glb", ".3mf", ".sculptforge"].includes(ext)) continue;
  const buf = fs.readFileSync(path.join(dir, file));
  try {
    let info, ok = true;
    if (ext === ".stl") { const t = checkSTL(buf); info = fmtTopo(t); if (mustBeWatertight.has(file)) ok = t.watertight && t.volume > 0; }
    else if (ext === ".obj") { const t = checkOBJ(buf.toString("utf8")); info = `${fmtTopo(t)}, ${t.extra}`; if (mustBeWatertight.has(file)) ok = t.watertight; }
    else if (ext === ".ply") { const t = checkPLY(buf); info = `${fmtTopo(t)}, ${t.extra}`; if (mustBeWatertight.has(file)) ok = t.watertight; }
    else if (ext === ".glb") info = checkGLB(buf).text;
    else if (ext === ".3mf") { const r = check3MF(buf); info = r.text; if (mustBeWatertight.has(file)) ok = r.allClosed; }
    else info = checkProject(buf).text;
    if (expectInvalid(file)) report(file, false, `expected an invalid file but it parsed: ${info}`);
    else report(file, ok, ok ? info : `${info}  <-- expected a closed solid`);
  } catch (e) {
    if (expectInvalid(file)) report(file, true, `correctly rejected (${e.message})`);
    else report(file, false, e.message);
  }
}
console.log(rows.join("\n"));
console.log(`\n${rows.length - failures} of ${rows.length} files validated, ${failures} failed`);
process.exit(failures || rows.length === 0 ? 1 : 0);
