#!/usr/bin/env python3
"""One-shot ingestion script: indexes the Delve codebase into OpenViking.

Maps project directories to viking:// URIs:
  src/game/terrain/  → viking://resources/delve/terrain/
  src/game/render/   → viking://resources/delve/actor/
  src/shaders/       → viking://resources/delve/shader/
  src/engine/        → viking://resources/delve/engine/
  src/test/          → viking://resources/delve/test/
  src/game/*.{h,cpp} → viking://resources/delve/game/

Also seeds KEYWORD_SYNONYMS as an OpenViking SKILL for agent knowledge.
"""

import json
import os
import sys
from pathlib import Path

try:
    from openviking import SyncHTTPClient
except ImportError:
    print("ERROR: openviking package not installed. Run: pip install -r requirements.txt", file=sys.stderr)
    sys.exit(1)

# Project root is 4 levels up from this script
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent.parent.parent

DIRECTORY_MAP = {
    "src/game/terrain": "viking://resources/delve/terrain",
    "src/game/render": "viking://resources/delve/actor",
    "src/shaders": "viking://resources/delve/shader",
    "src/engine": "viking://resources/delve/engine",
    "src/test": "viking://resources/delve/test",
}

EXTENSIONS = {".h", ".cpp", ".glsl"}

# Subsystem taxonomy from context.ts KEYWORD_SYNONYMS (seeded as a SKILL)
SUBSYSTEM_SKILL_PATH = SCRIPT_DIR / "_subsystem_skill.md"
SUBSYSTEM_SKILL = """# Delve Subsystem Taxonomy

Canonical subsystems: terrain, actor, shader, engine

## Keyword → Subsystem Mapping
- player/character/humanoid/npc/creature/figure → actor
- walk/run/move/locomotion/gait/stride/step → actor + animation
- limb/joint/arm/leg/torso/body/head/spine/shoulder/hip/knee/elbow → actor
- animation/animate/skeleton/bone/keyframe/rig/pose/ik → animation (→ actor)
- render/draw → actor (render subsystem)
- terrain/noise/palette/hex/contour/lava/mesh/elevation/plateau → terrain
- engine/camera/input/gpu/shader/test → engine (or shader for shader keyword)

## Directory Layout
- src/game/terrain/ — Terrain generation pipeline
- src/game/render/ — Actor/rig rendering
- src/shaders/ — GLSL shaders (SPIR-V compiled)
- src/engine/ — Reusable engine framework
- src/test/ — Test infrastructure
- src/game/ — Top-level game files (topo_game, config, joint_mapping)
"""


def should_index(path: Path) -> bool:
    """Check if a file should be indexed based on extension."""
    return any(path.name.endswith(ext) for ext in EXTENSIONS)


def ingest_directory(client: SyncHTTPClient, src_dir: str, viking_base: str) -> int:
    """Recursively index files from src_dir into viking_base URI."""
    full_dir = PROJECT_ROOT / src_dir
    if not full_dir.is_dir():
        print(f"  SKIP {src_dir} (not found)")
        return 0

    count = 0
    for root, _dirs, files in os.walk(full_dir):
        for fname in sorted(files):
            fpath = Path(root) / fname
            if not should_index(fpath):
                continue
            rel = fpath.relative_to(full_dir)
            target_uri = f"{viking_base}/{rel}"
            client.add_resource(path=str(fpath), to=target_uri, wait=True, timeout=60.0)
            count += 1
            print(f"  + {target_uri}")

    return count


def ingest_game_toplevel(client: SyncHTTPClient) -> int:
    """Index top-level src/game/ files (not subdirectories)."""
    game_dir = PROJECT_ROOT / "src" / "game"
    if not game_dir.is_dir():
        return 0

    count = 0
    for fpath in sorted(game_dir.iterdir()):
        if fpath.is_file() and should_index(fpath):
            rel = fpath.relative_to(game_dir)
            target_uri = f"viking://resources/delve/game/{rel}"
            client.add_resource(path=str(fpath), to=target_uri, wait=True, timeout=60.0)
            count += 1
            print(f"  + {target_uri}")

    return count


def write_directory_map() -> None:
    """Write DIRECTORY_MAP to .pi/viking_directory_map.json as the single source of truth.

    Includes the top-level src/game/ → game mapping that ingest_game_toplevel handles.
    """
    pi_dir = PROJECT_ROOT / ".pi"
    pi_dir.mkdir(exist_ok=True)
    full_map = {**DIRECTORY_MAP, "src/game": "viking://resources/delve/game"}
    out_path = pi_dir / "viking_directory_map.json"
    out_path.write_text(json.dumps(full_map, indent=2) + "\n")
    print(f"Wrote directory map to {out_path.relative_to(PROJECT_ROOT)}\n")


def main() -> None:
    # Write the directory map before connecting — it's useful even if Viking is down
    write_directory_map()

    print("Connecting to OpenViking...")
    client = SyncHTTPClient(url="http://127.0.0.1:1933")
    client.initialize()
    client.is_healthy()
    print("Connected.\n")

    total = 0

    # Index mapped directories
    for src_dir, viking_uri in DIRECTORY_MAP.items():
        print(f"Indexing {src_dir} → {viking_uri}/")
        count = ingest_directory(client, src_dir, viking_uri)
        total += count
        print(f"  ({count} files)\n")

    # Index top-level game files
    print("Indexing src/game/ (top-level) → viking://resources/delve/game/")
    count = ingest_game_toplevel(client)
    total += count
    print(f"  ({count} files)\n")

    # Seed subsystem taxonomy as a SKILL
    print("Seeding subsystem taxonomy SKILL...")
    SUBSYSTEM_SKILL_PATH.write_text(SUBSYSTEM_SKILL)
    client.add_resource(
        path=str(SUBSYSTEM_SKILL_PATH),
        to="viking://agent/skills/delve-subsystems",
        wait=True,
    )
    SUBSYSTEM_SKILL_PATH.unlink(missing_ok=True)
    print("  + viking://agent/skills/delve-subsystems\n")

    print(f"Done. Indexed {total} files + 1 skill.")


if __name__ == "__main__":
    main()
