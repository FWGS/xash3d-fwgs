#!/usr/bin/env python3
"""Compare a captured WAV against a reference without assuming its start time."""

from __future__ import annotations

import argparse
import json
import wave
from pathlib import Path

import numpy as np


def read_wav(path: Path) -> tuple[np.ndarray, int]:
	with wave.open(str(path), "rb") as wav:
		if wav.getsampwidth() != 2 or wav.getnchannels() != 2:
			raise ValueError(f"{path}: expected stereo PCM16")
		return np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2").reshape(-1, 2), wav.getframerate()


def resample(samples: np.ndarray, source_rate: int, target_rate: int) -> np.ndarray:
	if source_rate == target_rate:
		return samples.astype(np.float64)
	length = int(round(len(samples) * target_rate / source_rate))
	old = np.arange(len(samples), dtype=np.float64)
	new = np.arange(length, dtype=np.float64) * source_rate / target_rate
	return np.column_stack([np.interp(new, old, samples[:, c]) for c in range(2)])


def best_offset(reference: np.ndarray, candidate: np.ndarray) -> int:
	ref = reference.mean(axis=1)
	cand = candidate.mean(axis=1)
	step = 8
	r = ref[::step] - ref[::step].mean()
	c = cand[::step] - cand[::step].mean()
	if len(c) < len(r):
		raise ValueError("candidate is shorter than reference")
	fft_size = 1 << (len(c) + len(r) - 2).bit_length()
	# Convolution with a reversed reference gives every valid sliding dot
	# product without the quadratic memory/time cost of np.correlate.
	correlation = np.fft.irfft(
		np.fft.rfft(c, fft_size) * np.fft.rfft(r[::-1], fft_size), fft_size
	)[len(r) - 1:len(c)]
	csq = np.concatenate(([0.0], np.cumsum(c * c)))
	energy = csq[len(r):] - csq[:-len(r)]
	denom = np.sqrt(np.maximum(energy * np.dot(r, r), 1e-30))
	coarse = int(np.argmax(correlation / denom)) * step
	lo = max(0, coarse - step * 4)
	hi = min(len(cand) - len(ref), coarse + step * 4)
	best = (-1.0, coarse)
	r = ref
	for offset in range(lo, hi + 1):
		x = cand[offset:offset + len(ref)]
		x = x - x.mean()
		y = r
		norm = np.linalg.norm(x) * np.linalg.norm(y)
		value = float(np.dot(x, y) / norm) if norm else 0.0
		if value > best[0]:
			best = (value, offset)
	return best[1]


def db(value: float) -> float:
	return float(20.0 * np.log10(max(value, 1e-12)))


def metrics(reference: np.ndarray, candidate: np.ndarray, rate: int) -> dict[str, object]:
	offset = best_offset(reference, candidate)
	aligned = candidate[offset:offset + len(reference)]
	ref = reference.astype(np.float64) / 32768.0
	got = aligned.astype(np.float64) / 32768.0
	ref_mono = ref.mean(axis=1)
	got_mono = got.mean(axis=1)
	error = got - ref
	fft_size = 1 << (len(ref_mono) - 1).bit_length()
	ref_fft = np.abs(np.fft.rfft(ref_mono, fft_size)) ** 2
	got_fft = np.abs(np.fft.rfft(got_mono, fft_size)) ** 2
	freq = np.fft.rfftfreq(fft_size, 1.0 / rate)
	def band(lo: float, hi: float) -> float:
		mask = (freq >= lo) & (freq < hi)
		return db(float((got_fft[mask].sum() + 1e-18) / (ref_fft[mask].sum() + 1e-18)))
	ref_rms = float(np.sqrt(np.mean(ref_mono ** 2)))
	got_rms = float(np.sqrt(np.mean(got_mono ** 2)))
	ref_peak = float(np.max(np.abs(ref)))
	got_peak = float(np.max(np.abs(got)))
	noise = float(np.sqrt(np.mean((got_mono - ref_mono) ** 2)))
	return {
		"offset_samples": offset,
		"offset_seconds": offset / rate,
		"reference_frames": len(reference),
		"candidate_frames": len(candidate),
		"aligned_frames": len(aligned),
		"reference_rate": rate,
		"candidate_rate_after_resample": rate,
		"duration_seconds": len(aligned) / rate,
		"rms_dbfs_reference": db(ref_rms),
		"rms_dbfs_candidate": db(got_rms),
		"rms_error_db": db(got_rms / ref_rms),
		"peak_dbfs_reference": db(ref_peak),
		"peak_dbfs_candidate": db(got_peak),
		"peak_error_db": db(got_peak / ref_peak),
		"dc_error": float(got.mean() - ref.mean()),
		"channel_mismatch_rms": float(np.sqrt(np.mean((got[:, 0] - got[:, 1]) ** 2))),
		"correlation": float(np.corrcoef(ref_mono, got_mono)[0, 1]),
		"sample_difference_snr_db": db(float(np.sqrt(np.mean(ref_mono ** 2))) / max(noise, 1e-12)),
		"spectral_error_db_0_8khz": band(0, 8000),
		"spectral_error_db_8_20khz": band(8000, 20000),
		"clipped_samples": int(np.count_nonzero(np.abs(aligned) >= 32767)),
		"pass_shape": len(aligned) == len(reference),
		"pass_acceptance": bool(
			len(aligned) == len(reference)
			and abs(db(got_rms / ref_rms)) <= 0.1
			and abs(db(got_peak / ref_peak)) <= 0.1
			and abs(float(got.mean() - ref.mean())) <= 1.0 / 32768.0
			and float(np.sqrt(np.mean((got[:, 0] - got[:, 1]) ** 2))) <= 1.0 / 32768.0
			and float(np.corrcoef(ref_mono, got_mono)[0, 1]) >= 0.999
			and band(8000, 20000) <= 0.1
			and not np.any(np.abs(aligned) >= 32767)
		),
	}


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("reference", type=Path)
	parser.add_argument("candidate", type=Path)
	parser.add_argument("--json", action="store_true")
	parser.add_argument("--strict", action="store_true", help="exit nonzero unless acceptance criteria pass")
	args = parser.parse_args()
	reference, reference_rate = read_wav(args.reference)
	candidate, candidate_rate = read_wav(args.candidate)
	if reference_rate != 48000:
		raise SystemExit(f"reference must be 48000 Hz, got {reference_rate}")
	result = metrics(reference, resample(candidate, candidate_rate, reference_rate), reference_rate)
	result["candidate_rate_original"] = candidate_rate
	result["candidate_duration_original_seconds"] = len(candidate) / candidate_rate
	print(json.dumps(result, indent=2, sort_keys=True) if args.json else "\n".join(f"{k}: {v}" for k, v in result.items()))
	return 0 if result["pass_acceptance"] or not args.strict else 1


if __name__ == "__main__":
	raise SystemExit(main())
