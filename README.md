# PKG Splitter

Split & rejoin large PS4 `.pkg` files — in your browser.

Split large `.pkg` files into 4 GiB parts (FAT32-safe) and rejoin them for
verification. Runs entirely client-side. Static site — GitHub Pages ready.

## Usage
1. Open the page (Chrome/Edge desktop).
2. Split: pick `.pkg`, set base name, split. Save each part.
3. Copy every part (`<name>.pkg.0..N`) to the USB root, same folder.
4. On the PS4: Settings → Debug Settings → Game → Package Installer →
   USB → select `.pkg.0`. The installer reassembles `.1..N` during install.
5. Rejoin section is for PC-side verification only.

## Constraints
- 4 GiB parts = FAT32-safe. exFAT also fine.
- Parts must keep exact names and sit in the same folder.
- PS4 jailbreak only.

## License
MIT — see [LICENSE](LICENSE).
