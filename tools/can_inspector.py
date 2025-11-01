#!/usr/bin/env python3
"""Inspect CAN capture logs against TinyBMS reference expectations."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional


@dataclass
class Expectation:
    scenario_id: str
    pgn: int
    interval_ms: int
    data_hex: str
    expected_fields: str


def load_expectations(path: Path,
                      scenario_filter: Optional[Iterable[str]] = None) -> Dict[int, Expectation]:
    allowed = set(scenario_filter) if scenario_filter else None
    expectations: Dict[int, Expectation] = {}
    with path.open("r", encoding="utf-8") as fp:
        reader = csv.DictReader(fp)
        for row in reader:
            scenario_id = row["scenario_id"].strip()
            if allowed and scenario_id not in allowed:
                continue
            pgn = int(row["pgn_hex"], 16)
            expectations[pgn] = Expectation(
                scenario_id=scenario_id,
                pgn=pgn,
                interval_ms=int(row["interval_ms"]),
                data_hex=row["data_hex"].strip().upper(),
                expected_fields=row["expected_fields"].strip(),
            )
    if allowed and not expectations:
        raise SystemExit("No expectations matched the requested scenario(s)")
    return expectations


@dataclass
class InspectionResult:
    seen: int = 0
    interval_violations: int = 0
    data_mismatches: int = 0
    last_timestamp: Optional[float] = None


class CanLogInspector:
    def __init__(self, expectations: Dict[int, Expectation], tolerance_ms: int) -> None:
        self.expectations = expectations
        self.tolerance_ms = tolerance_ms
        self.results: Dict[int, InspectionResult] = {
            pgn: InspectionResult() for pgn in expectations
        }

    def consume(self, timestamp: float, identifier: int, data_hex: str) -> None:
        expectation = self.expectations.get(identifier)
        if expectation is None:
            return

        result = self.results[identifier]
        result.seen += 1

        # Check payload
        cleaned = data_hex.upper()
        if cleaned != expectation.data_hex:
            result.data_mismatches += 1

        # Check interval
        if result.last_timestamp is not None:
            interval_ms = int((timestamp - result.last_timestamp) * 1000)
            delta = abs(interval_ms - expectation.interval_ms)
            if delta > self.tolerance_ms:
                result.interval_violations += 1
        result.last_timestamp = timestamp

    def summarize(self) -> int:
        failures = 0
        for expectation in self.expectations.values():
            result = self.results[expectation.pgn]
            if result.seen == 0:
                print(f"[FAIL] PGN 0x{expectation.pgn:08X} missing for scenario {expectation.scenario_id}")
                failures += 1
                continue
            if result.data_mismatches:
                print(
                    f"[FAIL] PGN 0x{expectation.pgn:08X} payload mismatch "
                    f"({result.data_mismatches} occurrences)"
                )
                failures += 1
            if result.interval_violations:
                print(
                    f"[FAIL] PGN 0x{expectation.pgn:08X} interval out of tolerance "
                    f"({result.interval_violations} occurrences, expected {expectation.interval_ms} ms)"
                )
                failures += 1
            if result.data_mismatches == 0 and result.interval_violations == 0:
                print(
                    f"[OK] PGN 0x{expectation.pgn:08X} "
                    f"{expectation.expected_fields}"
                )
        return failures


def parse_candump_line(line: str) -> Optional[tuple[float, int, str]]:
    line = line.strip()
    if not line or line.startswith("(") is False:
        return None

    try:
        timestamp_str, rest = line.split(")", maxsplit=1)
        timestamp = float(timestamp_str[1:])
    except ValueError:
        return None

    parts = rest.strip().split()
    if len(parts) < 2:
        return None
    frame = parts[1]
    if "#" not in frame:
        return None
    identifier_str, data_hex = frame.split("#", maxsplit=1)
    try:
        identifier = int(identifier_str, 16)
    except ValueError:
        return None
    return timestamp, identifier, data_hex


def _parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="candump -L log file")
    parser.add_argument("--expected",
                        type=Path,
                        default=Path("test/reference/can_expected_snapshot.csv"),
                        help="CSV file with PGN expectations")
    parser.add_argument("--scenario",
                        action="append",
                        help="Scenario identifier to validate (repeatable)")
    parser.add_argument("--tolerance-ms",
                        type=int,
                        default=100,
                        help="Allowed timing difference in milliseconds")
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = _parse_args(argv)
    expectations = load_expectations(args.expected, args.scenario)
    inspector = CanLogInspector(expectations, args.tolerance_ms)

    with args.log.open("r", encoding="utf-8") as fp:
        for line in fp:
            parsed = parse_candump_line(line)
            if not parsed:
                continue
            timestamp, identifier, data_hex = parsed
            inspector.consume(timestamp, identifier, data_hex)

    failures = inspector.summarize()
    if failures:
        print(f"\nSummary: {failures} failure(s) detected")
        return 1
    print("\nSummary: capture matches expectations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
