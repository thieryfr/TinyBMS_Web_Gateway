#!/usr/bin/env python3
"""UART simulator for TinyBMS register frames.

The script loads reference vectors from ``test/reference/uart_frames.json`` and emits
binary frames that mimic the TinyBMS ``0xAA 0x09`` response. It can either dump the
frames as hexadecimal strings (default), write raw bytes to a file or replay them on a
serial port using :mod:`pyserial`.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Dict, Iterable, List, MutableMapping, Sequence

PREAMBLE = 0xAA
OPCODE_READ = 0x09
CRC_POLY = 0xA001


def _crc16(data: Sequence[int]) -> int:
    """Return Modbus CRC16 for *data*."""

    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ CRC_POLY
            else:
                crc >>= 1
    return crc & 0xFFFF


class ReferenceFrames:
    """Parses the JSON reference file and exposes ordered frame definitions."""

    def __init__(self, reference_path: Path) -> None:
        with reference_path.open("r", encoding="utf-8") as fp:
            self._payload = json.load(fp)

        metadata = self._payload.get("metadata", {})
        addresses: List[str] = metadata.get("poll_addresses", [])
        if not addresses:
            raise ValueError("reference file must list poll_addresses")

        self.address_order: List[str] = addresses
        self.frames = self._payload.get("frames", [])
        if not self.frames:
            raise ValueError("reference file does not declare frames")

    def build_register_map(self, frame_def: MutableMapping[str, object],
                            base_maps: Dict[str, Dict[str, int]]) -> Dict[str, int]:
        """Combine the base frame (if any) and declared registers into a map."""

        registers: Dict[str, int]
        base_name = frame_def.get("base_frame")
        if base_name:
            if base_name not in base_maps:
                raise KeyError(f"base frame '{base_name}' not loaded yet")
            registers = dict(base_maps[base_name])
        else:
            registers = {}

        explicit = frame_def.get("registers") or {}
        for address_str, payload in explicit.items():
            raw_value = payload.get("value")
            registers[address_str] = _parse_register_value(raw_value)

        missing = [addr for addr in self.address_order if addr not in registers]
        if missing:
            raise ValueError(
                f"frame '{frame_def.get('id')}' is missing registers: {', '.join(missing)}"
            )

        return registers


def _parse_register_value(raw_value) -> int:
    if isinstance(raw_value, str):
        return int(raw_value, 0)
    if isinstance(raw_value, int):
        return raw_value
    raise TypeError(f"Unsupported register value type: {type(raw_value)!r}")


def _pack_frame(address_order: Sequence[str], registers: Dict[str, int]) -> bytearray:
    payload = bytearray()
    for address in address_order:
        value = registers[address]
        if not 0 <= value <= 0xFFFF:
            raise ValueError(f"Register {address} out of range: 0x{value:04X}")
        payload.append(value & 0xFF)
        payload.append((value >> 8) & 0xFF)

    frame = bytearray([PREAMBLE, OPCODE_READ, len(payload)])
    frame.extend(payload)
    crc = _crc16(frame)
    frame.append(crc & 0xFF)
    frame.append((crc >> 8) & 0xFF)
    return frame


def _apply_mutations(frame: bytearray,
                     mutations: Iterable[MutableMapping[str, object]],
                     address_order: Sequence[str],
                     registers: Dict[str, int]) -> bytearray:
    mutated = bytearray(frame)
    for mutation in mutations:
        mtype = mutation.get("type")
        if mtype == "crc_flip":
            index = int(mutation.get("byte", -1))
            if abs(index) > len(mutated):
                raise ValueError("crc_flip byte index outside frame")
            mutated[index] ^= 0xFF
        elif mtype == "truncate_after_address":
            address = mutation.get("address")
            if address not in address_order:
                raise KeyError(f"unknown address {address}")
            word_index = address_order.index(address) + 1
            payload_len = word_index * 2
            keep_len = 3 + payload_len  # header + payload
            mutated = mutated[:keep_len]
        else:
            raise ValueError(f"Unsupported mutation type: {mtype}")
    return mutated


def iter_frames(ref: ReferenceFrames,
                scenario_filter: Sequence[str] | None = None) -> Iterable[Dict[str, object]]:
    """Yield dictionaries with ``id`` and ``frame`` fields."""

    selected = set(scenario_filter) if scenario_filter else None
    base_maps: Dict[str, Dict[str, int]] = {}
    for frame_def in ref.frames:
        frame_id = frame_def.get("id")
        if not frame_id:
            raise ValueError("frame without id")

        registers = ref.build_register_map(frame_def, base_maps)
        base_maps[frame_id] = registers

        frame = _pack_frame(ref.address_order, registers)
        mutations = frame_def.get("mutations")
        if mutations:
            frame = _apply_mutations(frame,
                                     mutations,
                                     ref.address_order,
                                     registers)

        if selected is None or frame_id in selected:
            yield {
                "id": frame_id,
                "bytes": bytes(frame),
                "description": frame_def.get("description", ""),
            }


class OutputTarget:
    def send(self, payload: bytes, frame_id: str, description: str) -> None:
        raise NotImplementedError

    def close(self) -> None:
        return None


class StdoutTarget(OutputTarget):
    def __init__(self, binary: bool) -> None:
        self.binary = binary

    def send(self, payload: bytes, frame_id: str, description: str) -> None:
        if self.binary:
            sys.stdout.buffer.write(payload)
        else:
            hex_payload = payload.hex(" ")
            sys.stdout.write(f"{frame_id}: {hex_payload}\n")
            if description:
                sys.stdout.write(f"    {description}\n")


class FileTarget(OutputTarget):
    def __init__(self, path: Path) -> None:
        self.path = path
        self._buffer = bytearray()

    def send(self, payload: bytes, frame_id: str, description: str) -> None:  # noqa: D401
        del frame_id, description
        self._buffer.extend(payload)

    def close(self) -> None:
        self.path.write_bytes(self._buffer)


class SerialTarget(OutputTarget):
    def __init__(self, port: str, baud: int) -> None:
        try:
            import serial  # type: ignore
        except ImportError as exc:  # pragma: no cover - optional dependency
            raise SystemExit(
                "pyserial is required for --port usage. Install with 'pip install pyserial'."
            ) from exc

        self._serial = serial.Serial(port, baudrate=baud)

    def send(self, payload: bytes, frame_id: str, description: str) -> None:  # noqa: D401
        del frame_id, description
        self._serial.write(payload)

    def close(self) -> None:
        self._serial.close()


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference",
                        type=Path,
                        default=Path("test/reference/uart_frames.json"),
                        help="Path to the UART reference JSON file")
    parser.add_argument("--scenario",
                        action="append",
                        help="Scenario id to replay (repeatable, default = all)")
    parser.add_argument("--port", help="Optional serial port to stream frames to")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--output", type=Path, help="Write binary frames to a file")
    parser.add_argument("--binary", action="store_true", help="Emit raw bytes on stdout")
    parser.add_argument("--repeat", type=int, default=1, help="Number of iterations per scenario")
    parser.add_argument("--sleep", type=float, default=1.0, help="Delay between frames (seconds)")
    return parser.parse_args(argv)


def _resolve_target(args: argparse.Namespace) -> OutputTarget:
    if args.port:
        return SerialTarget(args.port, args.baud)
    if args.output:
        return FileTarget(args.output)
    return StdoutTarget(args.binary)


def main(argv: Sequence[str] | None = None) -> int:
    args = _parse_args(argv or sys.argv[1:])
    reference = ReferenceFrames(args.reference)
    target = _resolve_target(args)

    try:
        scenarios = list(iter_frames(reference, args.scenario))
        if not scenarios:
            raise SystemExit("No frames matched the provided filter")

        for _ in range(max(args.repeat, 1)):
            for scenario in scenarios:
                target.send(scenario["bytes"],
                            scenario["id"],
                            scenario.get("description", ""))
                if args.sleep:
                    time.sleep(args.sleep)
    finally:
        target.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
