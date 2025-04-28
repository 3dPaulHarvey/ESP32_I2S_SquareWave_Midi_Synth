import mido
import sys

def parse_midi_to_arduino_array(midi_file_path):
    """
    Parse a MIDI file and output a C-style array initialization
    that can be copied directly into Arduino code.
    """
    try:
        mid = mido.MidiFile(midi_file_path)
    except Exception as e:
        print(f"Error opening MIDI file: {e}")
        return
    
    # Structure to hold our parsed events
    events = []
    
    # Process all tracks
    for track in mid.tracks:
        track_time = 0
        for msg in track:
            # Update track time
            track_time += msg.time
            
            # Only process note_on and note_off events
            if msg.type == 'note_on' or msg.type == 'note_off':
                # event_type: 0 = note off, 1 = note on
                event_type = 1 if msg.type == 'note_on' and msg.velocity > 0 else 0
                
                # Store event with absolute time
                events.append({
                    'time': track_time,
                    'type': event_type,
                    'note': msg.note,
                    'velocity': msg.velocity,
                    'channel': msg.channel
                })
    
    # Sort events by time
    events.sort(key=lambda x: x['time'])
    
    # Convert to delta time (time between events)
    last_time = 0
    for event in events:
        delta = event['time'] - last_time
        last_time = event['time']
        event['delta'] = delta
    
    # Generate array data
    array_data = "{"
    
    # Add all events with delta time split into two bytes
    for i, event in enumerate(events):
        if i > 0:
            array_data += ", "
        
        # Split 16-bit delta time into two separate bytes
        delta_high = (event['delta'] >> 8) & 0xFF  # High byte
        delta_low = event['delta'] & 0xFF          # Low byte
        
        # Format as: high byte, low byte, type, note, velocity, channel
        array_data += f"0x{delta_high:02x}, 0x{delta_low:02x}, 0x{event['type']:02x}, 0x{event['note']:02x}, 0x{event['velocity']:02x}, 0x{event['channel']:02x}"
    
    array_data += "}"
    
    # Print the array declaration header
    print(f"// MIDI data from {midi_file_path}")
    print(f"// Format: delta_time_high(8bit), delta_time_low(8bit), event_type(8bit), note(8bit), velocity(8bit), channel(8bit)")
    print(f"// event_type: 0 = note off, 1 = note on")
    print(f"const uint8_t MIDI_DATA[] PROGMEM = {array_data};")
    print(f"const uint16_t MIDI_EVENT_COUNT = {len(events)};")
    print(f"const uint8_t MIDI_BYTES_PER_EVENT = 6;  // Now 6 bytes per event")
    
    # Print helper function for accessing the data
    print("""
// Helper function to read an event from PROGMEM
void readMidiEvent(uint16_t index, uint16_t &delta, uint8_t &type, uint8_t &note, uint8_t &velocity, uint8_t &channel) {
  uint16_t pos = index * MIDI_BYTES_PER_EVENT;
  // Combine the two delta time bytes
  delta = (pgm_read_byte(&MIDI_DATA[pos]) << 8) | pgm_read_byte(&MIDI_DATA[pos + 1]);
  type = pgm_read_byte(&MIDI_DATA[pos + 2]);
  note = pgm_read_byte(&MIDI_DATA[pos + 3]);
  velocity = pgm_read_byte(&MIDI_DATA[pos + 4]);
  channel = pgm_read_byte(&MIDI_DATA[pos + 5]);
}
""")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python myMidiParse.py <midi_file>")
        sys.exit(1)
        
    midi_file = sys.argv[1]
    parse_midi_to_arduino_array(midi_file)