// Fixture "scorefollow" — clockSource 2 with an explicit follow track (1 of
// a 2-track song): META carries the P4 trailing followTrack byte. One
// note-binding notedriven clip + a whole-strip fire2012 bed.
song = {
  ticksPerQuarter: 480,
  tempo: [{ tick: 0, usPerQuarter: 500000 }],
  tracks: [{ name: "R" }, { name: "L" }],
  notes: [
    { onTick: 0,   offTick: 240, note: 72, velocity: 100, track: 0, onMs: 0,   offMs: 250 },
    { onTick: 480, offTick: 720, note: 76, velocity: 90,  track: 0, onMs: 500, offMs: 750 },
    { onTick: 0,   offTick: 240, note: 48, velocity: 80,  track: 1, onMs: 0,   offMs: 250 },
    { onTick: 480, offTick: 720, note: 43, velocity: 70,  track: 1, onMs: 500, offMs: 750 }
  ],
  durationMs: 750
};
clips = []; groups = []; selection = new Set(); clipSeq = 1;
el("showName").value = "corpus-scorefollow";
el("clockSource").value = "2";
updateFollowTrackVis(); populateFollowTrack();
el("followTrack").value = "1";
clips = [
  Object.assign(newClip(0, 0, 750), { effect: "notedriven", scopeType: 3, drive: 1 }),
  Object.assign(newClip(1, 0, 750), { effect: "fire2012", scopeType: 0 })
];
"scorefollow setup ok";
