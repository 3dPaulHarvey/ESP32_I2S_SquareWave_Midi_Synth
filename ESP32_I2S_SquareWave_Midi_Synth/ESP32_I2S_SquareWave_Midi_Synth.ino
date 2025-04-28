#include <Arduino.h>
#include "Synthesizer.h" // Synth engine
#include "SongData.h"    // Song definitions and song_list
#include "MidiPlayer.h"  // MIDI playback logic

// --- Global Objects ---
Synthesizer synth;
MidiPlayer player;

// --- Setup ---
void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println("\nESP32 Refactored MIDI Player");

    // 1. Initialize the Synthesizer (starts I2S, audio task)
    if (!synth.init()) {
        Serial.println("FATAL: Synthesizer initialization failed!");
        while (true); // Halt
    }
    Serial.println("Synthesizer Initialized.");

    // 2. Initialize the MIDI Player, giving it the synth instance
    player.init(synth);
    Serial.println("MIDI Player Initialized.");

    // 3. Select a Random Song from the list in SongData.h
    if (SONG_COUNT == 0) {
        Serial.println("Error: SONG_COUNT is zero in SongData.h!");
        while (true); // Halt
    }
    randomSeed((unsigned int)esp_random()); // Use hardware RNG
    uint16_t selected_song_index = random(SONG_COUNT);
    Serial.printf("Total songs defined: %u. Randomly selected index: %u\n", SONG_COUNT, selected_song_index);

    // 4. Load the selected song's info into the player
    //    We pass the PROGMEM address of the chosen SongInfo struct
    const SongInfo* selected_song_info_pgm_addr = &song_list[selected_song_index];
    if (!player.loadSong(selected_song_info_pgm_addr)) {
        Serial.println("FATAL: Failed to load selected song info!");
        while(true); // Halt
    }

    // 5. Start Playback (after a short delay)
    Serial.println("Setup Complete. Starting playback soon...");
    delay(1500);
    player.start(); // Tell the player to start processing
}

// --- Main Loop ---
void loop() {
    // The main loop simply calls the player's update method.
    // The player handles timing and event processing internally.
    bool still_playing = player.update();

    if (!still_playing) {
        // Song has finished (or was stopped)
        Serial.println("Playback complete or stopped. Halting in main loop.");
        Serial.flush();
        // Halt execution - the player indicated it's done.
        while (true) {
            delay(1000); // Keep the watchdog happy in the halt loop
        }
    }

    // No delay needed here typically, as the player's timing relies on millis()
    // and the synth's audio task runs independently. The loop should run fast
    // to allow player.update() to check time frequently.
    // A small yield() could be added if other low-priority tasks exist, but
    // often not necessary for this structure.
    // yield(); // or delay(1); if absolutely needed
}