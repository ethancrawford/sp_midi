// MIT License

// Copyright (c) 2016-2020 Luis Lloret

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <regex>
#include <algorithm>
#include <sstream>
#include <cassert>
#include "sp_midi.h"
#include "midiinprocessor.h"
#include "osc/OscOutboundPacketStream.h"
#include "utils.h"

using namespace std;
using namespace juce;

MidiInProcessor::MidiInProcessor(const std::string& inputName, bool isVirtual) : m_oscRawMidiMessage(false)
{
    m_input = make_unique<MidiIn>(inputName, this, isVirtual);
}

void MidiInProcessor::handleIncomingMidiMessage(MidiInput* source, const juce::MidiMessage& midiMessage)
{
    unsigned char channel = 0xff, status = 0;
    string message_type;
    const uint8_t* message = midiMessage.getRawData();
    int nBytes = midiMessage.getRawDataSize();

    assert(nBytes > 0);

    if ((message[0] & 0xf0) != 0xf0) {
        channel = message[0] & 0x0f;
        channel++; // Make channel 1-16, instead of 0-15
        status = message[0] & 0xf0;
    } else {
        status = message[0];
    }

    dumpMIDIMessage(message, nBytes);

    // Process the message
    switch (status) {
    case 0x80:
        message_type = "note_off";
        assert(nBytes == 3);
        break;

    case 0x90:
        message_type = "note_on";
        assert(nBytes == 3);
        break;

    case 0xA0:
        message_type = "polyphonic_key_pressure";
        assert(nBytes == 3);
        break;

    case 0xB0:
        message_type = "control_change";
        assert(nBytes == 3);
        break;

    case 0xC0:
        message_type = "program_change";
        assert(nBytes == 2);
        break;

    case 0xD0:
        message_type = "channel_pressure";
        assert(nBytes == 2);
        break;

    case 0xE0:
        message_type = "pitch_bend";
        assert(nBytes == 3);
        break;

    case 0xF0:
        message_type = "sysex";
        // Remove the end of message marker if raw message is not specified
        if (!m_oscRawMidiMessage)
            nBytes--;
        break;

    case 0xF1:
        message_type = "MTC";
        assert(nBytes == 2);
        break;

    case 0xF2:
        message_type = "song_position";
        assert(nBytes == 3);
        break;

    case 0xF3:
        message_type = "song_select";
        assert(nBytes == 2);
        break;

    case 0xF4:
    case 0xF5:
        message_type = "syscommon_undefined";
        assert(nBytes == 1);
        break;

    case 0xF6:
        message_type = "tune_request";
        assert(nBytes == 1);
        break;

    case 0xF8:
        message_type = "clock";
        assert(nBytes == 1);
        break;

    case 0xF9:
    case 0xFD:
        message_type = "sysrt_undefined";
        assert(nBytes == 1);
        break;

    case 0xFA:
        message_type = "start";
        assert(nBytes == 1);
        break;

    case 0xFB:
        message_type = "continue";
        assert(nBytes == 1);
        break;

    case 0xFC:
        message_type = "stop";
        assert(nBytes == 1);
        break;

    case 0xFE:
        message_type = "active_sensing";
        assert(nBytes == 1);
        break;

    default:
        message_type = "unknown_message";
        break;
    }

    // Prepare the OSC address
    stringstream path;
    string normalizedPortName(m_input->getNormalizedPortName());
    int portId = m_input->getPortId();
    path << "/midi:" << normalizedPortName << ":" << portId << ":";
    if (channel != 0xff) {
        path << static_cast<int>(channel);
    }
    path << "/" << message_type;

    // And now prepare the OSC message body
    char buffer[1024];
    osc::OutboundPacketStream p(buffer, 1024);
    p << osc::BeginMessage(path.str().c_str());

    // send the raw midi message as part of the body
    // TODO: do we want a raw midi message?
    if (m_oscRawMidiMessage) {
        if (nBytes > 0) {
            p << osc::Blob(message, static_cast<osc::osc_bundle_element_size_t>(nBytes));
        }
    } else {
        // We treat the pitch bend differently. Instead of sending the bytes separately,
        // we send the processed 14 bits value
        if (midiMessage.isPitchWheel()) {
            p << (int)(midiMessage.getPitchWheelValue());
        }
        else {
            for (int i = 1; i < nBytes; i++) {
                p << (int)message[i];
            }
        }
    }
    p << osc::EndMessage;

    // Dump the OSC message
    m_logger.info("sending OSC: [{}] -> {}, {}", path.str(), portId, normalizedPortName);
    if (m_oscRawMidiMessage) {
        if (nBytes > 0) {
            m_logger.info("  <raw_midi_message>");
        }
    } else {
        for (int i = 1; i < nBytes; i++) {
            m_logger.info("   [{:02x}]", (int)message[i]);
        }
    }

    // And send the message to the erlang process
    send_midi_osc_to_erlang(p.Data(), p.Size());
}


void MidiInProcessor::dumpMIDIMessage(const uint8_t* message, int size) const
{
    m_logger.info("received MIDI message: ");
    for (int i = 0; i < size; i++) {
        m_logger.info("   [{:02x}]", (unsigned int)message[i]);
    }
}
