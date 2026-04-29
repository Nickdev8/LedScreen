#!/usr/bin/env python3
"""
Convert any FFmpeg-readable video (MP4, MKV, GIF, …) to .lsa for SD card playback.

The video is scaled to 160×32 and encoded as raw RGB24. No external Python
dependencies — requires only ffmpeg and ffprobe to be installed on PATH.

Usage:
    python3 scripts/mp4_to_lsa.py clip.mp4 clip.lsa
    python3 scripts/mp4_to_lsa.py clip.mp4 clip.lsa --fps 30

No external Python dependencies — standard library only.
"""

import argparse
import struct
import subprocess
import sys

# Must match Config.h kLiveLedCount / kLiveFrameBytes
LED_COUNT   = 5120           # 160 x 32  (20 panels: 2 rows × 10 cols)
FRAME_BYTES = LED_COUNT * 3  # 15360 bytes per frame
WIDTH, HEIGHT = 160, 32

LSA_MAGIC     = b'LSA1'
LSA_FLAG_LOOP = 0x01

# Physical ceiling imposed by WS2812 bitrate + lane count
MAX_FPS = 43


def probe_fps(path: str) -> float:
    """Return the video's native frame rate via ffprobe."""
    try:
        result = subprocess.run(
            [
                "ffprobe", "-v", "error",
                "-select_streams", "v:0",
                "-show_entries", "stream=r_frame_rate",
                "-of", "default=noprint_wrappers=1:nokey=1",
                path,
            ],
            capture_output=True, text=True, check=True,
        )
    except FileNotFoundError:
        sys.exit("ERROR: ffprobe not found — install ffmpeg (e.g. sudo pacman -S ffmpeg).")
    except subprocess.CalledProcessError as e:
        sys.exit(f"ERROR: ffprobe failed:\n{e.stderr}")

    line = result.stdout.strip()
    if '/' in line:
        num, den = line.split('/')
        return float(num) / float(den) if float(den) else 25.0
    try:
        return float(line)
    except ValueError:
        print(f"WARNING: Could not parse fps '{line}', defaulting to 25.")
        return 25.0


def write_lsa(input_path: str, output_path: str, fps: int) -> None:
    """
    LSA header (16 bytes):
      [0-3]   'LSA1'
      [4-5]   LED count  (uint16 LE)
      [6-7]   FPS        (uint16 LE)
      [8-11]  frame count (uint32 LE)  ← patched after streaming
      [12]    flags  (0x01 = loop)
      [13-15] reserved
    Followed immediately by frame_count * FRAME_BYTES of raw RGB data.
    """
    cmd = [
        "ffmpeg",
        "-i", input_path,
        "-vf", f"scale={WIDTH}:{HEIGHT}",
        "-r", str(fps),
        "-pix_fmt", "rgb24",
        "-f", "rawvideo",
        "-loglevel", "error",
        "pipe:1",
    ]

    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except FileNotFoundError:
        sys.exit("ERROR: ffmpeg not found — install ffmpeg (e.g. sudo pacman -S ffmpeg).")

    frames_written = 0
    with open(output_path, 'wb') as out:
        # Write header with placeholder frame count; patch it once streaming is done.
        out.write(LSA_MAGIC)
        out.write(struct.pack('<H', LED_COUNT))
        out.write(struct.pack('<H', fps))
        frame_count_pos = out.tell()
        out.write(struct.pack('<I', 0))              # placeholder
        out.write(bytes([LSA_FLAG_LOOP, 0, 0, 0]))

        while True:
            chunk = proc.stdout.read(FRAME_BYTES)
            if len(chunk) < FRAME_BYTES:
                break
            out.write(chunk)
            frames_written += 1
            if frames_written % 100 == 0:
                print(f"  {frames_written} frames…", end='\r')

        # Patch the frame count now that we know it.
        out.seek(frame_count_pos)
        out.write(struct.pack('<I', frames_written))

    proc.wait()
    if proc.returncode not in (0, 255):  # 255 = SIGPIPE from closing pipe early, also fine
        err = proc.stderr.read().decode()
        if err:
            sys.exit(f"ERROR: ffmpeg failed:\n{err}")

    if frames_written == 0:
        err = proc.stderr.read().decode()
        sys.exit(f"ERROR: No frames decoded.\n{err}")

    size_mb = (16 + frames_written * FRAME_BYTES) / 1_048_576
    print(f"Wrote: {output_path}")
    print(f"  {frames_written} frames  |  {fps} fps  |  {size_mb:.1f} MB  |  loops forever")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('input',  help='Input video file (MP4, MKV, GIF, …)')
    ap.add_argument('output', help='Output .lsa file to copy to the SD card')
    ap.add_argument('--fps', type=int, default=None,
                    help=f'Override FPS (default: source FPS, capped at {MAX_FPS})')
    args = ap.parse_args()

    src_fps = probe_fps(args.input)
    fps = min(int(args.fps or round(src_fps)), MAX_FPS)
    fps = max(fps, 1)
    print(f"Source: {args.input}  ({src_fps:.2f} fps → encoding at {fps} fps)")

    write_lsa(args.input, args.output, fps)


if __name__ == '__main__':
    main()
