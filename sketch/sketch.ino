#include <Arduino_Modulino.h>
#include <Arduino_RouterBridge.h>
#include <Arduino_LED_Matrix.h>


ModulinoDistance distanceSensor;
ModulinoKnob knob;
ModulinoButtons buttons;
ModulinoPixels offsetPixels;
Arduino_LED_Matrix matrix;

const int MATRIX_WIDTH = 13;
const int MATRIX_HEIGHT = 8;
uint8_t frame[MATRIX_WIDTH * MATRIX_HEIGHT] = {0};

const int OFFSET_LED_COUNT = 8;
const int OFFSET_LED_BRIGHTNESS = 20;  // 0-100 percent
ModulinoColor OFFSET_LED_COLOR(50, 255, 0);  // Orange

unsigned long previousUpdate = 0;
unsigned long lastValidDistance = 0;
unsigned long previousBounceUpdate = 0;

bool toneIsPlaying = false;

const int DEFAULT_WAVEFORM_INDEX = 0;
const int DEFAULT_SCALE_INDEX = 0;
const int DEFAULT_ROOT_INDEX = 0;
const int DEFAULT_MIDI_CHANNEL = 13;

int waveformIndex = DEFAULT_WAVEFORM_INDEX;
int scaleIndex = DEFAULT_SCALE_INDEX;
int rootIndex = DEFAULT_ROOT_INDEX;
int midiChannel = DEFAULT_MIDI_CHANNEL;
int lastDisplayedKnob = -1;

const int MIDI_CHANNEL_MIN = 1;
const int MIDI_CHANNEL_MAX = 16;

const int STARTUP_WARNING_NO_SPEAKER = 0x01;
const int STARTUP_WARNING_NO_MIDI_OUTPUT = 0x02;


const int WAVEFORM_COUNT = 4;
const int SCALE_COUNT = 15;
const int ROOT_COUNT = 12;

const char* waveformNames[] = {
  "Sine", "Square", "Sawtooth", "Triangle"
};

// Keep this order exactly synchronized with SCALES in the Python app.
// Major is Ionian and minor is Aeolian, so those aliases are not duplicated.
const char* scaleNames[] = {
  "major",
  "minor",
  "Dorian",
  "Phrygian",
  "Lydian",
  "Mixolydian",
  "Locrian",
  "major pentatonic",
  "minor pentatonic",
  "blues",
  "chromatic",
  "Hirajoshi",
  "In Sen",
  "Iwato",
  "Yo"
};

// Scale names bounce left and right using the readable 4x6 font.
const char* scaleDisplayNames[] = {
  "  MAJOR  ",
  "  MINOR  ",
  "  DORIAN  ",
  "  PHRYGIAN  ",
  "  LYDIAN  ",
  "  MIXOLYDIAN  ",
  "  LOCRIAN  ",
  "  MAJOR PENTA  ",
  "  MINOR PENTA  ",
  "  BLUES  ",
  "  CHROMATIC  ",
  "  HIRAJOSHI  ",
  "  IN SEN  ",
  "  IWATO  ",
  "  YO  "
};

const char* rootNames[] = {
  "C", "C#", "D", "Eb", "E", "F",
  "F#", "G", "Ab", "A", "Bb", "B"
};

const char rootLetters[] = {
  'C', 'C', 'D', 'E', 'E', 'F',
  'F', 'G', 'A', 'A', 'B', 'B'
};

const char rootAccidentals[] = {
  ' ', '#', ' ', 'b', ' ', ' ',
  '#', ' ', 'b', ' ', 'b', ' '
};

// --------------------------------------------------
// Display state
// --------------------------------------------------

enum MatrixDisplayMode {
  DISPLAY_STARTUP_STATUS,
  DISPLAY_ROOT,
  DISPLAY_SCALE,
  DISPLAY_WAVEFORM,
  DISPLAY_MIDI_CHANNEL
};

MatrixDisplayMode matrixDisplayMode = DISPLAY_ROOT;

// A short grace period lets a repeated button press feel immediate,
// while still allowing "hold button + turn knob" to be recognized
// as a MIDI-channel gesture instead of a setting change.
const unsigned long BUTTON_MIDI_GESTURE_GRACE_MS = 100;

const char BUTTON_NONE = '\0';
const char BUTTON_WAVEFORM = 'A';
const char BUTTON_SCALE = 'B';
const char BUTTON_ROOT = 'C';

char lastDisplayedSettingButton = BUTTON_NONE;
char gestureButton = BUTTON_NONE;
bool buttonGestureActive = false;
bool buttonAdvancePending = false;
bool midiChannelGestureActive = false;
unsigned long buttonGestureStartedAt = 0;
int gestureStartKnob = 0;
int gestureStartMidiChannel = DEFAULT_MIDI_CHANNEL;
int gestureNormalKnobValue = 0;
int normalKnobValue = 0;

const unsigned long SCALE_BOUNCE_INTERVAL_MS = 60;
const int SMALL_GLYPH_WIDTH = 4;
const int SMALL_GLYPH_HEIGHT = 6;
const int SMALL_GLYPH_SPACING = 1;

const char* activeScaleText = nullptr;
int scaleBounceX = 0;
int scaleBounceDirection = -1;

// Readable 4x6 alphabet used by the fast bouncing scale names.
// Each row is four bits wide.
const uint8_t FONT_4X6[26][6] = {
  {0b0110, 0b1001, 0b1001, 0b1111, 0b1001, 0b1001}, // A
  {0b1110, 0b1001, 0b1110, 0b1001, 0b1001, 0b1110}, // B
  {0b0111, 0b1000, 0b1000, 0b1000, 0b1000, 0b0111}, // C
  {0b1110, 0b1001, 0b1001, 0b1001, 0b1001, 0b1110}, // D
  {0b1111, 0b1000, 0b1110, 0b1000, 0b1000, 0b1111}, // E
  {0b1111, 0b1000, 0b1110, 0b1000, 0b1000, 0b1000}, // F
  {0b0111, 0b1000, 0b1011, 0b1001, 0b1001, 0b0111}, // G
  {0b1001, 0b1001, 0b1111, 0b1001, 0b1001, 0b1001}, // H
  {0b1111, 0b0110, 0b0110, 0b0110, 0b0110, 0b1111}, // I
  {0b0011, 0b0001, 0b0001, 0b0001, 0b1001, 0b0110}, // J
  {0b1001, 0b1010, 0b1100, 0b1010, 0b1001, 0b1001}, // K
  {0b1000, 0b1000, 0b1000, 0b1000, 0b1000, 0b1111}, // L
  {0b1001, 0b1111, 0b1111, 0b1001, 0b1001, 0b1001}, // M
  {0b1001, 0b1101, 0b1101, 0b1011, 0b1011, 0b1001}, // N
  {0b0110, 0b1001, 0b1001, 0b1001, 0b1001, 0b0110}, // O
  {0b1110, 0b1001, 0b1110, 0b1000, 0b1000, 0b1000}, // P
  {0b0110, 0b1001, 0b1001, 0b1011, 0b0110, 0b0001}, // Q
  {0b1110, 0b1001, 0b1110, 0b1010, 0b1001, 0b1001}, // R
  {0b0111, 0b1000, 0b0110, 0b0001, 0b0001, 0b1110}, // S
  {0b1111, 0b0110, 0b0110, 0b0110, 0b0110, 0b0110}, // T
  {0b1001, 0b1001, 0b1001, 0b1001, 0b1001, 0b0110}, // U
  {0b1001, 0b1001, 0b1001, 0b1001, 0b0110, 0b0110}, // V
  {0b1001, 0b1001, 0b1001, 0b1111, 0b1111, 0b1001}, // W
  {0b1001, 0b1001, 0b0110, 0b0110, 0b1001, 0b1001}, // X
  {0b1001, 0b1001, 0b0110, 0b0110, 0b0110, 0b0110}, // Y
  {0b1111, 0b0001, 0b0010, 0b0100, 0b1000, 0b1111}  // Z
};

// Large 5x7 root letters A-G.
const uint8_t ROOT_FONT[7][7] = {
  {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}, // A
  {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}, // B
  {0b01111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b01111}, // C
  {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110}, // D
  {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}, // E
  {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}, // F
  {0b01111, 0b10000, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111}  // G
};


// Large 5x7 digits used by the MIDI-channel display.
const uint8_t DIGIT_FONT[10][7] = {
  {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}, // 0
  {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}, // 1
  {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}, // 2
  {0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110}, // 3
  {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}, // 4
  {0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110}, // 5
  {0b01110, 0b10000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}, // 6
  {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}, // 7
  {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}, // 8
  {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00001, 0b01110}  // 9
};

// --------------------------------------------------
// Matrix helpers
// --------------------------------------------------

void clearMatrixBuffer() {
  for (int i = 0; i < MATRIX_WIDTH * MATRIX_HEIGHT; i++) {
    frame[i] = 0;
  }
}

void setMatrixPixel(int x, int y, bool on = true) {
  if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT) {
    return;
  }

  frame[y * MATRIX_WIDTH + x] = on ? 1 : 0;
}

void renderMatrix() {
  matrix.draw(frame);
}

void drawSmallGlyph(char character, int startX, int startY) {
  if (character < 'A' || character > 'Z') {
    return;
  }

  const uint8_t* glyph = FONT_4X6[character - 'A'];

  for (int row = 0; row < SMALL_GLYPH_HEIGHT; row++) {
    for (int column = 0; column < SMALL_GLYPH_WIDTH; column++) {
      if (glyph[row] & (1 << (3 - column))) {
        setMatrixPixel(startX + column, startY + row);
      }
    }
  }
}

void drawSmallText(const char* text, int startX, int startY) {
  int cursorX = startX;

  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] != ' ') {
      drawSmallGlyph(text[i], cursorX, startY);
    }

    cursorX += SMALL_GLYPH_WIDTH + SMALL_GLYPH_SPACING;
  }
}

int smallTextWidth(const char* text) {
  int characterCount = 0;

  while (text[characterCount] != '\0') {
    characterCount++;
  }

  if (characterCount == 0) {
    return 0;
  }

  return characterCount *
    (SMALL_GLYPH_WIDTH + SMALL_GLYPH_SPACING) -
    SMALL_GLYPH_SPACING;
}

void drawLargeRootLetter(char character, int startX, int startY) {
  int glyphIndex = character - 'A';

  if (glyphIndex < 0 || glyphIndex > 6) {
    return;
  }

  for (int row = 0; row < 7; row++) {
    for (int column = 0; column < 5; column++) {
      if (ROOT_FONT[glyphIndex][row] & (1 << (4 - column))) {
        setMatrixPixel(startX + column, startY + row);
      }
    }
  }
}

void drawLargeDigit(int digit, int startX, int startY) {
  if (digit < 0 || digit > 9) {
    return;
  }

  for (int row = 0; row < 7; row++) {
    for (int column = 0; column < 5; column++) {
      if (DIGIT_FONT[digit][row] & (1 << (4 - column))) {
        setMatrixPixel(startX + column, startY + row);
      }
    }
  }
}

void drawAccidental(char accidental, int startX, int startY) {
  static const uint8_t SHARP[7] = {
    0b101, 0b101, 0b111, 0b101, 0b111, 0b101, 0b101
  };

  static const uint8_t FLAT[7] = {
    0b100, 0b100, 0b100, 0b110, 0b101, 0b110, 0b000
  };

  const uint8_t* glyph = nullptr;

  if (accidental == '#') {
    glyph = SHARP;
  } else if (accidental == 'b') {
    glyph = FLAT;
  } else {
    return;
  }

  for (int row = 0; row < 7; row++) {
    for (int column = 0; column < 3; column++) {
      if (glyph[row] & (1 << (2 - column))) {
        setMatrixPixel(startX + column, startY + row);
      }
    }
  }
}

// --------------------------------------------------
// Root display
// --------------------------------------------------

void showRootDisplay() {
  matrixDisplayMode = DISPLAY_ROOT;
  activeScaleText = nullptr;
  clearMatrixBuffer();

  char letter = rootLetters[rootIndex];
  char accidental = rootAccidentals[rootIndex];

  if (accidental == ' ') {
    drawLargeRootLetter(letter, 4, 0);
  } else {
    drawLargeRootLetter(letter, 1, 0);
    drawAccidental(accidental, 8, 0);
  }

  renderMatrix();
}

// --------------------------------------------------
// Fast bouncing scale-name display
// --------------------------------------------------

void drawScaleBounceFrame() {
  clearMatrixBuffer();
  drawSmallText(activeScaleText, scaleBounceX, 1);
  renderMatrix();
}

void showScaleDisplay() {
  matrixDisplayMode = DISPLAY_SCALE;
  activeScaleText = scaleDisplayNames[scaleIndex];
  previousBounceUpdate = millis();

  int textWidth = smallTextWidth(activeScaleText);

  if (textWidth <= MATRIX_WIDTH) {
    scaleBounceX = (MATRIX_WIDTH - textWidth) / 2;
    scaleBounceDirection = 0;
  } else {
    // Start with the beginning of the name visible, then move left.
    scaleBounceX = 0;
    scaleBounceDirection = -1;
  }

  drawScaleBounceFrame();
}

void updateScaleBounce() {
  if (
    matrixDisplayMode != DISPLAY_SCALE ||
    activeScaleText == nullptr ||
    scaleBounceDirection == 0
  ) {
    return;
  }

  unsigned long now = millis();

  if (
    now - previousBounceUpdate <
    SCALE_BOUNCE_INTERVAL_MS
  ) {
    return;
  }

  previousBounceUpdate = now;

  int textWidth = smallTextWidth(activeScaleText);
  int leftLimit = MATRIX_WIDTH - textWidth;
  int rightLimit = 0;

  scaleBounceX += scaleBounceDirection;

  if (scaleBounceX <= leftLimit) {
    scaleBounceX = leftLimit;
    scaleBounceDirection = 1;
  } else if (scaleBounceX >= rightLimit) {
    scaleBounceX = rightLimit;
    scaleBounceDirection = -1;
  }

  drawScaleBounceFrame();
}

// --------------------------------------------------
// Startup USB-device status icons
//
// Left: USB speaker
// Right: MIDI output
// A missing speaker uses a separate X beside the speaker body,
// so the speaker itself remains recognizable.
// The icons remain visible until a button changes the display.
// --------------------------------------------------

void drawMissingStrike(int startX) {
  for (int i = 0; i < 6; i++) {
    setMatrixPixel(startX + i, 6 - i);
  }
}

void drawSpeakerIcon(int startX, bool present) {
  // Compact speaker body in the left three columns.
  setMatrixPixel(startX, 2);
  setMatrixPixel(startX, 3);
  setMatrixPixel(startX, 4);

  for (int y = 1; y <= 5; y++) {
    setMatrixPixel(startX + 1, y);
  }

  setMatrixPixel(startX + 2, 0);
  setMatrixPixel(startX + 2, 1);
  setMatrixPixel(startX + 2, 5);
  setMatrixPixel(startX + 2, 6);

  if (present) {
    // Sound waves occupy the right three columns.
    setMatrixPixel(startX + 3, 2);
    setMatrixPixel(startX + 3, 4);
    setMatrixPixel(startX + 4, 1);
    setMatrixPixel(startX + 4, 5);
    setMatrixPixel(startX + 5, 0);
    setMatrixPixel(startX + 5, 6);
  } else {
    // Separate 3x5 X; it does not cover the speaker shape.
    setMatrixPixel(startX + 3, 1);
    setMatrixPixel(startX + 5, 1);
    setMatrixPixel(startX + 4, 2);
    setMatrixPixel(startX + 4, 3);
    setMatrixPixel(startX + 4, 4);
    setMatrixPixel(startX + 3, 5);
    setMatrixPixel(startX + 5, 5);
  }
}

void drawMidiIcon(int startX, bool present) {
  // Six-pixel-wide DIN-style MIDI connector outline.
  setMatrixPixel(startX + 2, 0);
  setMatrixPixel(startX + 3, 0);
  setMatrixPixel(startX + 1, 1);
  setMatrixPixel(startX + 4, 1);

  for (int y = 2; y <= 4; y++) {
    setMatrixPixel(startX, y);
    setMatrixPixel(startX + 5, y);
  }

  setMatrixPixel(startX + 1, 5);
  setMatrixPixel(startX + 4, 5);
  setMatrixPixel(startX + 2, 6);
  setMatrixPixel(startX + 3, 6);

  // Five MIDI/DIN pins.
  setMatrixPixel(startX + 2, 2);
  setMatrixPixel(startX + 3, 2);
  setMatrixPixel(startX + 1, 3);
  setMatrixPixel(startX + 4, 3);
  setMatrixPixel(startX + 2, 4);

  if (!present) {
    drawMissingStrike(startX);
  }
}

int getStartupWarnings() {
  int warningFlags = 0;

  // Python may still be starting, so retry for a few seconds.
  for (int attempt = 0; attempt < 12; attempt++) {
    bool ok = Bridge
      .call("get_startup_warnings")
      .result(warningFlags);

    if (ok) {
      return warningFlags;
    }

    delay(250);
  }

  // A Bridge startup delay is not itself a USB-device warning.
  return 0;
}

void showStartupDeviceStatus(int warningFlags) {
  matrixDisplayMode = DISPLAY_STARTUP_STATUS;
  activeScaleText = nullptr;
  lastDisplayedSettingButton = BUTTON_NONE;

  bool speakerPresent = (
    warningFlags & STARTUP_WARNING_NO_SPEAKER
  ) == 0;

  bool midiPresent = (
    warningFlags & STARTUP_WARNING_NO_MIDI_OUTPUT
  ) == 0;

  Monitor.print("USB speaker: ");
  Monitor.println(speakerPresent ? "present" : "not present");
  Monitor.print("MIDI output: ");
  Monitor.println(midiPresent ? "present" : "not present");

  clearMatrixBuffer();
  drawSpeakerIcon(0, speakerPresent);
  drawMidiIcon(7, midiPresent);
  renderMatrix();
}

// --------------------------------------------------
// Waveform picture display
// --------------------------------------------------

void drawConnectedWave(const uint8_t yValues[MATRIX_WIDTH]) {
  for (int x = 0; x < MATRIX_WIDTH; x++) {
    setMatrixPixel(x, yValues[x]);

    if (x > 0) {
      int lowY = min((int)yValues[x - 1], (int)yValues[x]);
      int highY = max((int)yValues[x - 1], (int)yValues[x]);

      for (int y = lowY; y <= highY; y++) {
        setMatrixPixel(x, y);
      }
    }
  }
}

void showWaveformDisplay() {
  matrixDisplayMode = DISPLAY_WAVEFORM;
  activeScaleText = nullptr;
  clearMatrixBuffer();

  static const uint8_t SINE_WAVE[MATRIX_WIDTH] = {
    4, 2, 1, 1, 2, 4, 6, 7, 7, 6, 4, 2, 1
  };

  static const uint8_t SQUARE_WAVE[MATRIX_WIDTH] = {
    1, 1, 1, 1, 1, 1, 6, 6, 6, 6, 6, 6, 1
  };

  static const uint8_t SAW_WAVE[MATRIX_WIDTH] = {
    6, 5, 4, 3, 2, 1, 6, 5, 4, 3, 2, 1, 6
  };

  static const uint8_t TRIANGLE_WAVE[MATRIX_WIDTH] = {
    6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6
  };

  switch (waveformIndex) {
    case 0:
      drawConnectedWave(SINE_WAVE);
      break;
    case 1:
      drawConnectedWave(SQUARE_WAVE);
      break;
    case 2:
      drawConnectedWave(SAW_WAVE);
      break;
    case 3:
      drawConnectedWave(TRIANGLE_WAVE);
      break;
  }

  renderMatrix();
}

// --------------------------------------------------
// Pixels Modulino: knob offset 0-7
// --------------------------------------------------

void showScaleOffset(int knobValue) {
  knobValue = constrain(knobValue, 0, OFFSET_LED_COUNT - 1);

  // Switch all LEDs off, then light only the selected scale offset.
  offsetPixels.clear();
  offsetPixels.set(
    knobValue,
    OFFSET_LED_COLOR,
    OFFSET_LED_BRIGHTNESS
  );
  offsetPixels.show();

  lastDisplayedKnob = knobValue;
}


// --------------------------------------------------
// Persistent MIDI-channel storage
//
// The UNO Q Zephyr sketch build does not link the
// fs_* functions used by the previous version.
// Python stores the setting on the Linux/eMMC side.
// --------------------------------------------------

int loadMidiChannel() {
  int loadedChannel = DEFAULT_MIDI_CHANNEL;

  // Both processors start together, so retry briefly
  // while Python registers its Bridge functions.
  for (int attempt = 0; attempt < 12; attempt++) {
    bool ok = Bridge
      .call("load_midi_channel")
      .result(loadedChannel);

    if (ok) {
      loadedChannel = constrain(
        loadedChannel,
        MIDI_CHANNEL_MIN,
        MIDI_CHANNEL_MAX
      );

      Monitor.print("Loaded MIDI channel: ");
      Monitor.println(loadedChannel);

      return loadedChannel;
    }

    delay(250);
  }

  Monitor.println(
    "Could not load MIDI channel; using channel 13"
  );

  return DEFAULT_MIDI_CHANNEL;
}


bool saveMidiChannel(int channel) {
  int requestedChannel = constrain(
    channel,
    MIDI_CHANNEL_MIN,
    MIDI_CHANNEL_MAX
  );

  int savedChannel = DEFAULT_MIDI_CHANNEL;

  bool ok = Bridge
    .call(
      "save_midi_channel",
      requestedChannel
    )
    .result(savedChannel);

  if (!ok) {
    Monitor.println(
      "Could not save MIDI channel on Linux side"
    );

    return false;
  }

  savedChannel = constrain(
    savedChannel,
    MIDI_CHANNEL_MIN,
    MIDI_CHANNEL_MAX
  );

  if (savedChannel != requestedChannel) {
    Monitor.println(
      "Saved MIDI channel did not match request"
    );

    return false;
  }

  Monitor.print("MIDI channel saved: ");
  Monitor.println(savedChannel);

  return true;
}


// --------------------------------------------------
// Persistent waveform, scale, and root storage
//
// The three values are packed into one integer for the
// Bridge return value, but stored as separate values by Python.
// --------------------------------------------------

int packMusicalSettings(
  int waveform,
  int scale,
  int root
) {
  waveform = constrain(waveform, 0, WAVEFORM_COUNT - 1);
  scale = constrain(scale, 0, SCALE_COUNT - 1);
  root = constrain(root, 0, ROOT_COUNT - 1);

  return waveform + WAVEFORM_COUNT * (
    scale + SCALE_COUNT * root
  );
}

void unpackMusicalSettings(int packed) {
  packed = max(0, packed);

  waveformIndex = constrain(
    packed % WAVEFORM_COUNT,
    0,
    WAVEFORM_COUNT - 1
  );

  int remaining = packed / WAVEFORM_COUNT;

  scaleIndex = constrain(
    remaining % SCALE_COUNT,
    0,
    SCALE_COUNT - 1
  );

  rootIndex = constrain(
    remaining / SCALE_COUNT,
    0,
    ROOT_COUNT - 1
  );
}

bool loadMusicalSettings() {
  int packedSettings = packMusicalSettings(
    DEFAULT_WAVEFORM_INDEX,
    DEFAULT_SCALE_INDEX,
    DEFAULT_ROOT_INDEX
  );

  for (int attempt = 0; attempt < 12; attempt++) {
    bool ok = Bridge
      .call("load_musical_settings")
      .result(packedSettings);

    if (ok) {
      unpackMusicalSettings(packedSettings);

      Monitor.print("Loaded waveform: ");
      Monitor.println(waveformNames[waveformIndex]);
      Monitor.print("Loaded scale: ");
      Monitor.println(scaleNames[scaleIndex]);
      Monitor.print("Loaded root: ");
      Monitor.println(rootNames[rootIndex]);

      return true;
    }

    delay(250);
  }

  waveformIndex = DEFAULT_WAVEFORM_INDEX;
  scaleIndex = DEFAULT_SCALE_INDEX;
  rootIndex = DEFAULT_ROOT_INDEX;

  Monitor.println(
    "Could not load musical settings; using defaults"
  );

  return false;
}

bool saveMusicalSettings() {
  int expectedPacked = packMusicalSettings(
    waveformIndex,
    scaleIndex,
    rootIndex
  );
  int savedPacked = -1;

  bool ok = Bridge
    .call(
      "save_musical_settings",
      waveformIndex,
      scaleIndex,
      rootIndex
    )
    .result(savedPacked);

  if (!ok || savedPacked != expectedPacked) {
    Monitor.println(
      "Could not save waveform, scale, and root"
    );
    return false;
  }

  Monitor.println(
    "Waveform, scale, and root saved"
  );
  return true;
}


// --------------------------------------------------
// MIDI-channel display and button gestures
// --------------------------------------------------

void showMidiChannelDisplay(int channel) {
  channel = constrain(
    channel,
    MIDI_CHANNEL_MIN,
    MIDI_CHANNEL_MAX
  );

  matrixDisplayMode = DISPLAY_MIDI_CHANNEL;
  activeScaleText = nullptr;
  clearMatrixBuffer();

  if (channel < 10) {
    drawLargeDigit(channel, 4, 0);
  } else {
    drawLargeDigit(channel / 10, 1, 0);
    drawLargeDigit(channel % 10, 7, 0);
  }

  renderMatrix();
}

char firstPressedButton() {
  if (buttons.isPressed(BUTTON_WAVEFORM)) {
    return BUTTON_WAVEFORM;
  }

  if (buttons.isPressed(BUTTON_SCALE)) {
    return BUTTON_SCALE;
  }

  if (buttons.isPressed(BUTTON_ROOT)) {
    return BUTTON_ROOT;
  }

  return BUTTON_NONE;
}

void showSettingForButton(char button) {
  switch (button) {
    case BUTTON_WAVEFORM:
      showWaveformDisplay();
      Monitor.print("Waveform: ");
      Monitor.println(waveformNames[waveformIndex]);
      break;

    case BUTTON_SCALE:
      showScaleDisplay();
      Monitor.print("Scale: ");
      Monitor.println(scaleNames[scaleIndex]);
      break;

    case BUTTON_ROOT:
      showRootDisplay();
      Monitor.print("Root: ");
      Monitor.println(rootNames[rootIndex]);
      break;
  }
}

void advanceSettingForButton(char button) {
  switch (button) {
    case BUTTON_WAVEFORM:
      waveformIndex = (waveformIndex + 1) % WAVEFORM_COUNT;
      break;

    case BUTTON_SCALE:
      scaleIndex = (scaleIndex + 1) % SCALE_COUNT;
      break;

    case BUTTON_ROOT:
      rootIndex = (rootIndex + 1) % ROOT_COUNT;
      break;
  }

  saveMusicalSettings();
  showSettingForButton(button);
  lastDisplayedSettingButton = button;
}

void beginButtonGesture(char button) {
  buttonGestureActive = true;
  gestureButton = button;
  buttonGestureStartedAt = millis();
  gestureStartKnob = knob.get();
  gestureStartMidiChannel = midiChannel;
  gestureNormalKnobValue = normalKnobValue;
  midiChannelGestureActive = false;

  // When a different setting button is selected, the first press
  // only reveals its current value. Repeated presses advance it.
  buttonAdvancePending = (
    lastDisplayedSettingButton == button
  );

  if (!buttonAdvancePending) {
    showSettingForButton(button);
    lastDisplayedSettingButton = button;
  }
}

void beginMidiChannelGesture() {
  midiChannelGestureActive = true;
  buttonAdvancePending = false;

  // The matrix no longer shows a musical setting, so the next
  // ordinary button press must reveal that setting before changing it.
  lastDisplayedSettingButton = BUTTON_NONE;

  offsetPixels.clear();
  offsetPixels.show();
  showMidiChannelDisplay(midiChannel);

  Monitor.println(
    "MIDI channel edit: keep button held and turn knob"
  );
}

void updateMidiChannelFromGesture(int rawKnob) {
  int knobDelta = rawKnob - gestureStartKnob;
  int selectedChannel = constrain(
    gestureStartMidiChannel + knobDelta,
    MIDI_CHANNEL_MIN,
    MIDI_CHANNEL_MAX
  );

  if (selectedChannel == midiChannel) {
    return;
  }

  midiChannel = selectedChannel;
  showMidiChannelDisplay(midiChannel);

  // Change the active channel immediately. Persistence is written
  // once, when the button is released.
  Bridge.notify(
    "set_midi_channel",
    midiChannel
  );

  Monitor.print("MIDI channel: ");
  Monitor.println(midiChannel);
}

void finishButtonGesture() {
  if (midiChannelGestureActive) {
    if (midiChannel != gestureStartMidiChannel) {
      bool saved = saveMidiChannel(midiChannel);

      Monitor.print("MIDI channel ");
      Monitor.print(midiChannel);
      Monitor.println(
        saved ? " saved" : " active but not saved"
      );
    } else {
      Monitor.print("MIDI channel unchanged: ");
      Monitor.println(midiChannel);
    }
  } else if (buttonAdvancePending) {
    // A quick repeated click may be released before the grace period.
    // Advance on release so it still behaves like an immediate click.
    advanceSettingForButton(gestureButton);
  }

  // MIDI editing is relative. Restore the normal 0-7 scale-offset
  // knob position after the button is released.
  knob.set(gestureNormalKnobValue);
  normalKnobValue = gestureNormalKnobValue;
  showScaleOffset(normalKnobValue);

  buttonGestureActive = false;
  buttonAdvancePending = false;
  midiChannelGestureActive = false;
  gestureButton = BUTTON_NONE;
}

void updateButtonGesture() {
  if (!buttonGestureActive) {
    return;
  }

  int rawKnob = knob.get();
  bool knobMoved = rawKnob != gestureStartKnob;

  if (knobMoved) {
    if (!midiChannelGestureActive) {
      beginMidiChannelGesture();
    }

    updateMidiChannelFromGesture(rawKnob);
  } else if (
    buttonAdvancePending &&
    millis() - buttonGestureStartedAt >=
      BUTTON_MIDI_GESTURE_GRACE_MS
  ) {
    // The same setting is already visible. Advance once without
    // waiting for release, unless knob movement selected MIDI mode.
    advanceSettingForButton(gestureButton);
    buttonAdvancePending = false;
  }

  if (!buttons.isPressed(gestureButton)) {
    finishButtonGesture();
  }
}

void printMusicalSettings() {
  Monitor.println();
  Monitor.println("==============================");
  Monitor.print("Root: ");
  Monitor.println(rootNames[rootIndex]);
  Monitor.print("Scale: ");
  Monitor.println(scaleNames[scaleIndex]);
  Monitor.print("Waveform: ");
  Monitor.println(waveformNames[waveformIndex]);
  Monitor.print("MIDI channel: ");
  Monitor.println(midiChannel);
  Monitor.println("==============================");
  Monitor.println();
}

void setup() {
  matrix.begin();
  matrix.setGrayscaleBits(1);

  clearMatrixBuffer();
  setMatrixPixel(6, 3);
  renderMatrix();

  Bridge.begin();
  Monitor.begin();
  Modulino.begin();

  bool distanceOK = distanceSensor.begin();
  bool knobOK = knob.begin();
  bool buttonsOK = buttons.begin();
  offsetPixels.begin();

  buttons.setLeds(false, false, false);

  // Load the last saved channel. MIDI-channel editing is available
  // during normal operation; there is no separate startup mode.
  midiChannel = loadMidiChannel();
  loadMusicalSettings();

  Monitor.println("Theremin starting");
  Monitor.print("Distance: ");
  Monitor.println(distanceOK ? "OK" : "FAILED");
  Monitor.print("Knob: ");
  Monitor.println(knobOK ? "OK" : "FAILED");
  Monitor.print("Buttons: ");
  Monitor.println(buttonsOK ? "OK" : "FAILED");

  Monitor.print("Offset LED brightness: ");
  Monitor.print(OFFSET_LED_BRIGHTNESS);
  Monitor.println("%");
  Monitor.println("Offset LED color: orange (255, 80, 0)");

  Monitor.println("MIDI channel storage: Linux file via Bridge");
  Monitor.println("Musical settings storage: Linux file via Bridge");

  printMusicalSettings();
  Monitor.println("A: show/change waveform");
  Monitor.println("B: show/change scale");
  Monitor.println("C: show/change root note");
  Monitor.println(
    "Hold any button and turn knob: MIDI channel"
  );

  normalKnobValue = 0;
  knob.set(normalKnobValue);
  delay(500);

  Bridge.notify(
    "set_midi_channel",
    midiChannel
  );

  // Missing optional USB devices do not stop operation.
  // Show both device-status icons until a button changes the display.
  int startupWarnings = getStartupWarnings();
  showStartupDeviceStatus(startupWarnings);
  showScaleOffset(0);
}

void loop() {
  // --------------------------------------------------
  // Buttons and button + knob MIDI gesture
  // --------------------------------------------------

  buttons.update();

  bool buttonA = buttons.isPressed(BUTTON_WAVEFORM);
  bool buttonB = buttons.isPressed(BUTTON_SCALE);
  bool buttonC = buttons.isPressed(BUTTON_ROOT);

  buttons.setLeds(buttonA, buttonB, buttonC);

  if (!buttonGestureActive) {
    char pressedButton = firstPressedButton();

    if (pressedButton != BUTTON_NONE) {
      beginButtonGesture(pressedButton);
    }
  }

  updateButtonGesture();

  // Keep scale-name movement independent from sensor updates.
  updateScaleBounce();

  // --------------------------------------------------
  // Read distance and normal knob about 30 times per second
  // --------------------------------------------------

  if (millis() - previousUpdate < 30) {
    return;
  }

  previousUpdate = millis();

  int knobValue = normalKnobValue;

  if (!buttonGestureActive) {
    int rawKnob = knob.get();
    knobValue = constrain(rawKnob, 0, 7);

    if (rawKnob != knobValue) {
      knob.set(knobValue);
    }

    normalKnobValue = knobValue;

    if (knobValue != lastDisplayedKnob) {
      showScaleOffset(knobValue);
    }
  }

  // --------------------------------------------------
  // Distance and bridge update
  // --------------------------------------------------

  if (distanceSensor.available()) {
    float measuredDistance = distanceSensor.get();

    if (!isnan(measuredDistance)) {
      int distance = (int)measuredDistance;

      if (distance >= 20 && distance <= 500) {
        lastValidDistance = millis();
        toneIsPlaying = true;

        Bridge.notify(
          "update_tone",
          distance,
          knobValue,
          waveformIndex,
          scaleIndex,
          rootIndex,
          midiChannel
        );

        return;
      }
    }
  }

  if (
    toneIsPlaying &&
    millis() - lastValidDistance > 250
  ) {
    toneIsPlaying = false;
    Bridge.notify("stop_tone");
    Monitor.println("No distance: tone stopped");
  }
}
