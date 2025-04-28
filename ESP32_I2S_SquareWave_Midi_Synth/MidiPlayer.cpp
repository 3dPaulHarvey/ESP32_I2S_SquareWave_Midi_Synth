#include "MidiPlayer.h"
#include <pgmspace.h> // For PROGMEM read functions
#include <cmath>      // For roundf

MidiPlayer::MidiPlayer() :
    synth(nullptr),
    current_song_data_ptr(nullptr),
    current_event_count(0),
    current_bpm(0.0f),
    is_playing(false),
    current_event_index(0),
    next_event_time_ms(0),
    millis_per_tick(0.0f)
{}

void MidiPlayer::init(Synthesizer& synth_instance) {
    synth = &synth_instance; // Store pointer to the synth
    is_playing = false;
}

bool MidiPlayer::loadSong(const SongInfo* song_info_progmem_addr) {
    if (song_info_progmem_addr == nullptr) {
        Serial.println("MidiPlayer Error: Invalid song info address provided.");
        return false;
    }

    // Read song metadata from the PROGMEM address provided
    // Note: Need to read members relative to the base address of the struct in PROGMEM
    current_song_data_ptr = (const uint8_t*)pgm_read_ptr_near(&song_info_progmem_addr->midi_data_ptr);
    current_event_count = pgm_read_word_near(&song_info_progmem_addr->event_count);
    // Reading float from PROGMEM requires special handling if pgm_read_float_near isn't available/reliable
    // Using memcpy_P is a safe way:
    memcpy_P(&current_bpm, &song_info_progmem_addr->bpm, sizeof(float));


    if (current_song_data_ptr == nullptr || current_event_count == 0) {
        Serial.println("MidiPlayer Error: Loaded song data seems invalid (null pointer or zero events).");
        current_song_data_ptr = nullptr; // Ensure invalid state
        current_event_count = 0;
        return false;
    }

    // Calculate timing for this specific song
    calculateTimingFactors(current_bpm);

    Serial.println("--- MidiPlayer Loaded Song Info ---");
    Serial.printf("  Event Count: %u\n", current_event_count);
    Serial.printf("  BPM: %.2f\n", current_bpm);
    Serial.printf("  Millis per Tick: %.4f\n", millis_per_tick);
    Serial.printf("  Data Address: 0x%p\n", current_song_data_ptr);

    // Reset playback state for the new song
    is_playing = false;
    current_event_index = 0;
    next_event_time_ms = 0;

    return true;
}


void MidiPlayer::start() {
    if (is_playing) return;
    if (current_event_count == 0 || current_song_data_ptr == nullptr) {
        Serial.println("MidiPlayer Error: Cannot start playback, no valid song loaded.");
        return;
    }

    Serial.println("MidiPlayer: Starting Playback...");
    current_event_index = 0;
    is_playing = true;

    // Schedule the very first event
    uint16_t delta_ticks = pgm_read_uint16_big_endian(current_song_data_ptr);
    unsigned long delta_ms = convertTicksToMillis(delta_ticks);
    next_event_time_ms = millis() + delta_ms;
    // Serial.printf("MidiPlayer: First event in %lu ms (ticks: %u)\n", delta_ms, delta_ticks);
}

void MidiPlayer::stop() {
    if (!is_playing) return;
    Serial.println("MidiPlayer: Stopping Playback.");
    is_playing = false;
    // Optional: Tell synth to silence all notes
    if (synth) {
        // synth->allNotesOff(); // Requires implementing this method in Synthesizer
    }
}

bool MidiPlayer::update() {
    if (!is_playing) {
        return false; // Not playing, indicate finished/stopped
    }

    unsigned long current_time_ms = millis();

    // Check if it's time for the next event
    if (current_time_ms >= next_event_time_ms) {

        // End condition check *before* processing
        if (current_event_index >= current_event_count) {
            Serial.println("\nMidiPlayer: Playback Finished.");
            is_playing = false;
            // Optional: synth->allNotesOff();
            return false; // Indicate finished
        }

        // Process the current event
        processCurrentEvent();

        // Advance index and schedule the *next* event
        current_event_index++;
        scheduleNextEvent(current_time_ms);

    } // End if time for event

    return is_playing; // Return true if still playing
}


// --- Private Helper Methods ---

void MidiPlayer::calculateTimingFactors(float bpm) {
    if (bpm <= 0) bpm = 120.0f;
    float microseconds_per_quarter_note = 60000000.0f / bpm;
    if (TICKS_PER_QUARTER_NOTE > 0) {
        millis_per_tick = (microseconds_per_quarter_note / 1000.0f) / (float)TICKS_PER_QUARTER_NOTE;
    } else {
        millis_per_tick = 0.0f;
        Serial.println("MidiPlayer ERROR: TICKS_PER_QUARTER_NOTE is zero!");
    }
}

unsigned long MidiPlayer::convertTicksToMillis(uint16_t ticks) {
    return (unsigned long)roundf((float)ticks * millis_per_tick);
}

uint16_t MidiPlayer::pgm_read_uint16_big_endian(const uint8_t* flash_address) {
    uint8_t highByte = pgm_read_byte_near(flash_address);
    uint8_t lowByte = pgm_read_byte_near(flash_address + 1);
    return ((uint16_t)highByte << 8) | lowByte;
}

void MidiPlayer::processCurrentEvent() {
    if (!synth) return; // Need synth to process

    const uint8_t* current_event_address = current_song_data_ptr + (current_event_index * BYTES_PER_EVENT);

    // Read event data from FLASH
    uint8_t event_type = pgm_read_byte_near(current_event_address + 2); // Offset 2
    uint8_t note_number = pgm_read_byte_near(current_event_address + 3); // Offset 3
    uint8_t velocity = pgm_read_byte_near(current_event_address + 4);    // Offset 4

    // Debug print
    // Serial.printf("MidiPlayer T:%lu E:%u Typ:%u N:%u V:%u\n",
    //              millis(), current_event_index, event_type, note_number, velocity);

    // Execute the event
    if (event_type == 1) { // Note On
        if (velocity > 0) synth->startNote(note_number, velocity);
        else synth->stopNote(note_number); // Note On w/ vel 0 == Note Off
    } else if (event_type == 0) { // Note Off
        synth->stopNote(note_number);
    }
}

void MidiPlayer::scheduleNextEvent(unsigned long current_processing_time_ms) {
     if (current_event_index < current_event_count) {
        const uint8_t* next_event_address = current_song_data_ptr + (current_event_index * BYTES_PER_EVENT);
        uint16_t next_delta_ticks = pgm_read_uint16_big_endian(next_event_address); // Offset 0
        unsigned long next_delta_ms = convertTicksToMillis(next_delta_ticks);

        // Schedule relative to when the current event was processed
        next_event_time_ms = current_processing_time_ms + next_delta_ms;

         // Debug print
        // Serial.printf("  Next event %u scheduled in %lu ms (ticks: %u) at %lu\n",
        //              current_event_index, next_delta_ms, next_delta_ticks, next_event_time_ms);
    } else {
        // This was the last event, update() will catch completion on next call
        // No need to schedule further. Make sure next_event_time_ms doesn't cause issues.
        // Setting it based on last event time is okay, the index check prevents processing.
    }
}