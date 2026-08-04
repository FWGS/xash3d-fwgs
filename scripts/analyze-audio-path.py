#!/usr/bin/env python3
"""Analyze native/reference, AI-input, DMA, and final Dolphin audio captures."""

from __future__ import annotations

import argparse
import json
import re
import wave
from pathlib import Path

import numpy as np


def read_wav(path: Path) -> tuple[np.ndarray, int]:
	with wave.open(str(path), "rb") as w:
		if w.getsampwidth() != 2 or w.getnchannels() != 2:
			raise ValueError(f"{path}: expected stereo PCM16")
		x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2)
		return x.astype(np.float64) / 32768.0, w.getframerate()


def resample(x: np.ndarray, source_rate: int, target_rate: int) -> np.ndarray:
	if source_rate == target_rate:
		return x
	n = int(round(len(x) * target_rate / source_rate))
	old = np.arange(len(x), dtype=np.float64)
	new = np.arange(n, dtype=np.float64) * source_rate / target_rate
	return np.column_stack([np.interp(new, old, x[:, c]) for c in range(2)])


def integer_offset(ref: np.ndarray, cand: np.ndarray) -> int:
	r = ref.mean(axis=1)
	c = cand.mean(axis=1)
	step = 8
	r = r[::step] - r[::step].mean()
	c = c[::step] - c[::step].mean()
	n = 1 << (len(r) + len(c) - 2).bit_length()
	cor = np.fft.irfft(np.fft.rfft(c, n) * np.fft.rfft(r[::-1], n), n)[len(r) - 1:len(c)]
	energy = np.concatenate(([0.0], np.cumsum(c * c)))
	den = np.sqrt(np.maximum((energy[len(r):] - energy[:-len(r)]) * np.dot(r, r), 1e-30))
	coarse = int(np.argmax(cor / den)) * step
	lo = max(0, coarse - 32)
	hi = min(len(cand) - len(ref), coarse + 32)
	best = (-1.0, coarse)
	for o in range(lo, hi + 1):
		x = cand[o:o + len(ref)].mean(axis=1)
		y = ref.mean(axis=1)
		x -= x.mean()
		y -= y.mean()
		v = float(np.dot(x, y) / max(np.linalg.norm(x) * np.linalg.norm(y), 1e-30))
		if v > best[0]:
			best = (v, o)
	return best[1]


def fractional_peak(ref: np.ndarray, cand: np.ndarray, offset: int) -> float:
	# Estimate a sub-sample peak from three integer correlation points. This is
	# reported only; null tests remain integer-sample tests.
	x = ref.mean(axis=1) - ref.mean()
	vals = []
	for o in (offset - 1, offset, offset + 1):
		y = cand[o:o + len(ref)].mean(axis=1)
		y -= y.mean()
		vals.append(float(np.dot(x, y) / max(np.linalg.norm(x) * np.linalg.norm(y), 1e-30)))
	den = vals[0] - 2.0 * vals[1] + vals[2]
	delta = 0.0 if abs(den) < 1e-12 else 0.5 * (vals[0] - vals[2]) / den
	return float(offset + np.clip(delta, -0.5, 0.5))


def null_metrics(ref: np.ndarray, cand: np.ndarray, offset: int, rate: int) -> dict[str, float | int]:
	x = ref
	y = cand[offset:offset + len(ref)]
	if len(y) != len(x):
		raise ValueError("candidate does not contain a complete aligned reference")
	d = y - x
	mono = d.mean(axis=1)
	ref_mono = x.mean(axis=1)
	f = np.fft.rfftfreq(len(mono), 1.0 / rate)
	dp = np.abs(np.fft.rfft(mono * np.hanning(len(mono)))) ** 2
	rp = np.abs(np.fft.rfft(ref_mono * np.hanning(len(ref_mono)))) ** 2
	def db(v: float) -> float:
		return float(20.0 * np.log10(max(v, 1e-15)))
	def band(lo: float, hi: float) -> float:
		m = (f >= lo) & (f < hi)
		return db(float(np.sqrt((dp[m].sum() + 1e-30) / (rp[m].sum() + 1e-30))))
	rms = float(np.sqrt(np.mean(d * d)))
	ref_rms = float(np.sqrt(np.mean(ref_mono * ref_mono)))
	crest = float(np.max(np.abs(d)) / max(rms, 1e-15))
	return {
		"offset_samples": offset,
		"frames": len(x),
		"residual_rms_dbfs": db(rms),
		"residual_peak_dbfs": db(float(np.max(np.abs(d)))),
		"snr_db": db(ref_rms / max(rms, 1e-15)),
		"dc_error": float(d.mean()),
		"correlation": float(np.corrcoef(ref_mono, y.mean(axis=1))[0, 1]),
		"spectral_error_db_0_8khz": band(0, 8000),
		"spectral_error_db_8_20khz": band(8000, 20000),
		"crest_factor": crest,
		"impulsive": bool(crest > 12.0),
	}


def parse_telemetry(path: Path) -> list[dict[str, int]]:
	pat = re.compile(
		r"second=(\d+).*?sound=(-?\d+).*?samplepos=(-?\d+).*?raw=(\d+)/(\d+).*?"
		r"chunks=(\d+).*?callbacks=(\d+).*?addfail=(\d+).*?asnd=(\d+)"
	)
	rows = []
	for line in path.read_text(errors="replace").splitlines():
		m = pat.search(line)
		if m:
			v = list(map(int, m.groups()))
			rows.append(dict(zip(("second", "sound", "samplepos", "raw_lead", "raw_capacity",
				"chunks", "callbacks", "addfail", "asnd"), v)))
	return rows


def anomalies(ref: np.ndarray, cand: np.ndarray, offset: int, telemetry: list[dict[str, int]]) -> list[dict[str, object]]:
	d = (cand[offset:offset + len(ref)] - ref).mean(axis=1)
	rows = []
	for frame in range(0, len(d), 1024):
		block = d[frame:frame + 1024]
		rms = float(np.sqrt(np.mean(block * block)))
		if rms < 0.01:
			continue
		second = min(10, frame // 48000 + 1)
		near = min(telemetry, key=lambda x: abs(x["second"] - second), default={})
		rows.append({
			"frame": frame,
			"callback_index": frame // 1024,
			"callback_bytes": 4096,
			"ring_frame": frame % 8192,
			"residual_rms": rms,
			"residual_peak": float(np.max(np.abs(block))),
			"waveform_before": np.rint(cand[offset + max(0, frame - 8):offset + frame].mean(axis=1) * 32768).astype(int).tolist(),
			"waveform_after": np.rint(cand[offset + frame + 1024:offset + frame + 1032].mean(axis=1) * 32768).astype(int).tolist(),
			"telemetry": near,
		})
	return rows


def write_spectrogram(ref: np.ndarray, cand: np.ndarray, offset: int, path: Path) -> None:
	try:
		import matplotlib.pyplot as plt
	except ImportError:
		return
	d = (cand[offset:offset + len(ref)].mean(axis=1) - ref.mean(axis=1))
	window = np.hanning(2048)
	step = 512
	rows = []
	for i in range(0, len(d) - len(window), step):
		rows.append(np.abs(np.fft.rfft(d[i:i + len(window)] * window)))
	if not rows:
		return
	data = 20.0 * np.log10(np.maximum(np.asarray(rows).T, 1e-8))
	plt.imsave(path, data, origin="lower", cmap="magma",
		vmin=float(np.percentile(data, 5)), vmax=float(np.percentile(data, 99)))


def pcm_variants(ref: np.ndarray, cand: np.ndarray, offset: int) -> dict[str, float]:
	# Captures are already PCM16. These variants prove that no PCM16 rounding
	# choice can account for the observed multi-thousand-count residual.
	y = cand[offset:offset + len(ref)]
	variants = {
		"identity": y,
		"truncate": np.trunc(y * 32768.0) / 32768.0,
		"round_nearest": np.round(y * 32768.0) / 32768.0,
		"ties_to_even": np.rint(y * 32768.0) / 32768.0,
		"asymmetric_signed": np.where(y >= 0, np.floor(y * 32768.0), np.ceil(y * 32768.0)) / 32768.0,
	}
	return {name: float(20.0 * np.log10(np.linalg.norm(ref.mean(1)) /
		max(np.linalg.norm((value - ref).mean(1)), 1e-15))) for name, value in variants.items()}


def main() -> int:
	p = argparse.ArgumentParser()
	p.add_argument("reference", type=Path)
	p.add_argument("ai_input", type=Path)
	p.add_argument("dma", type=Path)
	p.add_argument("final", type=Path)
	p.add_argument("--telemetry", type=Path)
	p.add_argument("--json", type=Path, required=True)
	a = p.parse_args()
	ref, rr = read_wav(a.reference)
	if rr != 48000:
		raise SystemExit("reference must be 48000 Hz")
	loaded = {}
	for name, path in (("ai_input", a.ai_input), ("dma", a.dma), ("final", a.final)):
		x, rate = read_wav(path)
		loaded[name] = resample(x, rate, rr)
	report: dict[str, object] = {"reference_rate": rr, "signals": {}}
	for name, x in loaded.items():
		o = integer_offset(ref, x)
		report["signals"][name] = {
			"frames": len(x),
			"offset_samples_integer": o,
			"offset_samples_subsample_estimate": fractional_peak(ref, x, o),
			"null_native": null_metrics(ref, x, o, rr),
			"pcm_variant_snr_db": pcm_variants(ref, x, o),
		}
	for name in ("dma", "final"):
		o = integer_offset(loaded["ai_input"], loaded[name])
		report["signals"][name]["null_ai_input"] = null_metrics(loaded["ai_input"], loaded[name], o, rr)
	telemetry = parse_telemetry(a.telemetry) if a.telemetry else []
	ai_o = report["signals"]["ai_input"]["offset_samples_integer"]
	report["discontinuities"] = anomalies(ref, loaded["ai_input"], ai_o, telemetry)
	report["telemetry"] = telemetry
	spectrogram = a.json.with_suffix(".spectrogram.png")
	write_spectrogram(ref, loaded["ai_input"], ai_o, spectrogram)
	if spectrogram.exists():
		report["residual_spectrogram"] = str(spectrogram)
	a.json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
	print(json.dumps(report, indent=2, sort_keys=True))
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
