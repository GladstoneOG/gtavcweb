#!/usr/bin/env python3
"""
GTA: Vice City Web (reVC WASM) — Asset Packager
================================================
Packages retail Grand Theft Auto: Vice City assets + reVC overlay files into:
  1. Incremental slice-chunk VFS packs (`public/chunks/core.part00`, ... and optional
     `cuts.part00`, ...) in GTAVCPK2 format (each decompressed chunk <= 22 MiB and
     each gzipped `.partXX` file strictly <= 18 MiB for Cloudflare Workers Free Plan).
  2. Generated `audio/sound.cache` inside the core pack matching PC `StreamedNameTable`
     (`TOTAL_STREAMED_SOUNDS` little-endian uint32 durations in ms).
  3. Lazy-streamed radio & mission audio (`public/audio/`), with `.adf` radio stations
     losslessly XOR-decoded (`0x22`) and segmented into < 18 MiB MP3 segments, `.mp3`
     tracks copied directly, and `.wav` speech transcoded to 64 kbps MP3.
  4. Asset manifest (`public/chunks/manifest.json`, version 2, format `GTAVCPK2`).
"""

from __future__ import annotations

import argparse
from collections import deque
from concurrent.futures import ThreadPoolExecutor, as_completed
import glob
import gzip
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
ROOT_DIR = os.path.abspath(os.path.join(PROJECT_DIR, ".."))

DEFAULT_RETAIL_DIR = os.path.abspath(os.path.join(ROOT_DIR, "..", "Grand Theft Auto Vice City"))
OVERLAY_DIR = os.path.join(PROJECT_DIR, "engine", "gamefiles")
SAMPMAN_H = os.path.join(PROJECT_DIR, "engine", "src", "audio", "sampman.h")
AUDIO_ENUMS_H = os.path.join(PROJECT_DIR, "engine", "src", "audio", "audio_enums.h")

PUBLIC_DIR = os.path.join(PROJECT_DIR, "public")
CHUNKS_DIR = os.path.join(PUBLIC_DIR, "chunks")
AUDIO_OUT_DIR = os.path.join(PUBLIC_DIR, "audio")

ARCHIVE_MAGIC = b"GTAVCPK2"
MAX_UNCOMPRESSED_CHUNK_BYTES = 22 * 1024 * 1024
MAX_COMPRESSED_CHUNK_BYTES = 18 * 1024 * 1024
VERIFY_MAX_FILE_SIZE = 19 * 1024 * 1024
VERIFY_MAX_TOTAL_FILES = 20000
MP3_TARGET_MAX_BYTES = int(17.2 * 1024 * 1024)
BITRATE_LADDER_KBPS = [96, 80, 64, 56, 48, 40, 32]
ADF_XOR_KEY = 0x22
ADF_XOR_TABLE = bytes(b ^ ADF_XOR_KEY for b in range(256))


def parse_total_streamed_sounds(audio_enums_path: str) -> int:
    """Count PC eStreamedSounds enum entries before TOTAL_STREAMED_SOUNDS."""
    with open(audio_enums_path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    match = re.search(r"enum\s+eStreamedSounds\s*\{(.*?)\};", text, re.S)
    if not match:
        raise RuntimeError(f"Could not find enum eStreamedSounds in {audio_enums_path}")

    body = match.group(1)
    if_stack: list[bool] = []
    count = 0

    for raw_line in body.splitlines():
        line = raw_line.split("//", 1)[0].strip()
        if not line:
            continue
        if line.startswith("#ifdef "):
            macro = line[len("#ifdef "):].strip()
            # PC build: GTA_PS2 is undefined
            if_stack.append(macro != "GTA_PS2")
            continue
        if line.startswith("#ifndef "):
            macro = line[len("#ifndef "):].strip()
            if_stack.append(macro == "GTA_PS2")
            continue
        if line.startswith("#else"):
            if if_stack:
                if_stack[-1] = not if_stack[-1]
            continue
        if line.startswith("#endif"):
            if if_stack:
                if_stack.pop()
            continue
        if line.startswith("#"):
            continue

        if not all(if_stack):
            continue

        if "TOTAL_STREAMED_SOUNDS" in line:
            break
        if "," in line or re.match(r"^[A-Z0-9_]+", line):
            count += 1

    if count <= 0:
        raise RuntimeError(f"Parsed 0 PC eStreamedSounds entries from {audio_enums_path}")
    return count


def parse_streamed_name_table(sampman_path: str) -> list[str]:
    """Extract the PC StreamedNameTable entries from reVC's sampman.h."""
    with open(sampman_path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()

    start_idx = None
    for idx in range(len(lines) - 1, -1, -1):
        if "StreamedNameTable" in lines[idx] and "static char" in lines[idx]:
            start_idx = idx
            break

    if start_idx is None:
        raise RuntimeError(f"Could not find PC StreamedNameTable in {sampman_path}")

    entries: list[str] = []
    for line in lines[start_idx:]:
        for match in re.finditer(r'"(AUDIO\\\\[^"]+)"', line):
            entries.append(match.group(1).replace("\\\\", "\\"))
        if "};" in line:
            break

    if not entries:
        raise RuntimeError(f"Parsed 0 entries from StreamedNameTable in {sampman_path}")
    return entries


def read_wav_duration_ms(wav_path: str) -> int | None:
    """Compute exact WAV duration in ms matching reVC's CWavFile/header layout."""
    try:
        with open(wav_path, "rb") as f:
            riff = f.read(12)
            if len(riff) < 12 or riff[0:4] != b"RIFF" or riff[8:12] != b"WAVE":
                return None

            fmt_tag = channels = sample_rate = block_align = 0
            while True:
                chunk_hdr = f.read(8)
                if len(chunk_hdr) < 8:
                    break
                chunk_id, chunk_size = struct.unpack("<4sI", chunk_hdr)
                if chunk_id == b"fmt ":
                    fmt_data = f.read(chunk_size)
                    if chunk_size & 1:
                        f.seek(1, os.SEEK_CUR)
                    if len(fmt_data) < 16:
                        return None
                    fmt_tag, channels, sample_rate, _, block_align, _ = struct.unpack_from(
                        "<HHIIHH", fmt_data, 0
                    )
                elif chunk_id == b"data":
                    if sample_rate == 0 or block_align == 0 or channels == 0:
                        return None
                    if fmt_tag == 1:  # PCM
                        samples_per_block = 1
                    elif fmt_tag in (0x11, 0x69):  # IMA ADPCM / Xbox ADPCM
                        samples_per_block = (block_align // channels - 4) * 2 + 1
                    else:
                        return None
                    sample_count = (chunk_size // block_align) * samples_per_block
                    return max(1, int(sample_count * 1000.0 / sample_rate))
                else:
                    f.seek(chunk_size + (chunk_size & 1), os.SEEK_CUR)
    except OSError:
        return None
    return None


def probe_duration_ms(file_path: str) -> int:
    """Return audio track duration in milliseconds via WAV RIFF header or ffprobe."""
    if file_path.lower().endswith(".wav"):
        ms = read_wav_duration_ms(file_path)
        if ms is not None and ms > 0:
            return ms

    cmd = [
        "ffprobe",
        "-v", "error",
        "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1:nokey=1",
        file_path,
    ]
    res = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if res.returncode != 0 or not res.stdout.strip():
        raise RuntimeError(f"ffprobe failed for {file_path}: {res.stderr.strip()}")
    seconds = float(res.stdout.strip())
    return max(1, int(round(seconds * 1000.0)))


def build_audio_lookup(retail_dir: str) -> dict[str, str]:
    """Map lowercase filename -> actual path in retail Audio directory."""
    audio_dir = None
    for candidate in ("Audio", "audio"):
        p = os.path.join(retail_dir, candidate)
        if os.path.isdir(p):
            audio_dir = p
            break
    if audio_dir is None:
        raise FileNotFoundError(f"Missing retail Audio directory in {retail_dir}")

    lookup: dict[str, str] = {}
    for entry in os.listdir(audio_dir):
        full = os.path.join(audio_dir, entry)
        if os.path.isfile(full):
            lookup[entry.lower()] = full
    return lookup


def resolve_retail_audio_path(fn_lower: str, audio_lookup: dict[str, str]) -> str | None:
    """Resolve a StreamedNameTable filename against retail Audio/ (with .mp3 -> .adf fallback)."""
    if fn_lower in audio_lookup:
        return audio_lookup[fn_lower]
    stem, ext = os.path.splitext(fn_lower)
    if ext == ".mp3":
        adf_key = f"{stem}.adf"
        if adf_key in audio_lookup:
            return audio_lookup[adf_key]
    elif ext == ".adf":
        mp3_key = f"{stem}.mp3"
        if mp3_key in audio_lookup:
            return audio_lookup[mp3_key]
    return None


def compute_output_stems(unique_entries: list[str]) -> dict[str, str]:
    """Map each unique lowercase entry name to a collision-free output stem."""
    by_stem: dict[str, list[str]] = {}
    for fn_lower in unique_entries:
        stem, _ = os.path.splitext(fn_lower)
        by_stem.setdefault(stem, []).append(fn_lower)

    out_stems: dict[str, str] = {}
    used_stems: set[str] = set()
    for fn_lower in unique_entries:
        stem, ext = os.path.splitext(fn_lower)
        group = by_stem[stem]
        if len(group) > 1 and ext not in (".mp3", ".adf"):
            candidate = f"{stem}_{ext.lstrip('.')}"
        else:
            candidate = stem
        if candidate in used_stems:
            candidate = f"{stem}_{ext.lstrip('.')}"
        if candidate in used_stems:
            raise RuntimeError(f"Audio output stem collision for {fn_lower} -> {candidate}")
        used_stems.add(candidate)
        out_stems[fn_lower] = candidate
    return out_stems


def choose_mp3_bitrate(duration_ms: int) -> int:
    """Choose highest bitrate in BITRATE_LADDER_KBPS predicted to stay under MP3_TARGET_MAX_BYTES."""
    dur_sec = max(duration_ms / 1000.0, 0.1)
    max_kbps = (MP3_TARGET_MAX_BYTES * 8.0) / dur_sec / 1000.0
    for kbps in BITRATE_LADDER_KBPS:
        if kbps <= max_kbps:
            return kbps
    return BITRATE_LADDER_KBPS[-1]


def list_existing_segments(out_stem: str) -> list[str]:
    """Return sorted existing segment paths for a radio station."""
    pattern = os.path.join(AUDIO_OUT_DIR, f"{out_stem}_seg[0-9][0-9]*.mp3")
    return sorted(glob.glob(pattern))


def process_adf_station(
    orig_lower_name: str,
    src_path: str,
    out_stem: str,
    force: bool = False,
) -> tuple[str, dict, list[str], bool]:
    """XOR-decode `.adf` (0x22), probe exact duration, and segment losslessly into <18 MiB MP3s."""
    src_mtime = os.path.getmtime(src_path)
    existing_segs = list_existing_segments(out_stem)

    if not force and existing_segs:
        all_ok = all(
            0 < os.path.getsize(seg) < MAX_COMPRESSED_CHUNK_BYTES
            and os.path.getmtime(seg) >= src_mtime
            for seg in existing_segs
        )
        if all_ok:
            segments = []
            cum_ms = 0
            for seg_path in existing_segs:
                seg_dur = probe_duration_ms(seg_path)
                seg_size = os.path.getsize(seg_path)
                seg_name = os.path.basename(seg_path)
                segments.append({
                    "url": f"/audio/{seg_name}",
                    "startMs": cum_ms,
                    "durationMs": seg_dur,
                    "size": seg_size,
                })
                cum_ms += seg_dur
            return (
                orig_lower_name,
                {"durationMs": cum_ms, "segments": segments},
                [os.path.basename(p) for p in existing_segs],
                True,
            )

    decoded_tmp = os.path.join(AUDIO_OUT_DIR, f".tmp_{out_stem}_decoded.mp3")
    try:
        with open(src_path, "rb") as fin:
            raw_adf = fin.read()
        decoded_bytes = raw_adf.translate(ADF_XOR_TABLE)
        with open(decoded_tmp, "wb") as fout:
            fout.write(decoded_bytes)
        del raw_adf
        del decoded_bytes

        total_duration_ms = probe_duration_ms(decoded_tmp)

        seg_paths: list[str] = []
        for seg_time in (900, 600, 300, 180):
            for stale in list_existing_segments(out_stem):
                os.remove(stale)

            out_pattern = os.path.join(AUDIO_OUT_DIR, f"{out_stem}_seg%02d.mp3")
            cmd = [
                "ffmpeg",
                "-y",
                "-v", "error",
                "-i", decoded_tmp,
                "-c", "copy",
                "-f", "segment",
                "-segment_time", str(seg_time),
                out_pattern,
            ]
            res = subprocess.run(cmd, capture_output=True, text=True, check=False)
            if res.returncode != 0:
                raise RuntimeError(f"ffmpeg segment failed for {src_path}: {res.stderr.strip()}")

            seg_paths = list_existing_segments(out_stem)
            if not seg_paths:
                raise RuntimeError(f"ffmpeg produced no segments for {src_path}")

            if all(0 < os.path.getsize(p) < MAX_COMPRESSED_CHUNK_BYTES for p in seg_paths):
                break
        else:
            raise RuntimeError(
                f"Could not segment {src_path} under {MAX_COMPRESSED_CHUNK_BYTES} bytes"
            )

        segments = []
        cum_ms = 0
        for seg_path in seg_paths:
            seg_dur = probe_duration_ms(seg_path)
            seg_size = os.path.getsize(seg_path)
            seg_name = os.path.basename(seg_path)
            segments.append({
                "url": f"/audio/{seg_name}",
                "startMs": cum_ms,
                "durationMs": seg_dur,
                "size": seg_size,
            })
            cum_ms += seg_dur

        return (
            orig_lower_name,
            {"durationMs": total_duration_ms, "segments": segments},
            [os.path.basename(p) for p in seg_paths],
            False,
        )
    finally:
        if os.path.exists(decoded_tmp):
            os.remove(decoded_tmp)


def process_single_track(
    orig_lower_name: str,
    src_path: str,
    out_stem: str,
    duration_ms: int,
    force: bool = False,
) -> tuple[str, dict, list[str], bool]:
    """Copy small `.mp3` directly or transcode `.wav`/oversized `.mp3` to <18 MiB MP3."""
    out_name = f"{out_stem}.mp3"
    out_path = os.path.join(AUDIO_OUT_DIR, out_name)
    src_ext = os.path.splitext(src_path)[1].lower()
    src_size = os.path.getsize(src_path)

    if not force and os.path.isfile(out_path):
        out_size = os.path.getsize(out_path)
        if (
            0 < out_size < MAX_COMPRESSED_CHUNK_BYTES
            and os.path.getmtime(out_path) >= os.path.getmtime(src_path)
        ):
            return (
                orig_lower_name,
                {"url": f"/audio/{out_name}", "durationMs": duration_ms, "size": out_size},
                [out_name],
                True,
            )

    if src_ext == ".mp3" and src_size <= 17 * 1024 * 1024:
        shutil.copy2(src_path, out_path)
    elif src_ext == ".wav":
        tmp_path = out_path + ".tmp.mp3"
        cmd = [
            "ffmpeg",
            "-y",
            "-v", "error",
            "-i", src_path,
            "-vn",
            "-c:a", "libmp3lame",
            "-b:a", "64k",
            tmp_path,
        ]
        res = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if res.returncode != 0:
            if os.path.exists(tmp_path):
                os.remove(tmp_path)
            raise RuntimeError(f"ffmpeg failed for {src_path}: {res.stderr.strip()}")
        if os.path.getsize(tmp_path) >= MAX_COMPRESSED_CHUNK_BYTES:
            os.remove(tmp_path)
            raise RuntimeError(f"Transcoded {out_name} exceeds {MAX_COMPRESSED_CHUNK_BYTES} bytes")
        os.replace(tmp_path, out_path)
    else:
        start_kbps = choose_mp3_bitrate(duration_ms)
        start_idx = BITRATE_LADDER_KBPS.index(start_kbps)
        tmp_path = out_path + ".tmp.mp3"
        encoded_ok = False
        for kbps in BITRATE_LADDER_KBPS[start_idx:]:
            cmd = [
                "ffmpeg",
                "-y",
                "-v", "error",
                "-i", src_path,
                "-vn",
                "-c:a", "libmp3lame",
                "-b:a", f"{kbps}k",
            ]
            if kbps <= 48:
                cmd.extend(["-ac", "1"])
            cmd.append(tmp_path)
            res = subprocess.run(cmd, capture_output=True, text=True, check=False)
            if res.returncode != 0:
                if os.path.exists(tmp_path):
                    os.remove(tmp_path)
                raise RuntimeError(f"ffmpeg failed for {src_path} @ {kbps}k: {res.stderr.strip()}")
            if os.path.getsize(tmp_path) < MAX_COMPRESSED_CHUNK_BYTES:
                os.replace(tmp_path, out_path)
                encoded_ok = True
                break
        if not encoded_ok:
            if os.path.exists(tmp_path):
                os.remove(tmp_path)
            raise RuntimeError(f"Could not fit {out_name} below {MAX_COMPRESSED_CHUNK_BYTES} bytes")

    out_size = os.path.getsize(out_path)
    return (
        orig_lower_name,
        {"url": f"/audio/{out_name}", "durationMs": duration_ms, "size": out_size},
        [out_name],
        False,
    )


def collect_vfs_entries(
    retail_dir: str,
    overlay_dir: str,
    sound_cache_bytes: bytes | None,
    audio_lookup: dict[str, str],
    read_contents: bool = True,
) -> tuple[list[tuple[str, bytes]], list[tuple[str, bytes]]]:
    """Collect core VFS entries and optional cutscene pack entries (case-insensitive overlay)."""
    core_map: dict[str, tuple[str, bytes]] = {}
    cuts_entries: list[tuple[str, bytes]] = []

    def put_core(rel_path: str, raw_bytes: bytes) -> None:
        norm = rel_path.replace("\\", "/").lstrip("/")
        core_map[norm.lower()] = (norm, raw_bytes)

    def read_file_bytes(path: str) -> bytes:
        if not read_contents:
            return b""
        with open(path, "rb") as f:
            return f.read()

    def add_retail_tree(sub_dir: str, skip_lower: set[str] | None = None) -> None:
        base = None
        for entry in os.listdir(retail_dir):
            if entry.lower() == sub_dir.lower():
                cand = os.path.join(retail_dir, entry)
                if os.path.isdir(cand):
                    base = cand
                    break
        if base is None:
            raise FileNotFoundError(f"Missing required retail folder '{sub_dir}' in {retail_dir}")
        for dp, _, filenames in os.walk(base):
            for fn in filenames:
                full = os.path.join(dp, fn)
                rel = os.path.relpath(full, retail_dir).replace("\\", "/")
                if skip_lower and rel.lower() in skip_lower:
                    continue
                put_core(rel, read_file_bytes(full))

    for folder in ("data", "models", "TEXT", "txd", "skins"):
        skip = {"models/txd.img", "models/txd.dir"} if folder == "models" else None
        add_retail_tree(folder, skip_lower=skip)

    # Retail anim/: VC has ped.ifp, cuts.dir (core) and cuts.img (optional cuts pack)
    anim_dir = os.path.join(retail_dir, "anim")
    if not os.path.isdir(anim_dir):
        raise FileNotFoundError(f"Missing retail anim folder: {anim_dir}")
    anim_files = {
        fn.lower(): fn
        for fn in os.listdir(anim_dir)
        if os.path.isfile(os.path.join(anim_dir, fn))
    }
    for req in ("ped.ifp", "cuts.dir"):
        if req not in anim_files:
            raise FileNotFoundError(f"Missing required anim/{req} in {anim_dir}")
        actual_name = anim_files[req]
        put_core(f"anim/{actual_name}", read_file_bytes(os.path.join(anim_dir, actual_name)))

    if "cuts.img" in anim_files:
        actual_cuts = anim_files["cuts.img"]
        cuts_entries.append(
            (f"anim/{actual_cuts}", read_file_bytes(os.path.join(anim_dir, actual_cuts)))
        )

    # Retail SFX bank + index + generated sound.cache
    for req_audio in ("sfx.sdt", "sfx.raw"):
        if req_audio not in audio_lookup:
            raise FileNotFoundError(f"Missing required audio/{req_audio} in {retail_dir}/Audio")
        src = audio_lookup[req_audio]
        put_core(f"audio/{os.path.basename(src)}", read_file_bytes(src))

    if sound_cache_bytes is not None:
        put_core("audio/sound.cache", sound_cache_bytes)
    else:
        put_core("audio/sound.cache", b"")

    # Overlay reVC gamefiles ON TOP of retail files where paths collide (case-insensitive)
    if not os.path.isdir(overlay_dir):
        raise FileNotFoundError(f"Missing reVC overlay gamefiles directory: {overlay_dir}")
    overlay_count = 0
    for dp, _, filenames in os.walk(overlay_dir):
        for fn in filenames:
            full = os.path.join(dp, fn)
            rel = os.path.relpath(full, overlay_dir).replace("\\", "/")
            put_core(rel, read_file_bytes(full))
            overlay_count += 1

    print(f"[VFS] Core files: {len(core_map)} (after applying {overlay_count} reVC overlay files)")
    core_entries = [core_map[k] for k in sorted(core_map.keys())]
    return core_entries, cuts_entries


def serialize_gtavcpk2_chunk(records: list[tuple[bytes, int, int, memoryview]]) -> bytes:
    """Serialize a list of (path_utf8, total_size, slice_offset, slice_mv) into GTAVCPK2 bytes."""
    parts: list[bytes | memoryview] = [ARCHIVE_MAGIC, struct.pack("<I", len(records))]
    for path_bytes, total_size, slice_offset, slice_mv in records:
        parts.append(struct.pack("<H", len(path_bytes)))
        parts.append(path_bytes)
        parts.append(struct.pack("<III", total_size, slice_offset, len(slice_mv)))
        if len(slice_mv) > 0:
            parts.append(slice_mv)
    return b"".join(parts)


def write_packed_chunks_v2(prefix: str, entries: list[tuple[str, bytes]]) -> dict:
    """Pack entries into self-contained gzipped GTAVCPK2 slice-chunks (each <= 18 MiB gz)."""
    for stale in glob.glob(os.path.join(CHUNKS_DIR, f"{prefix}.part*")):
        os.remove(stale)

    queue: deque[tuple[str, int, int, memoryview]] = deque()
    for rel_path, raw_bytes in entries:
        path_bytes = rel_path.encode("utf-8")
        if len(path_bytes) > 0xFFFF:
            raise ValueError(f"Path too long: {rel_path}")
        queue.append((rel_path, len(raw_bytes), 0, memoryview(raw_bytes)))

    chunks_meta: list[dict] = []
    total_uncompressed_bytes = 0
    total_compressed_bytes = 0
    pack_hasher = hashlib.sha256()
    chunk_idx = 0

    def flush_chunk(chunk_records: list[tuple[bytes, int, int, memoryview]]) -> None:
        nonlocal chunk_idx, total_uncompressed_bytes, total_compressed_bytes
        if not chunk_records:
            return

        uncompressed = serialize_gtavcpk2_chunk(chunk_records)
        compressed = gzip.compress(uncompressed, compresslevel=6, mtime=0)

        while len(compressed) > MAX_COMPRESSED_CHUNK_BYTES:
            if len(chunk_records) > 1:
                p_bytes, tot, off, smv = chunk_records.pop()
                queue.appendleft((p_bytes.decode("utf-8"), tot, off, smv))
            else:
                p_bytes, tot, off, smv = chunk_records[0]
                cur_len = len(smv)
                if cur_len <= 1024:
                    raise RuntimeError(
                        f"Cannot compress single record {p_bytes.decode('utf-8')} below {MAX_COMPRESSED_CHUNK_BYTES} bytes"
                    )
                target_len = int(cur_len * (MAX_COMPRESSED_CHUNK_BYTES - 128 * 1024) / len(compressed))
                target_len = max(1024 * 1024 if cur_len > 1024 * 1024 else cur_len // 2, min(cur_len - 256 * 1024, target_len))
                if target_len >= cur_len:
                    target_len = cur_len // 2
                queue.appendleft((p_bytes.decode("utf-8"), tot, off + target_len, smv[target_len:]))
                chunk_records[0] = (p_bytes, tot, off, smv[:target_len])

            uncompressed = serialize_gtavcpk2_chunk(chunk_records)
            compressed = gzip.compress(uncompressed, compresslevel=6, mtime=0)

        part_name = f"{prefix}.part{chunk_idx:02d}"
        part_path = os.path.join(CHUNKS_DIR, part_name)
        with open(part_path, "wb") as f:
            f.write(compressed)

        chunk_sha = hashlib.sha256(compressed).hexdigest()
        pack_hasher.update(compressed)
        uncomp_len = len(uncompressed)
        comp_len = len(compressed)
        total_uncompressed_bytes += uncomp_len
        total_compressed_bytes += comp_len

        chunks_meta.append({
            "url": f"/chunks/{part_name}",
            "compressedSize": comp_len,
            "uncompressedSize": uncomp_len,
            "sha256": chunk_sha,
        })
        print(
            f"  [CHUNK] {part_name}: gz={comp_len:>10,} B ({comp_len / (1024 * 1024):.2f} MiB) | "
            f"raw={uncomp_len:>10,} B ({uncomp_len / (1024 * 1024):.2f} MiB) | records={len(chunk_records)}"
        )
        chunk_idx += 1

    chunk_records: list[tuple[bytes, int, int, memoryview]] = []
    chunk_uncomp_size = 12  # 8B magic + 4B record_count

    while queue:
        rel_path, total_size, slice_offset, mv = queue[0]
        path_bytes = rel_path.encode("utf-8")
        rec_overhead = 2 + len(path_bytes) + 12  # path_len + path + total_size + slice_offset + slice_len
        avail = MAX_UNCOMPRESSED_CHUNK_BYTES - chunk_uncomp_size - rec_overhead

        max_single_chunk_payload = MAX_UNCOMPRESSED_CHUNK_BYTES - 12 - rec_overhead
        if avail <= 0 or (len(mv) <= max_single_chunk_payload and len(mv) > avail and len(chunk_records) > 0):
            flush_chunk(chunk_records)
            chunk_records = []
            chunk_uncomp_size = 12
            continue

        queue.popleft()
        take = min(len(mv), avail) if len(mv) > 0 else 0
        if take < len(mv):
            queue.appendleft((rel_path, total_size, slice_offset + take, mv[take:]))

        chunk_records.append((path_bytes, total_size, slice_offset, mv[:take]))
        chunk_uncomp_size += rec_overhead + take

        if chunk_uncomp_size >= MAX_UNCOMPRESSED_CHUNK_BYTES - 512:
            flush_chunk(chunk_records)
            chunk_records = []
            chunk_uncomp_size = 12

    if chunk_records:
        flush_chunk(chunk_records)

    print(
        f"[PACK:{prefix}] files={len(entries)} raw={total_uncompressed_bytes:,} B "
        f"({total_uncompressed_bytes / (1024 * 1024):.2f} MiB) -> "
        f"gz={total_compressed_bytes:,} B ({total_compressed_bytes / (1024 * 1024):.2f} MiB) "
        f"in {len(chunks_meta)} chunks"
    )
    return {
        "fileCount": len(entries),
        "totalUncompressedSize": total_uncompressed_bytes,
        "totalCompressedSize": total_compressed_bytes,
        "packSha256": pack_hasher.hexdigest(),
        "chunks": chunks_meta,
    }


def verify_output_sizes() -> tuple[int, int]:
    """Ensure every file in public/chunks/ and public/audio/ is < 19 MiB and total files < 20,000."""
    checked = 0
    max_seen = 0
    for folder in (CHUNKS_DIR, AUDIO_OUT_DIR):
        for dp, _, filenames in os.walk(folder):
            for fn in filenames:
                full = os.path.join(dp, fn)
                sz = os.path.getsize(full)
                checked += 1
                if sz > max_seen:
                    max_seen = sz
                if sz >= VERIFY_MAX_FILE_SIZE:
                    raise RuntimeError(
                        f"File {full} is {sz:,} bytes (>= {VERIFY_MAX_FILE_SIZE:,} byte limit)!"
                    )

    total_public_files = sum(len(filenames) for _, _, filenames in os.walk(PUBLIC_DIR))
    if total_public_files >= VERIFY_MAX_TOTAL_FILES:
        raise RuntimeError(
            f"Total files in public/ is {total_public_files} (>= {VERIFY_MAX_TOTAL_FILES} CF limit)!"
        )
    return checked, max_seen


def main() -> int:
    parser = argparse.ArgumentParser(description="Package GTA: Vice City Web assets and lazy audio.")
    parser.add_argument("--retail-dir", default=DEFAULT_RETAIL_DIR, help="Path to retail Grand Theft Auto Vice City")
    parser.add_argument("--force-audio", action="store_true", help="Re-decode/transcode all streamed audio tracks")
    parser.add_argument("--workers", type=int, default=min(8, os.cpu_count() or 4), help="FFmpeg worker threads")
    parser.add_argument("--check-only", action="store_true", help="Validate tables, paths, and collisions without writing assets")
    args = parser.parse_args()

    print("[1/5] Parsing StreamedNameTable and TOTAL_STREAMED_SOUNDS from reVC...")
    total_streamed_sounds = parse_total_streamed_sounds(AUDIO_ENUMS_H)
    streamed_entries = parse_streamed_name_table(SAMPMAN_H)
    if len(streamed_entries) != total_streamed_sounds:
        raise RuntimeError(
            f"StreamedNameTable count ({len(streamed_entries)}) != TOTAL_STREAMED_SOUNDS ({total_streamed_sounds})"
        )
    print(f"  Found {len(streamed_entries)} streamed sound table entries (matches TOTAL_STREAMED_SOUNDS).")

    audio_lookup = build_audio_lookup(args.retail_dir)
    ordered_filenames: list[str] = []
    resolved_by_name: dict[str, str] = {}
    missing_tracks: list[str] = []

    for entry in streamed_entries:
        fn_lower = entry.split("\\")[-1].lower()
        ordered_filenames.append(fn_lower)
        resolved = resolve_retail_audio_path(fn_lower, audio_lookup)
        if resolved is None:
            missing_tracks.append(entry)
        else:
            resolved_by_name[fn_lower] = resolved

    if missing_tracks:
        raise FileNotFoundError(f"Missing retail audio tracks ({len(missing_tracks)}): {missing_tracks[:10]}")

    unique_names = sorted(set(ordered_filenames))
    out_stems = compute_output_stems(unique_names)
    adf_names = [fn for fn in unique_names if resolved_by_name[fn].lower().endswith(".adf")]
    single_names = [fn for fn in unique_names if not resolved_by_name[fn].lower().endswith(".adf")]
    print(
        f"  Unique streamed audio files: {len(unique_names)} "
        f"({len(adf_names)} .adf radio stations, {len(single_names)} .mp3/.wav tracks)."
    )

    if args.check_only:
        core_entries, cuts_entries = collect_vfs_entries(
            args.retail_dir, OVERLAY_DIR, None, audio_lookup, read_contents=False
        )
        collisions = {fn: out_stems[fn] for fn in unique_names if out_stems[fn] != os.path.splitext(fn)[0]}
        print(f"  Disambiguated audio stems: {collisions}")
        print(f"  VFS check OK: {len(core_entries)} core files, {len(cuts_entries)} cuts files.")
        return 0

    os.makedirs(CHUNKS_DIR, exist_ok=True)
    os.makedirs(AUDIO_OUT_DIR, exist_ok=True)

    print("[2/5] Decoding/segmenting .adf radio stations & probing non-radio durations...")
    durations_by_name: dict[str, int] = {}
    audio_streams_manifest: dict[str, dict] = {}
    expected_audio_files: set[str] = set()
    written_count = 0
    cached_count = 0

    # Process .adf stations first (decodes XOR 0x22, probes full duration, segments losslessly)
    with ThreadPoolExecutor(max_workers=max(1, min(args.workers, 4))) as pool:
        adf_futures = {
            pool.submit(
                process_adf_station,
                fn_lower,
                resolved_by_name[fn_lower],
                out_stems[fn_lower],
                args.force_audio,
            ): fn_lower
            for fn_lower in adf_names
        }
        for fut in as_completed(adf_futures):
            orig_name, info, produced_files, was_cached = fut.result()
            durations_by_name[orig_name] = info["durationMs"]
            audio_streams_manifest[orig_name] = info
            expected_audio_files.update(produced_files)
            if was_cached:
                cached_count += 1
            else:
                written_count += 1

    # Probe single-file tracks (.mp3 / .wav)
    with ThreadPoolExecutor(max_workers=max(1, args.workers)) as pool:
        probe_futures = {
            pool.submit(probe_duration_ms, resolved_by_name[fn_lower]): fn_lower
            for fn_lower in single_names
        }
        for fut in as_completed(probe_futures):
            fn_lower = probe_futures[fut]
            durations_by_name[fn_lower] = fut.result()

    print("[3/5] Transcoding/copying .mp3 and .wav streamed audio tracks...")
    with ThreadPoolExecutor(max_workers=max(1, args.workers)) as pool:
        single_futures = [
            pool.submit(
                process_single_track,
                fn_lower,
                resolved_by_name[fn_lower],
                out_stems[fn_lower],
                durations_by_name[fn_lower],
                args.force_audio,
            )
            for fn_lower in single_names
        ]
        for fut in as_completed(single_futures):
            orig_name, info, produced_files, was_cached = fut.result()
            audio_streams_manifest[orig_name] = info
            expected_audio_files.update(produced_files)
            if was_cached:
                cached_count += 1
            else:
                written_count += 1

    # Remove any orphan files in public/audio/
    for existing in os.listdir(AUDIO_OUT_DIR):
        if existing not in expected_audio_files:
            os.remove(os.path.join(AUDIO_OUT_DIR, existing))

    durations_ms = [durations_by_name[fn] for fn in ordered_filenames]
    if len(durations_ms) != total_streamed_sounds:
        raise RuntimeError(f"sound.cache length mismatch: {len(durations_ms)} != {total_streamed_sounds}")
    sound_cache_bytes = struct.pack(f"<{len(durations_ms)}I", *durations_ms)
    print(f"  Generated audio/sound.cache: {len(sound_cache_bytes)} bytes ({len(durations_ms)} uint32 entries)")

    audio_streams_manifest = {k: audio_streams_manifest[k] for k in sorted(audio_streams_manifest.keys())}
    print(
        f"  Audio streams ready: {len(audio_streams_manifest)} tracks "
        f"({written_count} written, {cached_count} up-to-date, {len(expected_audio_files)} output files)"
    )

    print("[4/5] Collecting and packing core VFS + optional cuts pack in GTAVCPK2 format...")
    core_entries, cuts_entries = collect_vfs_entries(
        args.retail_dir, OVERLAY_DIR, sound_cache_bytes, audio_lookup, read_contents=True
    )
    core_meta = write_packed_chunks_v2("core", core_entries)
    cuts_meta = write_packed_chunks_v2("cuts", cuts_entries) if cuts_entries else None

    manifest = {
        "version": 2,
        "format": "GTAVCPK2",
        "core": core_meta,
        "cuts": cuts_meta,
        "streamedSoundCount": len(durations_ms),
        "soundTable": ordered_filenames,
        "audioStreams": audio_streams_manifest,
    }
    manifest_path = os.path.join(CHUNKS_DIR, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as mf:
        json.dump(manifest, mf, indent=2)
    print(f"  Wrote manifest: {manifest_path}")

    print("[5/5] Verifying all generated files in public/chunks/ and public/audio/...")
    checked, max_seen = verify_output_sizes()
    print(
        f"  Verified {checked} files. Largest file: {max_seen:,} bytes "
        f"({max_seen / (1024 * 1024):.2f} MiB) < 19.00 MiB [OK]"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
