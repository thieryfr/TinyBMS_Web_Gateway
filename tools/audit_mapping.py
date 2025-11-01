#!/usr/bin/env python3
"""Audit TinyBMS ↔ Victron mapping coverage.

This helper consumes ``docs/TinyBMS_CAN_BMS_mapping.json`` and extracts:
- the TinyBMS register IDs required by the mapping;
- the CAN PGNs referenced by the mapping.

It then cross-checks them against the firmware sources to highlight coverage gaps.
The script prints a markdown report to STDOUT so it can be redirected into
``docs/mapping_audit.md``.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set, Tuple

REPO_ROOT = Path(__file__).resolve().parents[1]
MAPPING_JSON = REPO_ROOT / "docs" / "TinyBMS_CAN_BMS_mapping.json"
UART_PROTOCOL_C = REPO_ROOT / "main" / "uart_bms" / "uart_bms_protocol.c"
CONVERSION_TABLE_C = REPO_ROOT / "main" / "can_publisher" / "conversion_table.c"


@dataclass
class MappingField:
    can_id: str
    victron_field: str
    tiny_reg: Optional[int]
    mapping_type: Optional[str]
    scale: Optional[float]
    unit: Optional[str]


@dataclass
class RegisterInfo:
    address: int
    name: str


@dataclass
class ChannelInfo:
    pgn_value: int
    description: str


def _normalise_tiny_reg(value) -> Optional[int]:
    if value is None:
        return None
    if isinstance(value, (int, float)):
        return int(value)
    if isinstance(value, str):
        try:
            if value.strip().startswith("0x"):
                return int(value.strip(), 16)
            return int(float(value))
        except ValueError:
            return None
    return None


def load_mapping_fields() -> List[MappingField]:
    mapping = json.loads(MAPPING_JSON.read_text(encoding="utf-8"))
    fields: List[MappingField] = []
    for can_id, payload in mapping.get("bms_can_mapping", {}).items():
        for field in payload.get("fields", []):
            fields.append(
                MappingField(
                    can_id=can_id,
                    victron_field=str(field.get("victron_field", "")).strip() or "(unknown)",
                    tiny_reg=_normalise_tiny_reg(field.get("tiny_reg")),
                    mapping_type=(field.get("mapping_type") or "").strip() or None,
                    scale=field.get("scale"),
                    unit=(field.get("unit") or "").strip() or None,
                )
            )
    return fields


def parse_uart_registers() -> Dict[int, RegisterInfo]:
    text = UART_PROTOCOL_C.read_text(encoding="utf-8")

    register_pattern = re.compile(
        r"\{\s*"  # opening brace
        r"\.id = [^,]+,\s*"
        r"\.address = 0x([0-9A-Fa-f]+)U?,\s*"
        r"\.word_count = (\d+),"  # capture the number of words to expand
        r"(?P<body>.*?)"
        r"\},",
        re.DOTALL,
    )
    name_pattern = re.compile(r"\.name = \"([^\"]+)\"")

    registers: Dict[int, RegisterInfo] = {}
    for match in register_pattern.finditer(text):
        base_address = int(match.group(1), 16)
        word_count = int(match.group(2))
        body = match.group("body")
        name_match = name_pattern.search(body)
        name = name_match.group(1) if name_match else f"Register 0x{base_address:04X}"
        for offset in range(word_count):
            address = base_address + offset
            registers[address] = RegisterInfo(address=address, name=name)

    return registers


def parse_uart_poll_addresses() -> Set[int]:
    text = UART_PROTOCOL_C.read_text(encoding="utf-8")
    array_pattern = re.compile(
        r"g_uart_bms_poll_addresses\s*\[[^\]]*\]\s*=\s*\{(?P<body>[^}]+)\};",
        re.DOTALL,
    )
    match = array_pattern.search(text)
    if not match:
        return set()
    body = match.group("body")
    addresses: Set[int] = set()
    for token in body.split(','):
        token = token.strip()
        if not token:
            continue
        try:
            if token.lower().startswith("0x"):
                addresses.add(int(token, 16))
            else:
                addresses.add(int(token))
        except ValueError:
            continue
    return addresses


def parse_can_channels() -> List[ChannelInfo]:
    text = CONVERSION_TABLE_C.read_text(encoding="utf-8")

    define_pattern = re.compile(r"#define\s+(VICTRON_PGN_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)U")
    defines: Dict[str, int] = {}
    for name, value in define_pattern.findall(text):
        defines[name] = int(value, 16)

    channel_pattern = re.compile(
        r"\{\s*\n\s*\.pgn = (VICTRON_PGN_[A-Z0-9_]+),\s*\n"  # capture PGN define name
        r"\s*\.can_id = [^,]+,\s*\n"
        r"\s*\.dlc = (\d+),\s*\n"
        r"\s*\.fill_fn = ([^,]+),\s*\n"
        r"\s*\.description = \"([^\"]+)\",",
        re.DOTALL,
    )

    channels: List[ChannelInfo] = []
    for match in channel_pattern.finditer(text):
        pgn_define = match.group(1)
        description = match.group(4)
        pgn_value = defines.get(pgn_define)
        if pgn_value is None:
            continue
        channels.append(ChannelInfo(pgn_value=pgn_value, description=description))

    return channels


def build_register_report(fields: Iterable[MappingField]) -> Tuple[List[str], Set[int]]:
    required_registers: Dict[int, Set[str]] = {}
    for field in fields:
        if field.tiny_reg is None:
            continue
        required_registers.setdefault(field.tiny_reg, set()).add(field.victron_field)

    uart_registers = parse_uart_registers()
    uart_poll_set = parse_uart_poll_addresses()

    lines: List[str] = []
    lines.append("### Couverture des registres TinyBMS requis\n")
    lines.append("| Registre | Champs concernés | Statut firmware | Commentaire |")
    lines.append("| --- | --- | --- | --- |")

    missing_registers: Set[int] = set()
    for reg in sorted(required_registers):
        fields = ", ".join(sorted(required_registers[reg]))
        if reg in uart_registers:
            status = "Pris en charge"
            comment = uart_registers[reg].name
        elif reg in uart_poll_set:
            status = "Interrogé (sans métadonnées)"
            comment = "Présent dans la liste de poll, pas de décodage dédié"
        else:
            status = "Manquant"
            comment = "Non lu par la pile UART"
            missing_registers.add(reg)
        lines.append(f"| {reg} | {fields} | {status} | {comment} |")

    return lines, missing_registers


def build_can_report(fields: Iterable[MappingField]) -> Tuple[List[str], Set[str]]:
    required_can_ids: Set[int] = set()
    for field in fields:
        try:
            required_can_ids.add(int(field.can_id, 16))
        except ValueError:
            continue

    channels = parse_can_channels()
    available_ids = {c.pgn_value: c for c in channels}

    lines: List[str] = []
    lines.append("### Couverture des trames CAN Victron\n")
    lines.append("| CAN ID (PGN) | Champs (comptés) | Statut firmware | Commentaire |")
    lines.append("| --- | --- | --- | --- |")

    missing_can_ids: Set[str] = set()
    for can_id in sorted(required_can_ids):
        fields_count = sum(1 for field in fields if int(field.can_id, 16) == can_id)
        if can_id in available_ids:
            channel = available_ids[can_id]
            status = "Publié"
            comment = channel.description
        else:
            status = "Manquant"
            comment = "Aucune entrée dans g_can_publisher_channels"
            missing_can_ids.add(f"0x{can_id:03X}")
        lines.append(f"| 0x{can_id:03X} | {fields_count} | {status} | {comment} |")

    return lines, missing_can_ids


def summarise_mapping(fields: Iterable[MappingField]) -> List[str]:
    total_fields = len(list(fields))
    fields = list(fields)
    regs = {field.tiny_reg for field in fields if field.tiny_reg is not None}
    can_ids = {field.can_id for field in fields}
    compute_fields = [f for f in fields if (f.mapping_type or "").lower() == "compute"]
    return [
        "## Synthèse de la matrice fournie\n",
        f"- Champs décrits : **{total_fields}**",
        f"- Registres TinyBMS référencés : **{len(regs)}**",
        f"- CAN ID Victron distincts : **{len(can_ids)}**",
        f"- Champs nécessitant un calcul : **{len(compute_fields)}**",
        "",
    ]


def main() -> None:
    fields = load_mapping_fields()
    summary_lines = summarise_mapping(fields)
    reg_lines, missing_regs = build_register_report(fields)
    can_lines, missing_can = build_can_report(fields)

    output: List[str] = []
    output.append("# Audit automatisé TinyBMS ↔ Victron\n")
    output.extend(summary_lines)
    output.append("## Vérification automatique\n")
    output.extend(reg_lines)
    output.append("")
    output.extend(can_lines)

    if missing_regs:
        output.append("")
        output.append("### Registres manquants à implémenter\n")
        output.append(
            "- "
            + ", ".join(f"Reg {reg}" for reg in sorted(missing_regs))
            + " : absents des métadonnées et de la lecture UART."
        )
    else:
        output.append("")
        output.append("### Registres manquants à implémenter\n")
        output.append("- Aucun registre requis n'est manquant dans la pile UART.")

    if missing_can:
        output.append("")
        output.append("### CAN ID manquants\n")
        output.append(
            "- " + ", ".join(sorted(missing_can)) + " : aucune trame publiée pour ces identifiants."
        )
    else:
        output.append("")
        output.append("### CAN ID manquants\n")
        output.append("- Tous les CAN ID requis sont publiés par le firmware.")

    output.append("")
    output.append(
        "_Rapport généré automatiquement par `tools/audit_mapping.py` à partir de"
        " `docs/TinyBMS_CAN_BMS_mapping.json`._"
    )

    print("\n".join(output))


if __name__ == "__main__":
    main()
