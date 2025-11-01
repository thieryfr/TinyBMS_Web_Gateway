"""Utilities to normalize and audit the TinyBMS UART↔CAN mapping files.

This script extracts a unified tabular view from the Excel spreadsheet
``docs/UART_CAN_mapping.xlsx`` and the JSON specification
``docs/TinyBMS_CAN_BMS_mapping.json``.  It also performs a few consistency
checks to highlight potential issues before code reviews.

The resulting consolidated data is emitted as ``docs/mapping_normalized.csv``
while a human readable report is stored in ``docs/mapping_audit.md``.
"""

from __future__ import annotations

import csv
import json
import re
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional
from zipfile import ZipFile


REPO_ROOT = Path(__file__).resolve().parents[1]
DOCS_DIR = REPO_ROOT / "docs"


# ---------------------------------------------------------------------------
# Excel parsing helpers


NS = {"main": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}


def _read_xlsx_rows(path: Path) -> List[List[Optional[str]]]:
    """Return the rows from the first worksheet of an XLSX file.

    Only the minimal functionality that we need for this repository is
    implemented so we do not depend on heavy third-party packages.
    """

    with ZipFile(path) as zf:
        shared_strings: List[str] = []
        if "xl/sharedStrings.xml" in zf.namelist():
            root = ET.fromstring(zf.read("xl/sharedStrings.xml"))
            for si in root.findall("main:si", NS):
                text = "".join(t.text or "" for t in si.findall(".//main:t", NS))
                shared_strings.append(text)

        sheet = ET.fromstring(zf.read("xl/worksheets/sheet1.xml"))
        rows: List[List[Optional[str]]] = []

        for row in sheet.findall(".//main:sheetData/main:row", NS):
            values: Dict[int, Optional[str]] = {}
            for cell in row.findall("main:c", NS):
                coord = cell.attrib["r"]
                col_label = "".join(ch for ch in coord if ch.isalpha())
                col_index = 0
                for ch in col_label:
                    col_index = col_index * 26 + ord(ch) - ord("A") + 1

                value: Optional[str] = None
                if (v_elem := cell.find("main:v", NS)) is not None:
                    raw_value = v_elem.text or ""
                    if cell.attrib.get("t") == "s":
                        value = shared_strings[int(raw_value)]
                    else:
                        # Numeric cells are left as strings – the downstream logic
                        # converts them if necessary.
                        value = raw_value
                elif (inline := cell.find("main:is", NS)) is not None:
                    value = "".join(t.text or "" for t in inline.findall(".//main:t", NS))

                if value is not None:
                    values[col_index] = value

            if values:
                max_col = max(values)
                row_values = [values.get(i) for i in range(1, max_col + 1)]
                rows.append(row_values)

    return rows


def _normalize_header(raw_header: Iterable[Optional[str]]) -> List[str]:
    header: List[str] = []
    for cell in raw_header:
        key = (cell or "").strip()
        key = key.replace(" ", "_")
        header.append(key)
    return header


def load_uart_can_mapping(path: Path) -> List[Dict[str, Optional[str]]]:
    rows = _read_xlsx_rows(path)
    if not rows:
        return []

    header = _normalize_header(rows[0])
    normalized: List[Dict[str, Optional[str]]] = []
    carry_columns = {
        "Victron_ID_0x3xx",
        "Victron_Intervalle_typique",
        "Victron_Description",
    }
    last_values: Dict[str, Optional[str]] = {}
    for row in rows[1:]:
        if all(cell in (None, "") for cell in row):
            continue

        record = {header[i]: row[i] if i < len(row) else None for i in range(len(header))}

        mapping_type_value = record.get("Mapping_Type")
        scale_to_can = record.get("Scale_Tiny_To_CAN")
        if (
            not record.get("Victron_ID_0x3xx")
            and isinstance(mapping_type_value, str)
            and mapping_type_value.startswith("0x")
            and isinstance(scale_to_can, str)
            and scale_to_can.lower().startswith("byte")
        ):
            record["Victron_ID_0x3xx"] = mapping_type_value
            if record.get("Compute_Inputs"):
                record["Victron_Description"] = record["Compute_Inputs"]
            record["Victron_Champs_principaux_(Bytes,_Scale,_Unit,_Offset)"] = scale_to_can
            if record.get("Formula"):
                record["Victron_Intervalle_typique"] = record["Formula"]
            record["Mapping_Type"] = None
            record["Compute_Inputs"] = None
            record["Scale_Tiny_To_CAN"] = None
            record["Formula"] = None

        for column in carry_columns:
            value = record.get(column)
            if value not in (None, ""):
                last_values[column] = value
            elif column in last_values:
                record[column] = last_values[column]

        normalized.append(record)

    return normalized


# ---------------------------------------------------------------------------
# JSON loading helpers


def load_can_json(path: Path) -> Dict[str, dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    return data.get("bms_can_mapping", {})


# ---------------------------------------------------------------------------
# Normalisation logic


FIELD_PATTERN = re.compile(
    r"Bytes?\s+(?P<bytes>[^:]+):\s*(?P<label>[^()]*?)\s*(?:\((?P<meta>[^)]*)\))?$"
)


def _parse_meta(meta: str | None) -> Dict[str, Optional[str]]:
    if not meta:
        return {}

    result: Dict[str, Optional[str]] = {}
    for part in meta.split(","):
        part = part.strip()
        if not part:
            continue
        if "=" in part:
            key, value = part.split("=", 1)
            result[key.strip().lower()] = value.strip()
        else:
            result[part.lower()] = "true"
    return result


@dataclass
class NormalizedRow:
    source: str
    can_id: Optional[str]
    victron_field: Optional[str]
    bytes: Optional[str]
    scale: Optional[str]
    offset: Optional[str]
    unit: Optional[str]
    signed: Optional[str]
    formula: Optional[str]
    mapping_type: Optional[str]
    compute_inputs: Optional[str]
    tiny_reg: Optional[str]
    tiny_name: Optional[str]
    tiny_type: Optional[str]
    tiny_scale_unit: Optional[str]
    scale_tiny_to_can: Optional[str]
    priority: Optional[str]
    interval: Optional[str]
    comment: Optional[str]
    pgn: Optional[str]

    def to_dict(self) -> Dict[str, Optional[str]]:
        return {
            "source": self.source,
            "can_id": self.can_id,
            "victron_field": self.victron_field,
            "bytes": self.bytes,
            "scale": self.scale,
            "offset": self.offset,
            "unit": self.unit,
            "signed": self.signed,
            "formula": self.formula,
            "mapping_type": self.mapping_type,
            "compute_inputs": self.compute_inputs,
            "tiny_reg": self.tiny_reg,
            "tiny_name": self.tiny_name,
            "tiny_type": self.tiny_type,
            "tiny_scale_unit": self.tiny_scale_unit,
            "scale_tiny_to_can": self.scale_tiny_to_can,
            "priority": self.priority,
            "interval": self.interval,
            "comment": self.comment,
            "pgn": self.pgn,
        }


def normalize_excel(records: List[Dict[str, Optional[str]]]) -> List[NormalizedRow]:
    normalized: List[NormalizedRow] = []

    for row in records:
        main_field = row.get(
            "Victron_Champs_principaux_(Bytes,_Scale,_Unit,_Offset)"
        ) or ""

        bytes_value: Optional[str] = None
        scale: Optional[str] = None
        offset: Optional[str] = None
        unit: Optional[str] = None
        signed: Optional[str] = None
        victron_field = row.get("Victron_Description")

        if main_field:
            match = FIELD_PATTERN.match(main_field)
            if match:
                bytes_value = match.group("bytes").strip()
                meta = _parse_meta(match.group("meta"))
                scale = meta.get("scale")
                offset = meta.get("offset")
                unit = meta.get("unit")
                signed = "true" if meta.get("signed") else None
            else:
                bytes_value = main_field.strip()

        if scale:
            tokens = scale.split()
            if tokens:
                scale = tokens[0]
                extras = [token for token in tokens[1:] if token]
                for extra in extras:
                    lowered = extra.lower()
                    if lowered == "signed":
                        signed = "true"
                    elif lowered not in {"unsigned"} and not unit:
                        unit = extra

        normalized.append(
            NormalizedRow(
                source="excel",
                can_id=row.get("Victron_ID_0x3xx"),
                victron_field=victron_field,
                bytes=bytes_value,
                scale=scale,
                offset=offset,
                unit=unit,
                signed=signed,
                formula=row.get("Formula"),
                mapping_type=row.get("Mapping_Type"),
                compute_inputs=row.get("Compute_Inputs"),
                tiny_reg=row.get("TinyBMS_UART_Reg"),
                tiny_name=row.get("TinyBMS_Name"),
                tiny_type=row.get("TinyBMS_Type"),
                tiny_scale_unit=row.get("TinyBMS_Scale_Unit"),
                scale_tiny_to_can=row.get("Scale_Tiny_To_CAN"),
                priority=row.get("CAN_Priority"),
                interval=row.get("Victron_Intervalle_typique"),
                comment=row.get("Comment"),
                pgn=None,
            )
        )

    return normalized


def normalize_json(can_mapping: Dict[str, dict]) -> List[NormalizedRow]:
    normalized: List[NormalizedRow] = []

    for can_id, definition in can_mapping.items():
        interval = definition.get("interval")
        pgn = definition.get("pgn")

        for field in definition.get("fields", []):
            normalized.append(
                NormalizedRow(
                    source="json",
                    can_id=can_id,
                    victron_field=field.get("victron_field"),
                    bytes=field.get("bytes"),
                    scale=_as_str(field.get("scale")),
                    offset=_as_str(field.get("offset")),
                    unit=field.get("unit"),
                    signed="true" if field.get("signed") else None,
                    formula=field.get("formula"),
                    mapping_type=field.get("mapping_type"),
                    compute_inputs=",".join(field.get("inputs", [])) or None,
                    tiny_reg=_as_str(field.get("tiny_reg")),
                    tiny_name=field.get("tiny_name"),
                    tiny_type=field.get("tiny_type"),
                    tiny_scale_unit=field.get("tiny_scale_unit"),
                    scale_tiny_to_can=_as_str(field.get("scale_tiny_to_can")),
                    priority=_as_str(field.get("priority")),
                    interval=interval,
                    comment=field.get("comment"),
                    pgn=pgn,
                )
            )

    return normalized


def _as_str(value: Optional[object]) -> Optional[str]:
    if value is None:
        return None
    return str(value)


# ---------------------------------------------------------------------------
# Audit helpers


def detect_duplicates(rows: List[NormalizedRow]) -> Dict[str, List[tuple]]:
    duplicates: Dict[str, List[tuple]] = {}
    by_source: Dict[str, Counter] = defaultdict(Counter)

    for row in rows:
        if not row.can_id and not row.bytes:
            continue
        key = (row.can_id or "", row.bytes or "", row.victron_field or "")
        by_source[row.source][key] += 1

    for source, counter in by_source.items():
        dup_keys = [key for key, count in counter.items() if count > 1]
        if dup_keys:
            duplicates[source] = dup_keys

    return duplicates


def detect_missing_formulas(rows: List[NormalizedRow]) -> List[NormalizedRow]:
    return [
        row
        for row in rows
        if (row.mapping_type or "").lower() == "compute" and not (row.formula or row.compute_inputs)
    ]


def compare_sources(excel_rows: List[NormalizedRow], json_rows: List[NormalizedRow]) -> Dict[str, List[str]]:
    findings: Dict[str, List[str]] = defaultdict(list)

    excel_index: Dict[tuple, NormalizedRow] = {}
    for row in excel_rows:
        key = (_safe_int(row.tiny_reg), row.can_id, row.bytes)
        if key[0] is not None:
            excel_index[key] = row

    json_index: Dict[tuple, NormalizedRow] = {}
    for row in json_rows:
        key = (_safe_int(row.tiny_reg), row.can_id, row.bytes)
        if key[0] is not None:
            json_index[key] = row

    shared_keys = set(excel_index) & set(json_index)
    missing_in_json = set(excel_index) - set(json_index)
    missing_in_excel = set(json_index) - set(excel_index)

    if missing_in_json:
        for key in sorted(missing_in_json):
            reg, can_id, byte_range = key
            findings["missing_in_json"].append(
                f"UART reg {reg} @ CAN {can_id or '?'} bytes {byte_range or '?'} missing in JSON"
            )

    if missing_in_excel:
        for key in sorted(missing_in_excel):
            reg, can_id, byte_range = key
            findings["missing_in_excel"].append(
                f"UART reg {reg} @ CAN {can_id or '?'} bytes {byte_range or '?'} missing in Excel"
            )

    for key in sorted(shared_keys):
        excel_row = excel_index[key]
        json_row = json_index[key]

        if _clean(excel_row.unit) != _clean(json_row.unit):
            findings["unit_mismatch"].append(
                _format_keyed_message(
                    key,
                    f"unit mismatch Excel={excel_row.unit!r} JSON={json_row.unit!r}",
                )
            )

        if _clean(excel_row.scale) != _clean(json_row.scale):
            findings["scale_mismatch"].append(
                _format_keyed_message(
                    key,
                    f"scale mismatch Excel={excel_row.scale!r} JSON={json_row.scale!r}",
                )
            )

        if _clean(excel_row.mapping_type) != _clean(json_row.mapping_type):
            findings["mapping_type_mismatch"].append(
                _format_keyed_message(
                    key,
                    f"mapping type mismatch Excel={excel_row.mapping_type!r} JSON={json_row.mapping_type!r}",
                )
            )

    return findings


def _clean(value: Optional[str]) -> Optional[str]:
    if value is None:
        return None
    return value.strip().lower()


def _format_keyed_message(key: tuple, message: str) -> str:
    reg, can_id, byte_range = key
    return f"UART reg {reg} @ CAN {can_id or '?'} bytes {byte_range or '?'}: {message}"


def _safe_int(value: Optional[str]) -> Optional[int]:
    if value is None or value == "-":
        return None
    try:
        return int(str(value).strip())
    except ValueError:
        return None


# ---------------------------------------------------------------------------
# Rendering helpers


def write_csv(rows: List[NormalizedRow], destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("w", encoding="utf-8", newline="") as csvfile:
        fieldnames = list(NormalizedRow.__dataclass_fields__.keys())
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row.to_dict())


def write_report(
    destination: Path,
    total_rows: List[NormalizedRow],
    duplicates: Dict[str, List[tuple]],
    missing_formulas: List[NormalizedRow],
    cross_findings: Dict[str, List[str]],
) -> None:
    lines: List[str] = []

    lines.append("# TinyBMS UART ↔ CAN mapping audit")
    lines.append("")
    lines.append("## Overview")
    lines.append("")

    totals = Counter(row.source for row in total_rows)
    for source, count in sorted(totals.items()):
        lines.append(f"- {source}: {count} fields")

    lines.append("")
    lines.append("## Potential issues")
    lines.append("")

    if not duplicates and not missing_formulas and not cross_findings:
        lines.append("No issues detected.")
    else:
        if duplicates:
            lines.append("### Duplicate field definitions")
            for source, entries in duplicates.items():
                lines.append(f"- **{source}**")
                for can_id, byte_range, field in entries:
                    lines.append(
                        f"  - CAN {can_id or '?'} bytes {byte_range or '?'} field {field or 'n/a'}"
                    )
            lines.append("")

        if missing_formulas:
            lines.append("### Compute mappings missing formula or inputs")
            for row in missing_formulas:
                lines.append(
                    f"- {row.source} CAN {row.can_id or '?'} bytes {row.bytes or '?'} "
                    f"({row.tiny_name or 'unknown field'})"
                )
            lines.append("")

        for category, messages in sorted(cross_findings.items()):
            title = category.replace("_", " ").capitalize()
            lines.append(f"### {title}")
            for message in messages:
                lines.append(f"- {message}")
            lines.append("")

    destination.write_text("\n".join(lines), encoding="utf-8")


# ---------------------------------------------------------------------------
# Main entry point


def main() -> None:
    excel_records = load_uart_can_mapping(DOCS_DIR / "UART_CAN_mapping.xlsx")
    json_records = load_can_json(DOCS_DIR / "TinyBMS_CAN_BMS_mapping.json")

    excel_rows = normalize_excel(excel_records)
    json_rows = normalize_json(json_records)

    combined = excel_rows + json_rows

    write_csv(combined, DOCS_DIR / "mapping_normalized.csv")

    duplicates = detect_duplicates(combined)
    missing_formulas = detect_missing_formulas(combined)
    cross_findings = compare_sources(excel_rows, json_rows)

    write_report(
        DOCS_DIR / "mapping_audit.md",
        combined,
        duplicates,
        missing_formulas,
        cross_findings,
    )


if __name__ == "__main__":
    main()

