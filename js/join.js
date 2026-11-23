const $j = id => document.getElementById(id);
const jlog = (el, msg, cls="") => {
  const d = document.createElement("div");
  if (cls) d.className = cls;
  d.textContent = msg;
  el.appendChild(d);
  el.scrollTop = el.scrollHeight;
};

function parsePart(name) {
  const m = name.match(/^(.*)\.pkg\.(\d+)$/);
  return m ? { base: m[1], index: parseInt(m[2], 10) } : null;
}

$j("join-go").addEventListener("click", async () => {
  const out = $j("join-log");
  out.innerHTML = "";
  const baseWanted = ($j("join-name").value || "").trim();
  const files = [...$j("join-files").files];
  if (!files.length) return jlog(out, "Select the part files.", "err");

  const parsed = files.map(f => ({ file: f, p: parsePart(f.name) }));
  const bad = parsed.filter(x => !x.p);
  if (bad.length) return jlog(out, `Not a part name: ${bad.map(b => b.file.name).join(", ")}`, "err");

  const bases = new Set(parsed.map(x => x.p.base));
  if (bases.size !== 1) return jlog(out, `Mixed base names: ${[...bases].join(", ")}`, "err");
  const base = [...bases][0];
  if (baseWanted && baseWanted !== base)
    return jlog(out, `Name mismatch: parts are "${base}", field says "${baseWanted}".`, "err");

  parsed.sort((a, b) => a.p.index - b.p.index);
  const idx = parsed.map(x => x.p.index);
  if (idx[0] !== 0) return jlog(out, `Missing part .0 (have ${idx.join(",")})`, "err");
  for (let i = 0; i < idx.length; i++)
    if (idx[i] !== i) return jlog(out, `Gap: expected .${i}, found .${idx[i]}`, "err");

  jlog(out, `Rejoining ${parsed.length} part(s) as ${base}.pkg`);

  if (!window.showSaveFilePicker)
    return jlog(out, "Browser lacks File System Access API. Use Chrome/Edge desktop.", "err");
  const handle = await window.showSaveFilePicker({
    suggestedName: `${base}.pkg`,
    types: [{ description: "PKG", accept: { "application/octet-stream": [".pkg"] } }]
  });
  const w = await handle.createWritable();
  for (const { file } of parsed) {
    await file.stream().pipeTo(w, { preventClose: true });
    jlog(out, `appended ${file.name}`, "ok");
  }
  await w.close();
  jlog(out, "Rejoined. Compare SHA-256 with the original if you kept it.", "ok");
});
