# webui

The phone remote the ESP32 serves: one file, `index.html` (inline CSS + vanilla JS, no external assets).
Rebuild the embedded firmware copy after any edit: `python webui/build.py` (regenerates `firmware/src/webui_gz.h`).
`index.html` and `docs/API.md` must stay in sync — the API contract changes there first or not at all.
