// Fixture "kitchen-sink" — every field the format can carry, freeRun clock:
// all 5 effects (A56 set), all 4 scope types, both blends, default + stock +
// custom palette refs, fractional + whole speeds, an open-ended cue, and a
// same-start z-order tie (lanes 0/2/3/4 at startMs 0 -> descending-lane
// stream order).
song = {
  ticksPerQuarter: 480,
  tempo: [{ tick: 0, usPerQuarter: 500000 }],
  tracks: [{ name: "R" }],
  notes: [
    { onTick: 0,   offTick: 240,  note: 60, velocity: 100, track: 0, onMs: 0,    offMs: 250 },
    { onTick: 480, offTick: 720,  note: 64, velocity: 90,  track: 0, onMs: 500,  offMs: 750 },
    { onTick: 960, offTick: 1200, note: 67, velocity: 80,  track: 0, onMs: 1000, offMs: 1250 }
  ],
  durationMs: 1250
};
clips = []; groups = []; selection = new Set(); clipSeq = 1;
el("showName").value = "corpus-kitchen-sink";
el("clockSource").value = "1";
updateFollowTrackVis(); populateFollowTrack(255);
clips = [
  Object.assign(newClip(0, 0, 2000), { effect: "fire2012", scopeType: 0, drive: 0,
    blend: 0, opacity: 0.5, paletteKind: "stock", stockIdx: 3, speed: 2.0 }),
  Object.assign(newClip(1, 500, 4000), { effect: "twinklefox", scopeType: 1,
    lo: 60, hi: 72, drive: 1, blend: 1, opacity: 1.0, paletteKind: "custom",
    cA: "#ff0000", cB: "#0000ff", speed: 0.5 }),
  Object.assign(newClip(2, 0, 1500), { effect: "notedriven", scopeType: 3,
    drive: 1, openEnded: false }),
  Object.assign(newClip(3, 0, 1000), { effect: "colorwaves", scopeType: 2,
    notes: [36, 48, 60] }),
  Object.assign(newClip(4, 0, 9999), { effect: "pacifica", scopeType: 0,
    openEnded: true, speed: 1.5 })
];
"kitchen-sink setup ok";
