"""Address normalization."""

from __future__ import annotations


def normalize_address(address: str) -> str:
	text = address.strip().lower()
	if not text.startswith("0x"):
		text = f"0x{text}"
	return text
