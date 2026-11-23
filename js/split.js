const CHUNK_DEFAULT = 4294967296;
const $ = id => document.getElementById(id);
const log = (el, msg, cls="") => {
  const d = document.createElement("div");
  if (cls) d.className = cls;
  d.textContent = msg;
  el.appendChild(d);
  el.scrollTop = el.scrollHeight;
};

function sanitize(name) {
  return name.trim().replace(/[\\/:*?"<>|]+/g, "_") || "package";
}

$("split-go").addEventListener("click", async () => {
  const fileEl = $("split-file");
  const out = $("split-log");
  out.innerHTML = "";
  const file = fileEl.files[0];
  if (!file) return log(out, "Pick a .pkg file first.", "err");
  if (!file.name.toLowerCase().endsWith(".pkg"))
    return log(out, "File does not end in .pkg.", "err");
  if (!window.showSaveFilePicker)
    return log(out, "This browser lacks File System Access API. Use Chrome or Edge desktop.", "err");

  const base = sanitize($("split-name").value || file.name.replace(/\.pkg$/i, ""));
  const chunk = parseInt($("split-size").value, 10) || CHUNK_DEFAULT;
  const parts = Math.ceil(file.size / chunk);
  log(out, `Input: ${file.name} (${file.size.toLocaleString()} bytes)`);
  log(out, `Base name: ${base}`);
  log(out, `Chunk: ${chunk.toLocaleString()} bytes → ${parts} part(s)`);
  log(out, "One save dialog per part. Keep this tab focused.");

  for (let i = 0; i < parts; i++) {
    const start = i * chunk;
    const end = Math.min(start + chunk, file.size);
    const blob = file.slice(start, end);
    const name = `${base}.pkg.${i}`;
    try {
      const handle = await window.showSaveFilePicker({
        suggestedName: name,
        types: [{ description: "PKG part", accept: { "application/octet-stream": [".pkg." + i] } }]
      });
      const w = await handle.createWritable();
      await blob.stream().pipeTo(w);
      log(out, `saved ${name} — ${blob.size.toLocaleString()} bytes`, "ok");
    } catch (e) {
      log(out, `skipped ${name}: ${e.name}`, "err");
      if (e.name === "AbortError") return log(out, "Cancelled by user.", "err");
    }
  }
  log(out, "Done. Copy every part to the USB root, same folder, names unchanged.", "ok");
});
