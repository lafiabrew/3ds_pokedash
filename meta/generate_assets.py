#!/usr/bin/env python3
"""
Deterministic generator for PokeDash PKHaX (3ds_pokedash) packaging assets:
- meta/icon.png (48x48 RGBA PNG: Crimson/White Master/Poké Ball with Golden Shiny Star badge)
- meta/banner.png (256x128 RGBA PNG: Poké Ball & PKHaX DNA/Star emblem)
- meta/audio.wav & meta/banner.bnr (Poké Center-inspired ascending chime)
"""

import base64
import math
import os
import struct
import wave
import zlib


def write_png(path: str, width: int, height: int, rgba_bytes: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    raw_rows = bytearray()
    stride = width * 4
    for y in range(height):
        raw_rows.append(0)
        raw_rows.extend(rgba_bytes[y * stride : (y + 1) * stride])

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    compressed = zlib.compress(bytes(raw_rows), 9)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", compressed))
        f.write(chunk(b"IEND", b""))


def generate_icon(path: str) -> None:
    w, h = 48, 48
    buf = bytearray(w * h * 4)
    cx, cy = 23.5, 23.5
    for y in range(h):
        for x in range(w):
            idx = (y * w + x) * 4
            # Deep violet-crimson border and dark background
            if x < 2 or x >= w - 2 or y < 2 or y >= h - 2:
                buf[idx : idx + 4] = bytes((240, 75, 90, 255))
                continue
            buf[idx : idx + 4] = bytes((20, 16, 34, 255))

            dx = x - cx
            dy = y - cy
            r2 = dx * dx + dy * dy

            # Golden shiny 4-point sparkle in top-right corner (around 38, 10)
            sx = abs(x - 38)
            sy = abs(y - 10)
            if (sx == 0 and sy <= 4) or (sy == 0 and sx <= 4) or (sx + sy <= 3):
                buf[idx : idx + 4] = bytes((255, 230, 80, 255))
                continue

            # Poké Ball / Master Ball outer ring (radius 17)
            if r2 <= 17.5 * 17.5:
                if r2 >= 15.0 * 15.0:
                    buf[idx : idx + 4] = bytes((30, 32, 44, 255))
                elif r2 <= 4.2 * 4.2:
                    # Center button inner white
                    buf[idx : idx + 4] = bytes((250, 252, 255, 255))
                elif r2 <= 6.5 * 6.5:
                    # Center button dark ring
                    buf[idx : idx + 4] = bytes((30, 32, 44, 255))
                elif abs(dy) <= 1.8:
                    # Center horizontal seam
                    buf[idx : idx + 4] = bytes((30, 32, 44, 255))
                elif dy < 0:
                    # Top half: Crimson Red with two white/gold PKHaX arcs
                    if (-12 <= dy <= -7) and (7 <= abs(dx) <= 11):
                        buf[idx : idx + 4] = bytes((255, 215, 90, 255))
                    else:
                        buf[idx : idx + 4] = bytes((228, 52, 68, 255))
                else:
                    # Bottom half: Clean pearl white
                    buf[idx : idx + 4] = bytes((240, 244, 252, 255))

    write_png(path, w, h, bytes(buf))


def generate_banner(path: str) -> bytes:
    w, h = 256, 128
    buf = bytearray(w * h * 4)
    cx, cy = 128.0, 50.0
    for y in range(h):
        for x in range(w):
            idx = (y * w + x) * 4
            r = 28 + (y * 24) // h
            g = 14 + (y * 16) // h
            b = 44 + (y * 28) // h
            buf[idx : idx + 4] = bytes((r, g, b, 255))

            dx = x - cx
            dy = y - cy
            r2 = dx * dx + dy * dy
            if r2 <= 28.0 * 28.0:
                if r2 >= 25.0 * 25.0 or (6.0 * 6.0 <= r2 <= 9.5 * 9.5) or abs(dy) <= 2.5:
                    buf[idx : idx + 4] = bytes((28, 30, 42, 255))
                elif r2 < 6.0 * 6.0:
                    buf[idx : idx + 4] = bytes((255, 235, 110, 255))
                elif dy < 0:
                    buf[idx : idx + 4] = bytes((230, 55, 72, 255))
                else:
                    buf[idx : idx + 4] = bytes((242, 245, 252, 255))

    draw_banner_text_centered(buf, w, h, 86, "POKEDASH", 2, (255, 225, 95, 255))
    draw_banner_text_centered(buf, w, h, 108, "POKEMON SAVE EDITOR", 1, (220, 235, 255, 255))
    raw = bytes(buf)
    write_png(path, w, h, raw)
    return raw


def generate_audio(path: str) -> None:
    sample_rate = 22050
    duration_s = 0.8
    n_samples = int(sample_rate * duration_s)
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        frames = bytearray()
        for i in range(n_samples):
            t = i / sample_rate
            freq = 587.33 if t < 0.25 else (783.99 if t < 0.5 else 987.77)
            env = max(0.0, 1.0 - (t / duration_s))
            val = int(12000 * env * math.sin(2.0 * math.pi * freq * t))
            frames.extend(struct.pack("<h", val))
        wf.writeframes(bytes(frames))


from generate_bnr_shared import draw_banner_text_centered, write_cbmd_bnr  # type: ignore


def main() -> None:
    meta_dir = os.path.dirname(os.path.abspath(__file__))
    generate_icon(os.path.join(meta_dir, "icon.png"))
    banner_rgba = generate_banner(os.path.join(meta_dir, "banner.png"))
    generate_audio(os.path.join(meta_dir, "audio.wav"))
    write_cbmd_bnr(os.path.join(meta_dir, "banner.bnr"), (587.33, 783.99, 987.77), banner_rgba)
    print("Generated PokeDash meta/icon.png, meta/banner.png, meta/audio.wav, meta/banner.bnr")


if __name__ == "__main__":
    main()
