#!/usr/bin/env python3
"""Compare a GameCube/Dolphin milestone screenshot against a stored baseline."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from PIL import Image, ImageChops, ImageOps, ImageStat


DEFAULT_MANIFEST = Path(".ai/screenshots/baselines.json")


@dataclass
class Milestone:
    id: str
    label: str
    reference: Path
    candidate: Path
    comparison: Path
    notes: list[str]


def load_manifest(path: Path, root: Path) -> dict[str, Milestone]:
    data = json.loads(path.read_text(encoding="utf-8"))
    milestones: dict[str, Milestone] = {}
    for item in data.get("milestones", []):
        milestone = Milestone(
            id=item["id"],
            label=item["label"],
            reference=root / item["reference"],
            candidate=root / item["candidate"],
            comparison=root / item["comparison"],
            notes=list(item.get("notes", [])),
        )
        milestones[milestone.id] = milestone
    return milestones


def ensure_same_size(reference: Image.Image, candidate: Image.Image) -> tuple[Image.Image, Image.Image]:
    if reference.size == candidate.size:
        return reference, candidate
    candidate = candidate.resize(reference.size, Image.Resampling.LANCZOS)
    return reference, candidate


def build_comparison(reference: Image.Image, candidate: Image.Image) -> tuple[Image.Image, dict[str, Any]]:
    reference, candidate = ensure_same_size(reference, candidate)
    diff = ImageChops.difference(reference, candidate)
    gray = ImageOps.grayscale(diff)
    stat = ImageStat.Stat(gray)
    mean_abs = stat.mean[0]
    extrema = gray.getextrema()
    diff_mask = gray.point(lambda value: 255 if value > 16 else 0)
    changed_pixels = total_changed = int(diff_mask.histogram()[255]) if diff_mask.getbbox() else 0
    total_pixels = gray.width * gray.height
    changed_ratio = total_changed / float(total_pixels) if total_pixels else 0.0

    side_by_side = Image.new("RGBA", (reference.width * 3, reference.height), (0, 0, 0, 255))
    side_by_side.paste(reference.convert("RGBA"), (0, 0))
    side_by_side.paste(candidate.convert("RGBA"), (reference.width, 0))
    side_by_side.paste(ImageOps.colorize(gray, black="black", white="magenta").convert("RGBA"), (reference.width * 2, 0))

    return side_by_side, {
        "mean_abs_diff": round(mean_abs, 3),
        "max_diff": int(extrema[1]),
        "changed_pixels": total_changed,
        "changed_ratio": round(changed_ratio, 6),
        "width": reference.width,
        "height": reference.height,
    }


def evaluate(metrics: dict[str, Any]) -> str:
    changed_ratio = metrics["changed_ratio"]
    mean_abs = metrics["mean_abs_diff"]
    if changed_ratio <= 0.02 and mean_abs <= 8.0:
        return "close"
    if changed_ratio <= 0.08 and mean_abs <= 20.0:
        return "drift"
    return "mismatch"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--milestone", required=True, help="milestone id from .ai/screenshots/baselines.json")
    parser.add_argument("--candidate", type=Path, help="override candidate screenshot path")
    parser.add_argument("--reference", type=Path, help="override reference screenshot path")
    parser.add_argument("--comparison", type=Path, help="override output comparison image path")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--result-json", type=Path, help="optional output path for machine-readable result")
    args = parser.parse_args()

    root = Path.cwd()
    milestones = load_manifest(args.manifest, root)
    if args.milestone not in milestones:
        raise SystemExit(f"unknown milestone: {args.milestone}")

    milestone = milestones[args.milestone]
    reference_path = (root / args.reference) if args.reference else milestone.reference
    candidate_path = (root / args.candidate) if args.candidate else milestone.candidate
    comparison_path = (root / args.comparison) if args.comparison else milestone.comparison

    if not reference_path.is_file():
        raise SystemExit(f"missing reference screenshot: {reference_path}")
    if not candidate_path.is_file():
        raise SystemExit(f"missing candidate screenshot: {candidate_path}")

    reference = Image.open(reference_path).convert("RGB")
    candidate = Image.open(candidate_path).convert("RGB")
    comparison, metrics = build_comparison(reference, candidate)
    verdict = evaluate(metrics)

    comparison_path.parent.mkdir(parents=True, exist_ok=True)
    comparison.save(comparison_path)

    result = {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "milestone": milestone.id,
        "label": milestone.label,
        "reference": str(reference_path.relative_to(root)),
        "candidate": str(candidate_path.relative_to(root)),
        "comparison": str(comparison_path.relative_to(root)),
        "verdict": verdict,
        "metrics": metrics,
        "notes": milestone.notes,
    }

    if args.result_json:
        result_path = root / args.result_json
        result_path.parent.mkdir(parents=True, exist_ok=True)
        result_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
