#!/usr/bin/env python3
"""Regenerate musiques/playlist.json from the audio files in that directory."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MUSIC_DIRECTORY = ROOT / "musiques"
PLAYLIST = MUSIC_DIRECTORY / "playlist.json"
SUPPORTED_EXTENSIONS = {".mp3", ".wav", ".ogg", ".m4a", ".aac", ".flac"}


def main() -> None:
    tracks = sorted(
        (
            path.relative_to(MUSIC_DIRECTORY).as_posix()
            for path in MUSIC_DIRECTORY.rglob("*")
            if path.is_file() and path.suffix.lower() in SUPPORTED_EXTENSIONS
        ),
        key=str.casefold,
    )
    PLAYLIST.write_text(json.dumps(tracks, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Playlist synchronisée : {len(tracks)} musique(s).")


if __name__ == "__main__":
    main()
