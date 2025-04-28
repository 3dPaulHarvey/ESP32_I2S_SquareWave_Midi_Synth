#ifndef MIDI_PLAYER_H
#define MIDI_PLAYER_H

#include <Arduino.h>
#include "Synthesizer.h" // Needs access to the Synthesizer class
#include "SongData.h"    // Needs access to SongInfo struct definition

// Class to handle MIDI playback logic
class MidiPlayer {
public:
    MidiPlayer();

    // Initialize the player, providing the synth it will control
    void init(Synthesizer& synth_instance);

    // Load song metadata from PROGMEM SongInfo struct address
    bool loadSong(const SongInfo* song_info_progmem_addr);

    // Start playback of the loaded song
    void start();

    // Stop playback (optional, might not be needed if just playing once)
    void stop();

    // Call this repeatedly in the main loop()
    // Returns true if playing, false if finished or stopped.
    bool update();

private:
    // --- Constants ---
    static const uint8_t BYTES_PER_EVENT = 6;
    static const uint16_t TICKS_PER_QUARTER_NOTE = 96;

    // --- References ---
    Synthesizer* synth; // Pointer to the synth engine

    // --- Loaded Song Info ---
    const uint8_t* current_song_data_ptr; // Pointer to selected song's data in PROGMEM
    uint16_t current_event_count;
    float current_bpm;

    // --- Playback State ---
    bool is_playing;
    uint16_t current_event_index;
    unsigned long next_event_time_ms;

    // --- Timing ---
    float millis_per_tick;

    // --- Private Helper Methods ---
    void calculateTimingFactors(float bpm);
    unsigned long convertTicksToMillis(uint16_t ticks);
    uint16_t pgm_read_uint16_big_endian(const uint8_t* flash_address);
    void processCurrentEvent();
    void scheduleNextEvent(unsigned long current_processing_time_ms);
};

#endif // MIDI_PLAYER_H