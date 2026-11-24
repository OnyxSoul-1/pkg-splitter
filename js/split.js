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
  const listContainer = $("download-list");
  const card = $("download-card");
  
  out.innerHTML = "";
  listContainer.innerHTML = "";
  card.style.display = "none";
  
  const file = fileEl.files[0];
  if (!file) return log(out, "Pick a .pkg file first.", "err");
  if (!file.name.toLowerCase().endsWith(".pkg")) return log(out, "File does not end in .pkg.", "err");

  const base = sanitize($("split-name").value || file.name.replace(/\.pkg$/i, ""));
  
  // Get the custom GB size from the input box
  let sizeInput = parseFloat($("split-size-custom").value);
  if (isNaN(sizeInput) || sizeInput <= 0) sizeInput = 4; // Default to 4GB if they type nothing
  
  // Convert GB to Bytes (1 GB = 1024 * 1024 * 1024 bytes)
  const chunk = Math.floor(sizeInput * 1024 * 1024 * 1024);
  
  const parts = Math.ceil(file.size / chunk);
  
  log(out, `Input: ${file.name} (${file.size.toLocaleString()} bytes)`);
  log(out, `Cut Size: ${sizeInput} GB (${chunk.toLocaleString()} bytes)`);
  log(out, `Total Parts: ${parts}`);
  
  // Show loading text
  $("loading").style.display = "block";

  // Slight delay so the browser can show the loading text before it cuts
  setTimeout(() => {
    for (let i = 0; i < parts; i++) {
      const start = i * chunk;
      const end = Math.min(start + chunk, file.size);
      
      // Cut the file block
      const blob = file.slice(start, end);
      
      // Create a download link for this block
      const url = URL.createObjectURL(blob);
      const name = `${base}.pkg.${i}`;
      
      // Create the button
      const btn = document.createElement("a");
      btn.href = url;
      btn.download = name; // Tells browser to download it
      btn.className = "download-btn";
      
      // Calculate size in GB for the button
      const gbSize = (blob.size / (1024 * 1024 * 1024)).toFixed(2);
      
      btn.innerHTML = `<span>Part ${i}</span> <span class="size">${gbSize} GB</span> ⬇️`;
      
      listContainer.appendChild(btn);
      
      log(out, `✅ Ready: ${name}`, "ok");
    }
    
    // Hide loading and show the download card
    $("loading").style.display = "none";
    card.style.display = "block";
    log(out, `DONE! Click the green buttons to download your parts.`, "ok");
    
  }, 500); // Wait half a second
});
