#ifndef SYNTHESIZER_H
#define SYNTHESIZER_H

#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cstdint> // For standard integer types like int16_t

// --- Configuration Constants ---
// You might move these into the class later or pass them via constructor if needed
const int SYNTH_SAMPLE_RATE = 44100;
const int SYNTH_BITS_PER_SAMPLE = 16;
const int SYNTH_MAX_VOICES = 8;               // Max simultaneous notes
const int16_t SYNTH_MAX_NOTE_AMPLITUDE = 16000;// Max amplitude for ONE note (tune this!) //4000
const int16_t SYNTH_MAX_OUTPUT_AMPLITUDE = 32767; // Absolute MAX output
const int16_t SYNTH_MIN_OUTPUT_AMPLITUDE = -32768;// Absolute MIN output
const int16_t SYNTH_SILENCE_AMPLITUDE = 0;

// --- I2S Pin Configuration ---
// Define pins here, or pass them to init()
const int I2S_BCK_PIN = 27;
const int I2S_WS_PIN = 26;
const int I2S_DO_PIN = 25;

// --- Voice State Structure ---
// Kept internal to the synthesizer concept
struct VoiceState
{
    bool isActive = false;
    int midiNoteNumber = 0;
    float frequency = 0.0f;
    int16_t targetAmplitude = 0;
    int16_t currentOutput = 0;
    uint16_t wavelength = 0;
    uint16_t timeAtLevelRemaining = 0;
};


class Synthesizer {
public:
    // Constructor
    Synthesizer();

    // Destructor (optional, for cleanup if needed)
    // ~Synthesizer();

    // Call this in setup()
    bool init();

    // Public interface to control notes
    void startNote(int noteNumber, int velocity);
    void stopNote(int noteNumber);

private:
    // --- I2S Members ---
    i2s_port_t i2s_port;
    i2s_config_t i2s_config;
    i2s_pin_config_t i2s_pin_config;

    // --- Voice Management Members ---
    VoiceState voices[SYNTH_MAX_VOICES];
    SemaphoreHandle_t voicesMutex;

    // --- Private Helper Methods ---
    float midiNoteToFrequency(int midiNote);
    int16_t velocityToAmplitude(int velocity);
    uint16_t calculate_wavelength(float frequency);
    int findFreeVoice_unsafe(); // Must hold mutex before calling
    int findVoicePlayingNote_unsafe(int midiNoteNumber); // Must hold mutex

    // --- Audio Task ---
    void send_sample_to_i2s(int16_t sample_value);
    void audioTaskRunner(); // The actual task loop method
    static void audioTaskWrapper(void* instance); // Static wrapper for xTaskCreate
};

#endif // SYNTHESIZER_H