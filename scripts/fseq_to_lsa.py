#!/usr/bin/env python3
"""
Convert an xLights FSEQ v2 (uncompressed) sequence to .lsa for SD card playback.

The output .lsa has the loop flag set so the Pico firmware loops it forever.

Usage:
    python3 scripts/fseq_to_lsa.py animation.fseq animation.lsa
    python3 scripts/fseq_to_lsa.py animation.fseq animation.lsa --fps 30

No external dependencies — standard library only.
"""

import argparse
import struct
import sys

# Must match Config.h kLiveLedCount / kLiveFrameBytes
LED_COUNT   = 5120        # 160 x 32  (20 panels: 2 rows × 10 cols)
FRAME_BYTES = LED_COUNT * 3  # 15360 bytes per frame

LSA_MAGIC     = b'LSA1'
LSA_FLAG_LOOP = 0x01


def read_fseq(path):
    with open(path, 'rb') as f:
        raw = f.read()

    if len(raw) < 32:
        sys.exit(f"ERROR: File too short to be a valid FSEQ: {path}")

    # FSEQ v2 header layout (all little-endian):
    #   [0-3]   identifier / magic
    #   [4-5]   data offset  — byte where frame data begins
    #   [6]     minor version
    #   [7]     major version  (must be 2)
    #   [8-9]   variable header length
    #   [10-13] channel count per frame
    #   [14-17] frame count
    #   [18]    step time in ms  (50 → 20 fps, 33 → ~30 fps, 25 → 40 fps)
    #   [19]    flags
    #   [20]    compression type  (0 = none, 1 = zstd, 2 = zlib)

    major_ver = raw[7]
    minor_ver = raw[6]
    if major_ver != 2:
        sys.exit(
            f"ERROR: Only FSEQ v2 is supported (got v{major_ver}.{minor_ver}).\n"
            "Save your xLights sequence — the default .fseq file is always v2."
        )

    data_offset   = struct.unpack_from('<H', raw, 4)[0]
    channel_count = struct.unpack_from('<I', raw, 10)[0]
    frame_count   = struct.unpack_from('<I', raw, 14)[0]
    step_ms       = raw[18]
    comp_type     = raw[20]

    if comp_type != 0:
        sys.exit(
            f"ERROR: FSEQ uses compression type {comp_type}.\n"
            "Re-save from xLights with compression disabled:\n"
            "  File > Preferences > Sequence File Format > uncheck 'Compress FSEQ'"
        )

    if channel_count < FRAME_BYTES:
        sys.exit(
            f"ERROR: FSEQ has {channel_count} channels but {FRAME_BYTES} are needed "
            f"for {LED_COUNT} LEDs ({LED_COUNT} x 3).\n"
            f"Make sure the xLights layout/controller covers all {LED_COUNT} pixels."
        )

    fps = round(1000 / step_ms) if step_ms > 0 else 20
    print(f"FSEQ v{major_ver}.{minor_ver}: {frame_count} frames, "
          f"{channel_count} ch/frame, {step_ms} ms/frame → {fps} fps")

    frames = []
    pos = data_offset
    for i in range(frame_count):
        if pos + channel_count > len(raw):
            print(f"WARNING: Truncated at frame {i} — file may be incomplete.")
            break
        # Take only the first FRAME_BYTES channels (our LED channels)
        frames.append(raw[pos: pos + FRAME_BYTES])
        pos += channel_count

    return frames, fps


def write_lsa(frames, fps, path):
    """
    LSA header (16 bytes):
      [0-3]   'LSA1'
      [4-5]   LED count  (uint16 LE)
      [6-7]   FPS        (uint16 LE)
      [8-11]  frame count (uint32 LE)
      [12]    flags  (0x01 = loop)
      [13-15] reserved
    Followed immediately by frame_count * FRAME_BYTES of raw RGB data.
    """
    header = (
        LSA_MAGIC
        + struct.pack('<H', LED_COUNT)
        + struct.pack('<H', min(fps, 0xFFFF))
        + struct.pack('<I', len(frames))
        + bytes([LSA_FLAG_LOOP, 0, 0, 0])
    )
    assert len(header) == 16

    with open(path, 'wb') as f:
        f.write(header)
        for frame in frames:
            f.write(frame)

    size_kb = (16 + len(frames) * FRAME_BYTES) / 1024
    print(f"Wrote: {path}")
    print(f"  {len(frames)} frames  |  {fps} fps  |  {size_kb:.0f} KB  |  loops forever")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('input',  help='Input .fseq file exported from xLights')
    ap.add_argument('output', help='Output .lsa file to copy to the SD card')
    ap.add_argument('--fps', type=int, default=None,
                    help='Override FPS (default: taken from FSEQ step time)')
    args = ap.parse_args()

    frames, fps = read_fseq(args.input)
    if args.fps:
        fps = args.fps
    if not frames:
        sys.exit("ERROR: No frames extracted.")

    write_lsa(frames, fps, args.output)


if __name__ == '__main__':
    main()
