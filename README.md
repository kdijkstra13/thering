<div align="center">

# Thering

### A gesture-controlled digital theremin for the Arduino UNO Q

Move your hand to play notes, turn the knob to shift the starting note, and use the Modulino buttons to shape the instrument—all with optional USB audio and MIDI output.

</div>

---

## Overview

**Thering** is a scale-aware digital theremin built around the **Arduino UNO Q** and Arduino Modulino components. A distance sensor converts hand movement into musical notes, while a knob and three buttons control the starting point, waveform, scale, root note, and MIDI channel.

The project is split across both sides of the UNO Q:

* The **Arduino sketch** reads the Modulino controls and drives the LED matrix and Pixels module.
* The **Python app** generates audio, sends USB MIDI, converts distance into notes, and stores settings on the Linux/eMMC side.

A USB speaker and USB MIDI interface are optional. Thering continues running when either one is missing.

## Features

* Touch-free pitch control using a Modulino Distance sensor
* Scale-quantized notes instead of continuous pitch
* Four selectable waveforms: sine, square, sawtooth, and triangle
* Fifteen MIDI-compatible scales and modes
* Selectable root notes from C through B, including sharps and flats
* USB audio output through the UNO Q Linux side
* USB MIDI output on channels 1–16
* Live MIDI-channel editing with a button-and-knob gesture
* Persistent MIDI channel, waveform, scale, and root settings
* Startup icons showing USB speaker and MIDI availability
* LED matrix graphics for waveforms, root notes, scales, and MIDI channels
* Fast bouncing text for long scale names
* Eight illuminated knob-offset positions
* Pitch hysteresis to prevent unstable note changes near boundaries

## Hardware

### Required

* Arduino UNO Q
* Modulino Distance
* Modulino Knob
* Modulino Buttons
* Modulino Pixels

### Optional

* USB speaker or USB audio device
* USB MIDI interface, synthesizer, or another writable ALSA raw-MIDI device

## Controls

### Distance sensor

Move your hand between approximately **20 mm and 500 mm** from the sensor. Every **20 mm** advances one note in the selected scale.

### Knob

During normal operation, the knob selects a scale offset from **0 to 7**. The active position is shown on the Modulino Pixels module.

### Buttons

| Control | First press when another setting is visible | Press while already visible |
| ------- | ------------------------------------------- | --------------------------- |
| **A**   | Show the current waveform                   | Select the next waveform    |
| **B**   | Show the current scale                      | Select the next scale       |
| **C**   | Show the current root note                  | Select the next root note   |

This avoids changing a setting accidentally: the first press reveals it, and another press changes it. When the same setting is already visible, it changes immediately after the short MIDI-gesture detection window.

### MIDI channel

Hold **A**, **B**, or **C** and turn the knob to select MIDI channel **1–16**.

* The channel changes live while the button remains held.
* The selected number appears on the LED matrix.
* The channel is saved when the button is released.
* The knob then returns to its previous normal 0–7 offset position.

## Scales and modes

All included scales use integer semitone intervals and can be represented directly with standard MIDI note numbers.

### Diatonic scales and modes

* Major / Ionian
* Minor / Aeolian
* Dorian
* Phrygian
* Lydian
* Mixolydian
* Locrian

### Pentatonic, blues, and chromatic

* Major pentatonic
* Minor pentatonic
* Blues
* Chromatic

### East Asian scales

* Hirajoshi
* In Sen
* Iwato
* Yo

> [!IMPORTANT]
> The order of the scale arrays in the Arduino and Python files must always remain identical.

## Display behavior

At startup, the 13×8 LED matrix shows two status icons:

* **Left:** USB speaker status
* **Right:** USB MIDI-output status

The icons remain visible until a button changes the display.

During operation, the matrix can show:

* A waveform graphic
* A large root note with an optional accidental
* A bouncing scale name
* The current MIDI-channel number

## How it works

1. The Arduino reads the distance sensor, knob, and buttons.
2. It sends the current control state to Python through `Arduino_RouterBridge`.
3. Python maps the distance and knob offset to a step in the selected scale.
4. The scale step is converted into a MIDI note and frequency.
5. The note is sent to the optional USB MIDI output.
6. The optional wave generator plays the same pitch through USB audio.

The distance-to-note conversion uses hysteresis, so small sensor fluctuations near a note boundary do not cause rapid switching.

## Installation

1. Create an Arduino UNO Q project containing both the Arduino sketch and Python app.
2. Add the current project files:

   * `theremin_modes_persistent_settings.ino`
   * `theremin_linux_modes_persistent_settings.py`
3. Install or enable the Arduino libraries and UNO Q components used by the project:

   * `Arduino_Modulino`
   * `Arduino_RouterBridge`
   * `Arduino_LED_Matrix`
   * UNO Q Python App Bricks and peripherals
4. Connect the Distance, Knob, Buttons, and Pixels Modulino modules.
5. Connect an optional USB speaker and/or MIDI device.
6. Deploy both parts of the project to the UNO Q.
7. Start the application.

The Arduino and Python processors start together. The Arduino code retries its Bridge calls briefly while the Python handlers are being registered.

## Configuration

### Modulino Pixels brightness

In the Arduino sketch:

```cpp
const int OFFSET_LED_BRIGHTNESS = 40;  // 0-100 percent
```

Lower the value to dim the orange offset LED:

```cpp
const int OFFSET_LED_BRIGHTNESS = 15;
```

### Offset LED color

```cpp
ModulinoColor OFFSET_LED_COLOR(255, 80, 0);  // Orange
```

### Scale-name bounce speed

```cpp
const unsigned long SCALE_BOUNCE_INTERVAL_MS = 45;
```

A smaller number moves the text faster. A larger number moves it slower.

For example:

```cpp
// Faster
const unsigned long SCALE_BOUNCE_INTERVAL_MS = 25;

// Slower
const unsigned long SCALE_BOUNCE_INTERVAL_MS = 80;
```

### Padding around scale names

Add spaces around the entries in `scaleDisplayNames`:

```cpp
const char* scaleDisplayNames[] = {
  "  MAJOR  ",
  "  MINOR  ",
  "  DORIAN  "
  // ...
};
```

Each space occupies one blank character cell and creates extra travel before the text reverses direction.

### Button-and-knob gesture timing

```cpp
const unsigned long BUTTON_MIDI_GESTURE_GRACE_MS = 100;
```

Increase this value slightly when more time is needed to begin turning the knob before a repeated button press changes its normal setting.

### Distance response

In the Python app:

```python
MIN_DISTANCE = 20
MAX_DISTANCE = 500
NOTE_STEP_MM = 20
HYSTERESIS_MM = 5
```

### Audio level

```python
FIXED_AMPLITUDE = 0.6
```

### Default MIDI channel

```python
DEFAULT_MIDI_CHANNEL = 13
```

### Select a specific MIDI device

By default, Thering opens the first writable ALSA raw-MIDI output.

To force a particular device:

```python
MIDI_OUTPUT_DEVICE_PATH = "/dev/snd/midiC2D0"
```

## Persistent settings

Settings are stored on the UNO Q Linux/eMMC side under:

```text
~/.config/thering/
```

Files used by the current version:

```text
midi_channel.txt
musical_settings.txt
```

The following values are restored at startup:

* MIDI channel
* Waveform
* Scale
* Root note

A waveform, scale, or root is saved only when it changes—not when a button merely reveals its current value.

### No sound

* Check whether the startup speaker icon reports a connected device.
* Confirm that Linux detects the USB playback device.
* Check the Python console for `USB speaker unavailable` messages.
* MIDI can continue working even when audio is unavailable.

### No MIDI output

* Confirm that the MIDI interface appears as `/dev/snd/midiC*D*`.
* Check the startup MIDI icon.
* Set `MIDI_OUTPUT_DEVICE_PATH` explicitly when automatic selection chooses the wrong device.
* Thering periodically retries missing MIDI devices.

### The wrong scale plays

The scale lists in the Arduino and Python files are index-based. Keep their entries in exactly the same order whenever adding, removing, or rearranging scales.

### MIDI editing changes a normal setting instead

Begin turning the knob shortly after pressing and holding the button. Increase `BUTTON_MIDI_GESTURE_GRACE_MS` when the recognition window feels too short.

### Notes change too easily near a boundary

Increase the Python hysteresis value:

```python
HYSTERESIS_MM = 5
```

A larger value makes note boundaries more stable but requires more hand movement before returning to the previous note.

## Adding another scale

Add the scale to the Arduino `scaleNames` and `scaleDisplayNames` arrays:

```cpp
const char* scaleNames[] = {
  // Existing scales...
  "new scale"
};

const char* scaleDisplayNames[] = {
  // Existing labels...
  "NEW SCALE"
};
```

Then add the matching interval list at the same index in Python:

```python
SCALES = [
    # Existing scales...
    {
        "name": "new scale",
        "intervals": [0, 2, 4, 7, 9],
    },
]
```

Finally, update `SCALE_COUNT` in the Arduino sketch.

Only use integer semitone intervals when the scale must be played through standard MIDI notes.

## License

GPL v3 `LICENSE`

## Acknowledgements

Built with:

* Arduino UNO Q
* Arduino Modulino modules
* Arduino Router Bridge
* Arduino LED Matrix
* UNO Q Python App Bricks

---

 



