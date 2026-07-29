import atexit
import glob
import os
from pathlib import Path
import threading
import time

from arduino.app_bricks.wave_generator import WaveGenerator
from arduino.app_peripherals.speaker import ALSASpeaker
from arduino.app_utils import App, Bridge


# --------------------------------------------------
# Distance configuration
# --------------------------------------------------

MIN_DISTANCE = 20
MAX_DISTANCE = 500

# Every 20 mm advances one note in the selected scale.
NOTE_STEP_MM = 20

# Prevent rapid switching around note boundaries.
HYSTERESIS_MM = 5


# --------------------------------------------------
# Audio configuration
# --------------------------------------------------

FIXED_AMPLITUDE = 0.6

# C3 is the reference pitch. This is one octave above
# the original C2 reference.
BASE_C_MIDI = 48

# Absolute pitch protection for the internal synth.
MIN_ALLOWED_MIDI = 9     # A-1
MAX_ALLOWED_MIDI = 144   # C11


# --------------------------------------------------
# USB MIDI configuration
# --------------------------------------------------

MIDI_ENABLED = True

# MIDI channels are numbered 1-16 for musicians.
# The Arduino sends the saved runtime channel.
DEFAULT_MIDI_CHANNEL = 13
MIDI_VELOCITY = 100

# Leave this as None to automatically select the first writable
# ALSA raw-MIDI output.
#
# To select a particular device manually:
# MIDI_OUTPUT_DEVICE_PATH = "/dev/snd/midiC2D0"
MIDI_OUTPUT_DEVICE_PATH = None

MIDI_MIN_NOTE = 0
MIDI_MAX_NOTE = 127
MIDI_RECONNECT_INTERVAL = 2.0

# Startup warning flags returned to the Arduino sketch.
STARTUP_WARNING_NO_SPEAKER = 0x01
STARTUP_WARNING_NO_MIDI_OUTPUT = 0x02

# Persisted on the UNO Q Linux/eMMC side.
MIDI_SETTINGS_FILE = (
    Path.home()
    / ".config"
    / "thering"
    / "midi_channel.txt"
)

MUSICAL_SETTINGS_FILE = (
    Path.home()
    / ".config"
    / "thering"
    / "musical_settings.txt"
)


# --------------------------------------------------
# Musical definitions
# --------------------------------------------------

WAVEFORMS = [
    "sine",
    "square",
    "sawtooth",
    "triangle",
]


ROOTS = [
    {"name": "C",  "offset": 0,  "prefer_flats": False},
    {"name": "C#", "offset": 1,  "prefer_flats": False},
    {"name": "D",  "offset": 2,  "prefer_flats": False},
    {"name": "Eb", "offset": 3,  "prefer_flats": True},
    {"name": "E",  "offset": 4,  "prefer_flats": False},
    {"name": "F",  "offset": 5,  "prefer_flats": True},
    {"name": "F#", "offset": 6,  "prefer_flats": False},
    {"name": "G",  "offset": 7,  "prefer_flats": False},
    {"name": "Ab", "offset": 8,  "prefer_flats": True},
    {"name": "A",  "offset": 9,  "prefer_flats": False},
    {"name": "Bb", "offset": 10, "prefer_flats": True},
    {"name": "B",  "offset": 11, "prefer_flats": False},
]


# The order must match scaleIndex in the Arduino sketch.
# Every interval is an integer semitone, so all scales are directly
# representable with ordinary MIDI note numbers. Major is Ionian and
# minor is Aeolian, so those aliases are not duplicated.
SCALES = [
    {
        "name": "major",
        "intervals": [0, 2, 4, 5, 7, 9, 11],
    },
    {
        "name": "minor",
        "intervals": [0, 2, 3, 5, 7, 8, 10],
    },
    {
        "name": "Dorian",
        "intervals": [0, 2, 3, 5, 7, 9, 10],
    },
    {
        "name": "Phrygian",
        "intervals": [0, 1, 3, 5, 7, 8, 10],
    },
    {
        "name": "Lydian",
        "intervals": [0, 2, 4, 6, 7, 9, 11],
    },
    {
        "name": "Mixolydian",
        "intervals": [0, 2, 4, 5, 7, 9, 10],
    },
    {
        "name": "Locrian",
        "intervals": [0, 1, 3, 5, 6, 8, 10],
    },
    {
        "name": "major pentatonic",
        "intervals": [0, 2, 4, 7, 9],
    },
    {
        "name": "minor pentatonic",
        "intervals": [0, 3, 5, 7, 10],
    },
    {
        "name": "blues",
        "intervals": [0, 3, 5, 6, 7, 10],
    },
    {
        "name": "chromatic",
        "intervals": list(range(12)),
    },
    {
        "name": "Hirajoshi",
        "intervals": [0, 2, 3, 7, 8],
    },
    {
        "name": "In Sen",
        "intervals": [0, 1, 5, 7, 10],
    },
    {
        "name": "Iwato",
        "intervals": [0, 1, 5, 6, 10],
    },
    {
        "name": "Yo",
        "intervals": [0, 2, 5, 7, 9],
    },
]

SHARP_NOTE_NAMES = [
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B",
]

FLAT_NOTE_NAMES = [
    "C", "Db", "D", "Eb", "E", "F",
    "Gb", "G", "Ab", "A", "Bb", "B",
]


# --------------------------------------------------
# Optional internal wave generator
#
# A missing or busy USB speaker must never stop the app.
# MIDI output continues working without audio.
# --------------------------------------------------

tone = None
audio_output_available = False
audio_output_error = None


def disable_audio_output(error):
    global tone
    global audio_output_available
    global audio_output_error

    audio_output_available = False
    audio_output_error = str(error)

    if tone is not None:
        try:
            tone.amplitude = 0.0
        except Exception:
            pass

    tone = None

    print(
        f"USB speaker unavailable | {audio_output_error} | "
        "Continuing with MIDI only"
    )


def usb_speaker_present():
    """Return True only when ALSA reports a USB playback device."""
    try:
        devices = ALSASpeaker.list_usb_devices()
    except Exception as error:
        print(f"USB speaker precheck failed | {error}")
        return False

    if devices:
        print(
            "USB speaker precheck | "
            f"Found={devices}"
        )
        return True

    print("USB speaker precheck | No USB playback device")
    return False


def start_audio_output():
    global tone
    global audio_output_available
    global audio_output_error

    # Do not construct or start the WaveGenerator when no USB
    # playback device exists. This keeps MIDI completely
    # independent from the optional audio output.
    if not usb_speaker_present():
        disable_audio_output(
            "No USB playback device detected"
        )
        return False

    try:
        candidate = WaveGenerator()
        App.start_brick(candidate)

        candidate.wave_type = WAVEFORMS[current_waveform_index]
        candidate.volume = 80
        candidate.attack = 0.02
        candidate.release = 0.08
        candidate.glide = 0.03
        candidate.amplitude = 0.0

        tone = candidate
        audio_output_available = True
        audio_output_error = None

        print("USB speaker connected | Internal audio enabled")
        return True

    except Exception as error:
        disable_audio_output(error)
        return False


def update_audio_output(frequency, amplitude):
    if tone is None:
        return False

    try:
        tone.frequency = frequency
        tone.amplitude = amplitude
        return True

    except Exception as error:
        disable_audio_output(error)
        return False


def set_audio_waveform(waveform):
    if tone is None:
        return False

    try:
        tone.wave_type = waveform
        return True

    except Exception as error:
        disable_audio_output(error)
        return False


def stop_audio_output():
    if tone is None:
        return

    try:
        tone.amplitude = 0.0
    except Exception as error:
        disable_audio_output(error)


# --------------------------------------------------
# Current musical state
# --------------------------------------------------

current_distance_step = None
current_waveform_index = 0
current_scale_index = 0
current_root_index = 0
current_knob_step = 0

last_print_time = 0.0


# --------------------------------------------------
# Current USB MIDI state
# --------------------------------------------------

midi_output_file = None
midi_output_active_path = None

current_midi_note = None
current_midi_channel = DEFAULT_MIDI_CHANNEL

last_midi_output_scan_time = 0.0
midi_output_missing_reported = False

# Protect MIDI output state and writes.
midi_output_lock = threading.RLock()


# --------------------------------------------------
# General helpers
# --------------------------------------------------

def limit(value, minimum, maximum):
    return max(minimum, min(maximum, value))


def midi_to_frequency(midi_note):
    return 440.0 * (2 ** ((midi_note - 69) / 12.0))


def midi_to_note_name(midi_note, prefer_flats=False):
    note_names = (
        FLAT_NOTE_NAMES if prefer_flats else SHARP_NOTE_NAMES
    )

    note_index = midi_note % 12
    octave = midi_note // 12 - 1

    return f"{note_names[note_index]}{octave}"


# --------------------------------------------------
# Scale conversion
# --------------------------------------------------

def scale_step_to_midi(scale_step, scale, root):
    intervals = scale["intervals"]

    octave_offset, note_index = divmod(
        scale_step,
        len(intervals),
    )

    return (
        BASE_C_MIDI
        + root["offset"]
        + octave_offset * 12
        + intervals[note_index]
    )


def scale_step_to_name(scale_step, scale, root):
    midi_note = scale_step_to_midi(
        scale_step,
        scale,
        root,
    )

    return midi_to_note_name(
        midi_note,
        root["prefer_flats"],
    )


# --------------------------------------------------
# ALSA raw-MIDI output
# --------------------------------------------------

def midi_device_candidates(explicit_path=None):
    """Return possible ALSA raw-MIDI device paths."""
    candidates = []

    if explicit_path:
        candidates.append(explicit_path)

    # Prefer stable USB aliases when they exist.
    for pattern in (
        "/dev/snd/by-id/*",
        "/dev/snd/by-path/*",
    ):
        for path in sorted(glob.glob(pattern)):
            target_name = os.path.basename(
                os.path.realpath(path)
            )

            if target_name.startswith("midiC"):
                candidates.append(path)

    # Fall back to normal ALSA raw-MIDI device names.
    candidates.extend(
        sorted(glob.glob("/dev/snd/midiC*D*"))
    )

    unique_candidates = []
    seen_targets = set()

    for path in candidates:
        target = os.path.realpath(path)

        if target in seen_targets:
            continue

        seen_targets.add(target)
        unique_candidates.append(path)

    return unique_candidates


def close_midi_output(clear_generated_note=False):
    global midi_output_file
    global midi_output_active_path
    global current_midi_note

    with midi_output_lock:
        if midi_output_file is not None:
            try:
                os.close(midi_output_file)
            except OSError:
                pass

        midi_output_file = None
        midi_output_active_path = None

        if clear_generated_note:
            current_midi_note = None


def open_midi_output(force=False):
    """Open the first writable ALSA raw-MIDI output."""
    global midi_output_file
    global midi_output_active_path
    global last_midi_output_scan_time
    global midi_output_missing_reported

    if not MIDI_ENABLED:
        return False

    with midi_output_lock:
        if midi_output_file is not None:
            return True

        now = time.monotonic()

        if (
            not force
            and now - last_midi_output_scan_time
            < MIDI_RECONNECT_INTERVAL
        ):
            return False

        last_midi_output_scan_time = now

        for path in midi_device_candidates(
            MIDI_OUTPUT_DEVICE_PATH
        ):
            try:
                file_descriptor = os.open(
                    path,
                    os.O_WRONLY | os.O_NONBLOCK,
                )
            except OSError:
                continue

            midi_output_file = file_descriptor
            midi_output_active_path = path
            midi_output_missing_reported = False

            print(
                f"USB MIDI output connected | "
                f"Device={path}"
            )

            return True

        if not midi_output_missing_reported:
            print(
                "USB MIDI output not found | "
                "Looking for /dev/snd/midiC*D*"
            )
            midi_output_missing_reported = True

        return False


def _write_midi_data_locked(data):
    """Write bytes while midi_output_lock is already held."""
    if not data:
        return True

    if not open_midi_output():
        return False

    view = memoryview(data)
    offset = 0
    deadline = time.monotonic() + 0.1

    while offset < len(view):
        try:
            written = os.write(
                midi_output_file,
                view[offset:],
            )

            if written <= 0:
                raise OSError("Zero-length MIDI write")

            offset += written

        except BlockingIOError:
            if time.monotonic() >= deadline:
                return False

            time.sleep(0.001)

        except OSError as error:
            print(
                f"USB MIDI output disconnected | {error}"
            )
            close_midi_output(clear_generated_note=True)
            return False

    return True


def write_midi_data(data):
    """Write one complete local MIDI message atomically."""
    with midi_output_lock:
        return _write_midi_data_locked(data)


def send_midi_bytes(status, data1, data2):
    """Write one standard three-byte MIDI message."""
    return write_midi_data(
        bytes((
            status & 0xFF,
            data1 & 0x7F,
            data2 & 0x7F,
        ))
    )


def normalize_midi_channel(channel):
    return limit(int(channel), 1, 16)



# --------------------------------------------------
# Persistent waveform, scale, and root settings
# --------------------------------------------------

def normalize_musical_settings(
    waveform_index,
    scale_index,
    root_index,
):
    return (
        limit(int(waveform_index), 0, len(WAVEFORMS) - 1),
        limit(int(scale_index), 0, len(SCALES) - 1),
        limit(int(root_index), 0, len(ROOTS) - 1),
    )


def pack_musical_settings(
    waveform_index,
    scale_index,
    root_index,
):
    waveform_index, scale_index, root_index = (
        normalize_musical_settings(
            waveform_index,
            scale_index,
            root_index,
        )
    )

    return waveform_index + len(WAVEFORMS) * (
        scale_index + len(SCALES) * root_index
    )


def read_musical_settings():
    defaults = (0, 0, 0)

    try:
        values = [
            int(value)
            for value in MUSICAL_SETTINGS_FILE
            .read_text(encoding="utf-8")
            .split()
        ]

        if len(values) != 3:
            raise ValueError(
                "Expected waveform, scale, and root indexes"
            )

        settings = normalize_musical_settings(*values)
    except (OSError, ValueError):
        settings = defaults

    return settings


def apply_musical_settings(
    waveform_index,
    scale_index,
    root_index,
):
    global current_waveform_index
    global current_scale_index
    global current_root_index

    (
        current_waveform_index,
        current_scale_index,
        current_root_index,
    ) = normalize_musical_settings(
        waveform_index,
        scale_index,
        root_index,
    )

    set_audio_waveform(
        WAVEFORMS[current_waveform_index]
    )

    return (
        current_waveform_index,
        current_scale_index,
        current_root_index,
    )


def load_musical_settings():
    """Load and apply waveform, scale, and root indexes."""
    settings = apply_musical_settings(
        *read_musical_settings()
    )

    packed = pack_musical_settings(*settings)

    print(
        "Musical settings loaded | "
        f"Waveform={WAVEFORMS[settings[0]]} | "
        f"Scale={SCALES[settings[1]]['name']} | "
        f"Root={ROOTS[settings[2]]['name']} | "
        f"File={MUSICAL_SETTINGS_FILE}"
    )

    return packed


def save_musical_settings(
    waveform_index,
    scale_index,
    root_index,
):
    """Atomically save and apply waveform, scale, and root."""
    settings = normalize_musical_settings(
        waveform_index,
        scale_index,
        root_index,
    )

    MUSICAL_SETTINGS_FILE.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    temporary_file = MUSICAL_SETTINGS_FILE.with_suffix(
        ".tmp"
    )

    temporary_file.write_text(
        f"{settings[0]} {settings[1]} {settings[2]}\n",
        encoding="utf-8",
    )

    os.replace(
        temporary_file,
        MUSICAL_SETTINGS_FILE,
    )

    apply_musical_settings(*settings)

    print(
        "Musical settings saved | "
        f"Waveform={WAVEFORMS[settings[0]]} | "
        f"Scale={SCALES[settings[1]]['name']} | "
        f"Root={ROOTS[settings[2]]['name']} | "
        f"File={MUSICAL_SETTINGS_FILE}"
    )

    return pack_musical_settings(*settings)


# --------------------------------------------------
# Persistent MIDI-channel settings
# --------------------------------------------------

def load_midi_channel():
    """Load the saved MIDI channel, returning 1-16."""
    try:
        value = int(
            MIDI_SETTINGS_FILE
            .read_text(encoding="utf-8")
            .strip()
        )
    except (OSError, ValueError):
        value = DEFAULT_MIDI_CHANNEL

    channel = normalize_midi_channel(value)

    print(
        f"MIDI settings loaded | "
        f"Channel={channel} | "
        f"File={MIDI_SETTINGS_FILE}"
    )

    return channel


def save_midi_channel(channel):
    """Save the channel atomically and activate it."""
    channel = normalize_midi_channel(channel)

    MIDI_SETTINGS_FILE.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    temporary_file = MIDI_SETTINGS_FILE.with_suffix(
        ".tmp"
    )

    temporary_file.write_text(
        f"{channel}\n",
        encoding="utf-8",
    )

    os.replace(
        temporary_file,
        MIDI_SETTINGS_FILE,
    )

    set_midi_channel(channel)

    print(
        f"MIDI settings saved | "
        f"Channel={channel} | "
        f"File={MIDI_SETTINGS_FILE}"
    )

    return channel


def send_midi_control_change(
    controller,
    value,
    channel=None,
):
    if channel is None:
        channel = current_midi_channel

    channel_index = normalize_midi_channel(channel) - 1
    status = 0xB0 | channel_index

    return send_midi_bytes(
        status,
        controller,
        value,
    )


def send_midi_note_on(
    note,
    velocity=MIDI_VELOCITY,
    channel=None,
):
    if channel is None:
        channel = current_midi_channel

    channel_index = normalize_midi_channel(channel) - 1
    status = 0x90 | channel_index

    return send_midi_bytes(
        status,
        note,
        velocity,
    )


def send_midi_note_off(note, channel=None):
    if channel is None:
        channel = current_midi_channel

    channel_index = normalize_midi_channel(channel) - 1
    status = 0x80 | channel_index

    return send_midi_bytes(
        status,
        note,
        0,
    )


def set_midi_channel(channel):
    """Switch only the theremin-generated MIDI channel."""
    global current_midi_channel
    global current_midi_note

    new_channel = normalize_midi_channel(channel)

    if new_channel == current_midi_channel:
        return current_midi_channel

    previous_channel = current_midi_channel
    previous_note = current_midi_note
    current_midi_note = None

    # Only locally generated theremin notes use this channel.
    if previous_note is not None:
        send_midi_note_off(
            previous_note,
            channel=previous_channel,
        )

    send_midi_control_change(
        123,
        0,
        channel=previous_channel,
    )

    current_midi_channel = new_channel

    send_midi_control_change(
        123,
        0,
        channel=current_midi_channel,
    )

    print(
        f"Theremin MIDI channel changed | "
        f"Channel={current_midi_channel} | "
        "Thru=all channels"
    )

    return current_midi_channel


def update_midi_note(midi_note):
    """Change the generated MIDI note only when pitch changes."""
    global current_midi_note

    if not MIDI_ENABLED:
        return

    midi_note = int(midi_note)

    if not MIDI_MIN_NOTE <= midi_note <= MIDI_MAX_NOTE:
        stop_midi_note()
        return

    if midi_note == current_midi_note:
        return

    previous_note = current_midi_note
    current_midi_note = None

    if previous_note is not None:
        send_midi_note_off(previous_note)

    if send_midi_note_on(midi_note):
        current_midi_note = midi_note


def stop_midi_note():
    global current_midi_note

    previous_note = current_midi_note
    current_midi_note = None

    if previous_note is not None:
        send_midi_note_off(previous_note)


def shutdown_midi():
    stop_midi_note()

    if midi_output_file is not None:
        send_midi_control_change(123, 0)

    close_midi_output()


atexit.register(shutdown_midi)


# --------------------------------------------------
# Startup device warning status for the matrix
# --------------------------------------------------

def get_startup_warnings():
    warning_flags = 0

    if not audio_output_available:
        warning_flags |= STARTUP_WARNING_NO_SPEAKER

    # Force one immediate check. The normal MIDI code keeps retrying
    # later, so a missing interface does not stop the application.
    if not open_midi_output(force=True):
        warning_flags |= STARTUP_WARNING_NO_MIDI_OUTPUT

    print(
        f"Startup warning flags | {warning_flags} | "
        f"Speaker={'OK' if audio_output_available else 'MISSING'} | "
        f"MIDI output={'OK' if midi_output_file is not None else 'MISSING'}"
    )

    return warning_flags


# --------------------------------------------------
# Console status
# --------------------------------------------------

def show_settings():
    scale = SCALES[current_scale_index]
    root = ROOTS[current_root_index]

    starting_note = scale_step_to_name(
        current_knob_step,
        scale,
        root,
    )

    degree_number = (
        current_knob_step % len(scale["intervals"])
    ) + 1

    print()
    print("=" * 58)
    print("CURRENT MUSICAL SETTINGS")
    print(f"Selected scale  : {root['name']} {scale['name']}")
    print(
        f"Starting point  : Degree {degree_number}, "
        f"{starting_note}"
    )
    print(f"Knob position   : {current_knob_step}")
    print(
        f"Waveform        : "
        f"{WAVEFORMS[current_waveform_index].capitalize()}"
    )
    print(f"Distance step   : {NOTE_STEP_MM} mm per note")
    print(f"Hysteresis      : {HYSTERESIS_MM} mm")
    print("Pitch range     : A-1 to C11")
    print(
        f"USB speaker     : "
        f"{'Connected' if audio_output_available else 'Not connected'}"
    )
    print(
        f"Theremin MIDI   : Channel "
        f"{current_midi_channel}"
    )
    print("MIDI Thru       : Disabled")
    print("=" * 58)
    print()


# --------------------------------------------------
# Bridge function called by Arduino
# --------------------------------------------------

def update_tone(
    distance_mm: int,
    knob_value: int,
    waveform_index: int,
    scale_index: int,
    root_index: int = 0,
    midi_channel: int = DEFAULT_MIDI_CHANNEL,
):
    global current_distance_step
    global current_waveform_index
    global current_scale_index
    global current_root_index
    global current_knob_step
    global last_print_time

    distance_mm = limit(
        int(distance_mm),
        MIN_DISTANCE,
        MAX_DISTANCE,
    )

    new_knob_step = limit(
        int(knob_value),
        0,
        7,
    )

    new_waveform_index = (
        int(waveform_index) % len(WAVEFORMS)
    )

    new_scale_index = (
        int(scale_index) % len(SCALES)
    )

    new_root_index = (
        int(root_index) % len(ROOTS)
    )

    new_midi_channel = normalize_midi_channel(
        midi_channel
    )

    settings_changed = False

    if new_waveform_index != current_waveform_index:
        current_waveform_index = new_waveform_index
        set_audio_waveform(
            WAVEFORMS[current_waveform_index]
        )
        settings_changed = True

    if new_scale_index != current_scale_index:
        current_scale_index = new_scale_index
        settings_changed = True

    if new_root_index != current_root_index:
        current_root_index = new_root_index
        settings_changed = True

    if new_midi_channel != current_midi_channel:
        set_midi_channel(new_midi_channel)
        settings_changed = True

    if new_knob_step != current_knob_step:
        current_knob_step = new_knob_step
        settings_changed = True

    if settings_changed:
        show_settings()

    if current_distance_step is None:
        current_distance_step = (
            distance_mm - MIN_DISTANCE
        ) // NOTE_STEP_MM

    while distance_mm >= (
        MIN_DISTANCE
        + (current_distance_step + 1) * NOTE_STEP_MM
        + HYSTERESIS_MM
    ):
        current_distance_step += 1

    while (
        current_distance_step > 0
        and distance_mm < (
            MIN_DISTANCE
            + current_distance_step * NOTE_STEP_MM
            - HYSTERESIS_MM
        )
    ):
        current_distance_step -= 1

    scale = SCALES[current_scale_index]
    root = ROOTS[current_root_index]

    total_scale_step = (
        current_knob_step
        + current_distance_step
    )

    requested_midi = scale_step_to_midi(
        total_scale_step,
        scale,
        root,
    )

    played_midi = limit(
        requested_midi,
        MIN_ALLOWED_MIDI,
        MAX_ALLOWED_MIDI,
    )

    frequency = midi_to_frequency(played_midi)

    # Internal audio output is optional. A missing speaker does
    # not affect MIDI generation or MIDI Thru.
    update_audio_output(
        frequency,
        FIXED_AMPLITUDE,
    )

    # External USB MIDI output.
    update_midi_note(played_midi)

    played_note = midi_to_note_name(
        played_midi,
        root["prefer_flats"],
    )

    starting_note = scale_step_to_name(
        current_knob_step,
        scale,
        root,
    )

    now = time.monotonic()

    if now - last_print_time >= 0.5:
        limit_message = ""

        if played_midi != requested_midi:
            limit_message = " | PITCH LIMIT REACHED"

        midi_message = (
            f"MIDI={played_midi} "
            f"Ch={current_midi_channel}"
            if current_midi_note is not None
            else "MIDI=not connected"
        )

        print(
            f"PLAYING | "
            f"Scale={root['name']} {scale['name']} | "
            f"Start={starting_note} | "
            f"Note={played_note} | "
            f"Distance={distance_mm} mm | "
            f"Wave={WAVEFORMS[current_waveform_index]} | "
            f"Frequency={frequency:.1f} Hz | "
            f"{midi_message}"
            f"{limit_message}"
        )

        last_print_time = now


# --------------------------------------------------
# Stop internal and external notes
# --------------------------------------------------

def stop_tone():
    global current_distance_step

    stop_audio_output()
    current_distance_step = None

    stop_midi_note()

    scale = SCALES[current_scale_index]
    root = ROOTS[current_root_index]

    print(
        f"STOPPED | No valid distance | "
        f"Scale={root['name']} {scale['name']} | "
        f"Start={scale_step_to_name(current_knob_step, scale, root)} | "
        f"Wave={WAVEFORMS[current_waveform_index]} | "
        f"MIDI channel={current_midi_channel}"
    )


load_musical_settings()
current_midi_channel = load_midi_channel()

# Register every Bridge handler before optional device startup.
# This guarantees Arduino communication even if audio is absent.
Bridge.provide("load_musical_settings", load_musical_settings)
Bridge.provide("save_musical_settings", save_musical_settings)
Bridge.provide("load_midi_channel", load_midi_channel)
Bridge.provide("save_midi_channel", save_midi_channel)
Bridge.provide("get_startup_warnings", get_startup_warnings)
Bridge.provide("update_tone", update_tone)
Bridge.provide("set_midi_channel", set_midi_channel)
Bridge.provide("stop_tone", stop_tone)

# MIDI is initialized first and does not depend on audio.
open_midi_output(force=True)

# Audio is optional. When no USB speaker is present, the
# WaveGenerator is not created at all.
start_audio_output()

print("Tone generator ready")
show_settings()

App.run()
