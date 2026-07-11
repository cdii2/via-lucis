// Corpus generation helpers — eval this in a freshly-loaded editor.html page
// BEFORE a fixture setup script, then read the three outputs:
//   corpusBytesB64() -> data:...;base64 data URL of the baked .vls bytes
//   corpusTwinJson() -> the pretty JSON twin (+ trailing newline) for .expected.json
//   corpusBytesHex() -> lowercase hex of the .vls bytes (the selftest byte pin)
// With the browse daemon (from the repo root):
//   $B goto file:///<abs>/editor/editor.html
//   $B eval corpus/gen/_helpers.js
//   $B eval corpus/gen/<fixture>.setup.js
//   $B js "corpusBytesB64()" --out corpus/shows/<fixture>.vls
//   $B js "corpusTwinJson()" --out corpus/shows/<fixture>.expected.json --raw
window.corpusBytesB64 = () => {
  const bin = encodeVls(compile());
  let s = "";
  for (const b of bin) s += String.fromCharCode(b);
  return "data:application/octet-stream;base64," + btoa(s);
};
window.corpusTwinJson = () =>
  JSON.stringify(jsonTwin(decodeVls(encodeVls(compile()))), null, 2) + "\n";
window.corpusBytesHex = () => {
  const bin = encodeVls(compile());
  return Array.from(bin, b => b.toString(16).padStart(2, "0")).join("");
};
"helpers ok";
