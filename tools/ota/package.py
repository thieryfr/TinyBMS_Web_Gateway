"""Package build artifacts into the OTA manifest structure."""
from __future__ import annotations

import argparse
import json
import logging
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, Optional

try:
    from .common import Manifest, ManifestValidationError, compute_sha256, load_manifest
except ImportError:  # pragma: no cover - execution as a script
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from tools.ota.common import (  # type: ignore
        Manifest,
        ManifestValidationError,
        compute_sha256,
        load_manifest,
    )

_LOGGER = logging.getLogger("ota.package")


def _write_json(path: Path, payload: dict, dry_run: bool) -> None:
    if dry_run:
        _LOGGER.info("[dry-run] Would write JSON to %s", path)
        return
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _copy_binary(source: Path, destination: Path, dry_run: bool) -> None:
    if dry_run:
        _LOGGER.info("[dry-run] Would copy %s to %s", source, destination)
        return
    shutil.copy2(source, destination)


def _update_manifest(manifest: Manifest, version_id: str, rel_path: Path, label: Optional[str], dry_run: bool) -> None:
    entry = None
    for candidate in manifest.raw["versions"]:
        if candidate.get("id") == version_id:
            entry = candidate
            break
    if entry is None:
        entry = {"id": version_id}
        manifest.raw["versions"].append(entry)
    entry["manifest"] = rel_path.as_posix()
    if label:
        entry["label"] = label
    if dry_run:
        _LOGGER.info("[dry-run] Would update manifest with version %s -> %s", version_id, rel_path)
        return
    manifest.path.write_text(json.dumps(manifest.raw, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _build_artifact_entry(
    manifest: Manifest,
    project: str,
    version: str,
    artifact_name: str,
    binary_path: Path,
) -> dict:
    checksum = compute_sha256(binary_path)
    size = binary_path.stat().st_size
    channels = {name: channel.config for name, channel in manifest.deployment.channels.items()}
    return {
        "version": version,
        "created_at": datetime.now(tz=timezone.utc).isoformat(),
        "channels": channels,
        "artifacts": [
            {
                "name": project,
                "filename": artifact_name,
                "size": size,
                "sha256": checksum,
                "protocols": list(manifest.deployment.default_protocols),
                "channels": channels,
            }
        ],
    }


def package_build(
    manifest_path: Path,
    binary_path: Path,
    version: str,
    project: Optional[str] = None,
    output_directory: Optional[Path] = None,
    artifact_name: Optional[str] = None,
    label: Optional[str] = None,
    update_manifest: bool = False,
    dry_run: bool = False,
) -> Path:
    manifest = load_manifest(manifest_path)
    project_name = project or manifest.product.get("name", "firmware")
    manifest_dir = manifest.path.parent.resolve()

    if output_directory is None:
        output_directory = manifest_dir / "versions"
    output_directory = output_directory.resolve()

    version_dir = output_directory / version
    artifact_filename = artifact_name or f"{project_name}-{version}.bin"
    version_dir.mkdir(parents=True, exist_ok=True)
    destination_binary = version_dir / artifact_filename

    _copy_binary(binary_path, destination_binary, dry_run)

    artifact_manifest = version_dir / "ota.json"
    entry = _build_artifact_entry(
        manifest=manifest,
        project=project_name,
        version=version,
        artifact_name=artifact_filename,
        binary_path=binary_path,
    )
    _write_json(artifact_manifest, entry, dry_run)

    if update_manifest:
        rel_path = artifact_manifest.resolve().relative_to(manifest_dir)
        _update_manifest(manifest, version, rel_path, label, dry_run)

    return artifact_manifest


def _parse_args(argv: Optional[Iterable[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate OTA metadata for a build artifact")
    parser.add_argument("--manifest", type=Path, default=Path("ota/manifest.json"), help="Path to the OTA manifest")
    parser.add_argument("--binary", type=Path, required=True, help="Firmware binary produced by the build")
    parser.add_argument("--version", required=True, help="Firmware version string")
    parser.add_argument("--project", help="Project name to associate with the artifact")
    parser.add_argument("--output", type=Path, help="Directory where OTA versions are produced")
    parser.add_argument("--artifact-name", help="Override the generated artifact filename")
    parser.add_argument("--label", help="Human readable label for the release")
    parser.add_argument("--update-manifest", action="store_true", help="Update the root manifest with the new version entry")
    parser.add_argument("--dry-run", action="store_true", help="Only log steps without copying or writing files")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose logging output")
    return parser.parse_args(list(argv) if argv is not None else None)


def main(argv: Optional[Iterable[str]] = None) -> int:
    args = _parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO, format="%(levelname)s: %(message)s")
    try:
        package_build(
            manifest_path=args.manifest,
            binary_path=args.binary,
            version=args.version,
            project=args.project,
            output_directory=args.output,
            artifact_name=args.artifact_name,
            label=args.label,
            update_manifest=args.update_manifest,
            dry_run=args.dry_run,
        )
    except ManifestValidationError as exc:
        _LOGGER.error("Manifest error: %s", exc)
        return 2
    except Exception:  # pragma: no cover - defensive
        _LOGGER.exception("Unexpected error while packaging OTA artifact")
        return 1
    return 0


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    sys.exit(main())
