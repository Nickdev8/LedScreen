#!/usr/bin/env python3
"""
Stream .rgb animation files to the LED screen over USB serial.
Replaces the SD card — works even without a working SD card reader.

Usage:
    python3 scripts/stream_anim.py /dev/ttyACM0 path/to/file.rgb [fps]
    python3 scripts/stream_anim.py /dev/ttyACM0 path/to/playlist.txt [fps]

Each frame is kFrameBytes = 5120 pixels * 3 bytes = 15360 bytes of raw RGB.
Protocol: 0xAB 0xCD magic header + 15360 bytes per frame.

Install dependency: pip install pyserial
"""

import sys
import time
import serial

FRAME_BYTES = 5120 * 3  # 15360
MAGIC = b'\xab\xcd'


def load_rgb(path):
    with open(path, 'rb') as f:
        data = f.read()
    frames = [data[i:i + FRAME_BYTES] for i in range(0, len(data), FRAME_BYTES)]
    frames = [f for f in frames if len(f) == FRAME_BYTES]
    return frames


def load_playlist(path):
    """Load files listed in a playlist.txt (same format as the firmware uses)."""
    import os
    base = os.path.dirname(path)
    frames = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split(',')
            rgb_path = parts[0].strip().lstrip('/')
            full = os.path.join(base, rgb_path)
            loaded = load_rgb(full)
            print(f"  {rgb_path}: {len(loaded)} frames")
            frames.extend(loaded)
    return frames


def stream(port, source, fps):
    if source.endswith('.txt'):
        print(f"Loading playlist: {source}")
        frames = load_playlist(source)
    else:
        frames = load_rgb(source)

    if not frames:
        print("No complete frames found — check file path and size.")
        sys.exit(1)

    print(f"Loaded {len(frames)} frames ({len(frames) * FRAME_BYTES / 1024:.0f} KB)")
    print(f"Streaming at {fps} fps to {port} — Ctrl+C to stop")

    ser = serial.Serial(port, 115200, timeout=1)
    time.sleep(0.5)

    delay = 1.0 / fps
    i = 0
    sent = 0
    t_start = time.monotonic()

    try:
        while True:
            t_frame = time.monotonic()
            ser.write(MAGIC + frames[i])
            i = (i + 1) % len(frames)
            sent += 1

            if sent % 30 == 0:
                elapsed = time.monotonic() - t_start
                print(f"  {sent} frames sent, {sent / elapsed:.1f} fps actual")

            # Pace to target fps; account for write time
            elapsed_frame = time.monotonic() - t_frame
            sleep = delay - elapsed_frame
            if sleep > 0:
                time.sleep(sleep)

    except KeyboardInterrupt:
        print(f"\nStopped after {sent} frames.")
    finally:
        ser.close()


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    port = sys.argv[1]
    source = sys.argv[2]
    fps = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    stream(port, source, fps)
