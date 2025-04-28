                                                                   #include "Synthesizer.h"
#include <Arduino.h> // For Serial, constrain, roundf, powf, etc.
#include <cmath>     // For roundf, powf

// Constructor: Initialize basic members
Synthesizer::Synthesizer() :
    i2s_port(I2S_NUM_0), // Or pass as parameter if needed
    voicesMutex(NULL)
{
    // Initialize I2S Config Struct
    i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SYNTH_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024, // Samples per buffer
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = I2S_PIN_NO_CHANGE
    };

    // Initialize Pin Config Struct
    i2s_pin_config = {
        .bck_io_num = I2S_BCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_DO_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
}

// Initialize I2S, Mutex, Voices, and start Audio Task
bool Synthesizer::init() {
    Serial.println("Synthesizer initializing...");

    // 1. Create Mutex
    voicesMutex = xSemaphoreCreateMutex();
    if (voicesMutex == NULL) {
        Serial.println("Error: Failed to create voices mutex!");
        return false;
    }
    Serial.println("- Mutex created.");

    // 2. Initialize Voices (safely using mutex)
    if (xSemaphoreTake(voicesMutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < SYNTH_MAX_VOICES; ++i) {
            voices[i].isActive = false;
            // Reset other fields if desired
        }
        xSemaphoreGive(voicesMutex);
        Serial.println("- Voices initialized.");
    } else {
         Serial.println("Error: Failed to take mutex for voice init!");
         return false;
    }

    // 3. Configure I2S Driver
    esp_err_t err;
    err = i2s_driver_install(i2s_port, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("Error: Failed to install I2S driver: %s\n", esp_err_to_name(err));
        return false;
    }
    err = i2s_set_pin(i2s_port, &i2s_pin_config);
    if (err != ESP_OK) {
        Serial.printf("Error: Failed to set I2S pins: %s\n", esp_err_to_name(err));
        // Consider calling i2s_driver_uninstall here
        return false;
    }
    Serial.println("- I2S driver configured.");

    // 4. Start Audio Task
    BaseType_t task_created = xTaskCreatePinnedToCore(
        audioTaskWrapper,   // Static wrapper function
        "SynthAudioTask",   // Task name
        8192,               // Stack size (increase if needed)
        this,               // Pass instance pointer as parameter
        configMAX_PRIORITIES - 1, // High priority
        NULL,               // Task handle (optional)
        1                   // Core ID (0 or 1)
    );

    if (task_created != pdPASS) {
         Serial.println("Error: Failed to create Audio Task!");
         // Consider cleanup (mutex deletion, i2s uninstall)
         return false;
    }
    Serial.println("- Audio task created on Core 1.");

    Serial.println("Synthesizer initialization complete.");
    return true;
}


// --- Public Note Control Methods ---

void Synthesizer::startNote(int noteNumber, int velocity) {
    if (xSemaphoreTake(voicesMutex, portMAX_DELAY) == pdTRUE) {
        int existingVoiceIndex = findVoicePlayingNote_unsafe(noteNumber);
        if (existingVoiceIndex != -1) {
             voices[existingVoiceIndex].isActive = false; // Stop existing note first
        }

        int voiceIndex = findFreeVoice_unsafe();
        if (voiceIndex != -1) {
            voices[voiceIndex].isActive = true;
            voices[voiceIndex].midiNoteNumber = noteNumber;
            voices[voiceIndex].frequency = midiNoteToFrequency(noteNumber);
            voices[voiceIndex].targetAmplitude = velocityToAmplitude(velocity);
            voices[voiceIndex].wavelength = calculate_wavelength(voices[voiceIndex].frequency);

            if (voices[voiceIndex].wavelength > 0 && voices[voiceIndex].targetAmplitude != 0) {
                voices[voiceIndex].currentOutput = voices[voiceIndex].targetAmplitude; // Start high
                voices[voiceIndex].timeAtLevelRemaining = voices[voiceIndex].wavelength;
            } else {
                // Invalid note, deactivate immediately
                voices[voiceIndex].isActive = false;
                 Serial.printf("Warning: Cannot start note %d (freq=%.2f, amp=%d, wl=%d)\n",
                              noteNumber, voices[voiceIndex].frequency, voices[voiceIndex].targetAmplitude, voices[voiceIndex].wavelength);
            }
        } else {
             Serial.println("Warning: No free voices!");
             // Implement voice stealing here if needed
        }
        xSemaphoreGive(voicesMutex);
    }
}

void Synthesizer::stopNote(int noteNumber) {
    if (xSemaphoreTake(voicesMutex, portMAX_DELAY) == pdTRUE) {
        int voiceIndex = findVoicePlayingNote_unsafe(noteNumber);
        if (voiceIndex != -1) {
            voices[voiceIndex].isActive = false;
            voices[voiceIndex].currentOutput = 0; // Stop sound immediately
        }
        xSemaphoreGive(voicesMutex);
    }
}


// --- Private Helper Methods ---

float Synthesizer::midiNoteToFrequency(int midiNote) {
     if (midiNote <= 0) return 0.0f;
    return 440.0f * powf(2.0f, (float)(midiNote - 69) / 12.0f);
}

int16_t Synthesizer::velocityToAmplitude(int velocity) {
    velocity = constrain(velocity, 0, 127);
    float amp_f = ((float)velocity / 127.0f) * (float)SYNTH_MAX_NOTE_AMPLITUDE;
    return (int16_t)roundf(amp_f);
}

uint16_t Synthesizer::calculate_wavelength(float frequency) {
    if (frequency <= 0) return 0;
    float samples_per_half_cycle_f = (float)SYNTH_SAMPLE_RATE / (frequency * 2.0f);
    uint16_t wavelength = (uint16_t)roundf(samples_per_half_cycle_f);
    return (wavelength < 1) ? 1 : wavelength;
}

int Synthesizer::findFreeVoice_unsafe() {
    for (int i = 0; i < SYNTH_MAX_VOICES; ++i) {
        if (!voices[i].isActive) return i;
    }
    return -1;
}

int Synthesizer::findVoicePlayingNote_unsafe(int midiNoteNumber) {
     for (int i = 0; i < SYNTH_MAX_VOICES; ++i) {
        if (voices[i].isActive && voices[i].midiNoteNumber == midiNoteNumber) return i;
    }
    return -1;
}

void Synthesizer::send_sample_to_i2s(int16_t sample_value) {
    uint32_t sample_32bit = ((uint32_t)(sample_value & 0xFFFF) << 16) | (sample_value & 0xFFFF);
    size_t bytes_written = 0;
    i2s_write(i2s_port, &sample_32bit, sizeof(sample_32bit), &bytes_written, portMAX_DELAY);
}


// --- Audio Task Implementation ---

// Static wrapper function required by xTaskCreate
void Synthesizer::audioTaskWrapper(void* instance) {
    // Cast the instance pointer back to Synthesizer* and call the member function
    if (instance) {
        static_cast<Synthesizer*>(instance)->audioTaskRunner();
    }
    // Task should not return, but if it does, delete it.
     vTaskDelete(NULL);
}

// The actual audio generation loop running in the task
void Synthesizer::audioTaskRunner() {
    Serial.println("Synthesizer::audioTaskRunner started.");
    while (true) {
        int32_t summedSample_raw = 0;
        int activeVoiceCount = 0;

        // Safely access and update voices using the mutex
        if (xSemaphoreTake(voicesMutex, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < SYNTH_MAX_VOICES; ++i) {
                if (voices[i].isActive) {
                    activeVoiceCount++;
                    VoiceState &voice = voices[i]; // Use reference

                    // Update square wave state
                    if (voice.timeAtLevelRemaining == 0) {
                        voice.currentOutput = (voice.currentOutput == voice.targetAmplitude)
                                                 ? -voice.targetAmplitude
                                                 : voice.targetAmplitude;
                        voice.timeAtLevelRemaining = voice.wavelength;
                    }
                    if(voice.timeAtLevelRemaining > 0) {
                       voice.timeAtLevelRemaining--;
                    }

                    summedSample_raw += voice.currentOutput;
                }
            }
            xSemaphoreGive(voicesMutex); // Release mutex
        } // End mutex lock

        // Mix, Scale, Clamp
        int16_t finalSample = 0;
        if (activeVoiceCount > 0) {
            float mixedSample_f = (float)summedSample_raw / (float)activeVoiceCount; // Averaging

            // Hard Clamping
            if (mixedSample_f > SYNTH_MAX_OUTPUT_AMPLITUDE) finalSample = SYNTH_MAX_OUTPUT_AMPLITUDE;
            else if (mixedSample_f < SYNTH_MIN_OUTPUT_AMPLITUDE) finalSample = SYNTH_MIN_OUTPUT_AMPLITUDE;
            else finalSample = (int16_t)roundf(mixedSample_f);
        } else {
            finalSample = SYNTH_SILENCE_AMPLITUDE;
        }

        // Send to I2S (this blocks, pacing the loop)
        send_sample_to_i2s(finalSample);
    } // End while(true)
}