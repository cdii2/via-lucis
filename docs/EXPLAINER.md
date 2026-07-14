# Via Lucis

*"The Way of Light."* A strip of LEDs above my piano keys that teaches me to play.

**The setup.** An ESP32 chip pairs with my Roland FP-30X over Bluetooth MIDI, so it knows every key I press the instant I press it. It drives an LED strip mounted above the keys. About $180 of parts (including the power-protection fuses), no soldering. My phone is the remote — the chip serves its own web page over WiFi. No laptop at the piano, no app to install.

**Learning a song.** Load a MIDI file and the lights show you the way.

- *Wait mode.* The song halts and waits. The right keys glow. Nothing advances until you hit them. Chords clear one key at a time, and a wrong key flashes red.
- *Lookahead.* A coming note glows faintly, swells, then snaps to full brightness the moment it's due. That jump is what makes *now* unmistakable.
- *Hands separate.* Left is blue, right is green. Mute one hand and the piano plays it for you while you practice the other.
- *Demo and loop.* The piano can play the whole piece itself with the lights following. Or loop one hard bar from 1% to 500% speed.

**Beyond practice.** Idle, the strip runs ambient effects. Noodling, it reacts to how hard you strike and whether the pedal's down. And there's a browser editor — a piano roll for light — where you can choreograph a whole show that *follows your live playing*, speeding up and slowing down with you.

**Recording.** Hit record, play, and the device writes your performance out as a MIDI file. Your own songs go straight back into the system.

Open source, MIT. Parts are ordered; everything's written and tested against a simulator while I wait for them to land.
