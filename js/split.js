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
  if (!file.name.toLowerCase().endsWith(".pkg")) return log(out, "File does not end in .pkg.", "err");
  if (!window.showDirectoryPicker) return log(out, "Your browser lacks the File System Access API. Use Chrome or Edge on desktop.", "err");

  const base = sanitize($("split-name").value || file.name.replace(/\.pkg$/i, ""));
  const chunk = parseInt($("split-size").value, 10) || CHUNK_DEFAULT;
  const parts = Math.ceil(file.size / chunk);
  
  log(out, `Input: ${file.name} (${file.size.toLocaleString()} bytes)`);
  log(out, `Base name: ${base}`);
  log(out, `Chunk: ${chunk.toLocaleString()} bytes → ${parts} part(s)`);
  
  try {
    log(out, "Please select your USB folder to save the parts...");
    // Ask user to select the USB folder ONCE
    const dirHandle = await window.showDirectoryPicker({ mode: 'readwrite' });
    
    for (let i = 0; i < parts; i++) {
      const start = i * chunk;
      const end = Math.min(start + chunk, file.size);
      const blob = file.slice(start, end);
      const name = `${base}.pkg.${i}`;
      
      log(out, `Writing part ${i+1} of ${parts}...`);
      
      // Create the file directly inside the selected folder
      const fileHandle = await dirHandle.getFileHandle(name, { create: true });
      const writable = await fileHandle.createWritable();
      
      // Stream the chunk to the file to prevent memory crashes
      await blob.stream().pipeTo(writable);
      
      log(out, `Saved ${name} — ${blob.size.toLocaleString()} bytes`, "ok");
    }
    log(out, "DONE! All parts are on your USB. Unplug it and use it on your PS4.", "ok");
    
  } catch (e) {
    if (e.name === "AbortError") {
      log(out, "Cancelled by user.", "err");
    } else {
      log(out, `Error: ${e.message}`, "err");
    }
  }
});
