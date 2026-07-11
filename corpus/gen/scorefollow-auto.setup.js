// Fixture "scorefollow-auto" — clockSource 2 with follow track Auto: the
// editor still emits the META trailing byte, as 0xFF (the device's auto
// resolver, A54).
song = {
  ticksPerQuarter: 480,
  tempo: [{ tick: 0, usPerQuarter: 500000 }],
  tracks: [{ name: "R" }],
  notes: [
    { onTick: 0, offTick: 240, note: 60, velocity: 100, track: 0, onMs: 0, offMs: 250 }
  ],
  durationMs: 250
};
clips = []; groups = []; selection = new Set(); clipSeq = 1;
el("showName").value = "corpus-scorefollow-auto";
el("clockSource").value = "2";
updateFollowTrackVis(); populateFollowTrack();
el("followTrack").value = "255";
clips = [Object.assign(newClip(0, 0, 250), { effect: "twinklefox", scopeType: 1, lo: 48, hi: 84 })];
"scorefollow-auto setup ok";
