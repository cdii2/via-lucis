// Fixture "minimal" — the smallest editor-producible show: demo clock, no
// song, one whole-strip fire2012 clip with every newClip default. Also the
// stream the editor selftest byte-pins (CORPUS_MINIMAL_HEX).
song = null; clips = []; groups = []; selection = new Set(); clipSeq = 1;
el("showName").value = "corpus-minimal";
el("clockSource").value = "0";
updateFollowTrackVis(); populateFollowTrack(255);
clips = [Object.assign(newClip(0, 0, 2000), { effect: "fire2012" })];
"minimal setup ok";
