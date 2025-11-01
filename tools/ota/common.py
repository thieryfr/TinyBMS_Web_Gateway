"""Shared helpers for OTA tooling."""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional
import hashlib
import json


@dataclass(frozen=True)
class DeploymentChannel:
    """Deployment channel definition."""

    name: str
    config: Dict[str, Any]


@dataclass(frozen=True)
class DeploymentConfig:
    """Configuration defaults for OTA delivery."""

    channels: Dict[str, DeploymentChannel]
    default_protocols: List[str]

    def merge_channels(self, overrides: Optional[Dict[str, Dict[str, Any]]]) -> Dict[str, Dict[str, Any]]:
        """Return a channel mapping merging deployment defaults and overrides."""
        merged: Dict[str, Dict[str, Any]] = {name: channel.config.copy() for name, channel in self.channels.items()}
        if overrides:
            for name, config in overrides.items():
                base = merged.get(name, {}).copy()
                base.update(config)
                merged[name] = base
        return merged


@dataclass(frozen=True)
class ManifestVersion:
    """Metadata describing a specific firmware version."""

    identifier: str
    label: Optional[str]
    manifest_path: Path


@dataclass(frozen=True)
class VersionArtifact:
    """Description of a deployable artifact."""

    name: str
    source_path: Path
    filename: str
    size: int
    sha256: str
    protocols: List[str]
    channels: Dict[str, Dict[str, Any]]

    @property
    def path(self) -> Path:
        return self.source_path


@dataclass(frozen=True)
class Manifest:
    """OTA manifest container."""

    path: Path
    schema_version: int
    product: Dict[str, Any]
    deployment: DeploymentConfig
    versions: Dict[str, ManifestVersion]
    raw: Dict[str, Any]


class ManifestValidationError(RuntimeError):
    """Raised when the manifest structure is invalid."""


def _ensure(condition: bool, message: str) -> None:
    if not condition:
        raise ManifestValidationError(message)


def compute_sha256(path: Path) -> str:
    """Compute the SHA-256 checksum for the provided file."""
    digest = hashlib.sha256()
    with path.open("rb") as file_obj:
        for chunk in iter(lambda: file_obj.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _normalize_channels(raw_channels: Dict[str, Dict[str, Any]]) -> Dict[str, DeploymentChannel]:
    normalized: Dict[str, DeploymentChannel] = {}
    for name, config in raw_channels.items():
        if not isinstance(config, dict):
            raise ManifestValidationError(f"Configuration for channel '{name}' must be an object")
        normalized[name] = DeploymentChannel(name=name, config=config)
    return normalized


def load_manifest(path: Path | str) -> Manifest:
    """Load and validate the root manifest."""
    manifest_path = Path(path)
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    validate_manifest(data, manifest_path.parent)

    schema_version = data["schema_version"]
    product = data["product"]
    deployment_cfg = data.get("deployment", {})
    channels = deployment_cfg.get("channels", {})
    default_protocols = deployment_cfg.get("default_protocols", ["mqtt", "https"])
    deployment = DeploymentConfig(
        channels=_normalize_channels(channels),
        default_protocols=list(default_protocols),
    )

    versions: Dict[str, ManifestVersion] = {}
    for version in data["versions"]:
        version_id = version["id"]
        manifest_rel = version["manifest"]
        manifest_path_full = (manifest_path.parent / manifest_rel).resolve()
        versions[version_id] = ManifestVersion(
            identifier=version_id,
            label=version.get("label"),
            manifest_path=manifest_path_full,
        )

    return Manifest(
        path=manifest_path,
        schema_version=schema_version,
        product=product,
        deployment=deployment,
        versions=versions,
        raw=data,
    )


def validate_manifest(data: Dict[str, Any], base_dir: Path | None = None) -> None:
    """Ensure the provided manifest structure matches expectations."""
    _ensure(isinstance(data, dict), "The manifest must be a JSON object")
    _ensure(isinstance(data.get("schema_version"), int), "'schema_version' must be an integer")
    _ensure("product" in data and isinstance(data["product"], dict), "'product' section is required")
    _ensure("versions" in data and isinstance(data["versions"], list) and data["versions"], "'versions' must be a non-empty list")

    deployment = data.get("deployment", {})
    _ensure(isinstance(deployment, dict), "'deployment' must be an object when provided")
    channels = deployment.get("channels", {})
    if channels:
        _ensure(isinstance(channels, dict), "'deployment.channels' must be an object")
        for name, config in channels.items():
            _ensure(isinstance(name, str), "Channel names must be strings")
            _ensure(isinstance(config, dict), f"Channel '{name}' must be a JSON object")
    default_protocols = deployment.get("default_protocols", ["mqtt", "https"])
    _ensure(
        isinstance(default_protocols, list) and all(isinstance(item, str) for item in default_protocols),
        "'deployment.default_protocols' must be a list of protocol names",
    )

    for entry in data["versions"]:
        _ensure(isinstance(entry, dict), "Each entry in 'versions' must be an object")
        _ensure("id" in entry and isinstance(entry["id"], str), "Each version requires an 'id' string")
        _ensure("manifest" in entry and isinstance(entry["manifest"], str), f"Version '{entry.get('id', '<unknown>')}' must define a 'manifest' path")
        if base_dir is not None:
            version_manifest_path = (base_dir / entry["manifest"]).resolve()
            _ensure(version_manifest_path.exists(), f"Referenced version manifest '{entry['manifest']}' is missing")


def resolve_version(manifest: Manifest, version_id: str) -> ManifestVersion:
    """Retrieve the manifest entry for a specific version."""
    try:
        return manifest.versions[version_id]
    except KeyError as exc:  # pragma: no cover - defensive branch
        known = ", ".join(sorted(manifest.versions)) or "<none>"
        raise ManifestValidationError(
            f"Version '{version_id}' is not declared in the manifest (known: {known})"
        ) from exc


def load_version_manifest(manifest: Manifest, version: ManifestVersion) -> Dict[str, Any]:
    """Load the manifest associated with a specific firmware version."""
    version_data = json.loads(version.manifest_path.read_text(encoding="utf-8"))
    _ensure(version_data.get("version") == version.identifier, "Version manifest does not match the requested identifier")
    _ensure(isinstance(version_data.get("artifacts"), list) and version_data["artifacts"], "Version manifest must provide at least one artifact")
    for artifact in version_data["artifacts"]:
        _ensure(isinstance(artifact, dict), "Artifacts must be JSON objects")
        required = ["name", "filename", "size", "sha256"]
        for key in required:
            _ensure(key in artifact, f"Artifact is missing required key '{key}'")
        protocols = artifact.get("protocols")
        if protocols is not None:
            _ensure(
                isinstance(protocols, list) and protocols,
                "When provided, 'protocols' must be a non-empty list",
            )
    return version_data


def iter_artifacts(
    manifest: Manifest, version: ManifestVersion, version_data: Dict[str, Any]
) -> Iterable[VersionArtifact]:
    """Yield normalized artifact definitions for a version."""
    deployment = manifest.deployment
    overrides = version_data.get("channels")
    default_channels = deployment.merge_channels(overrides)
    manifest_dir = version.manifest_path.parent
    base_override = version_data.get("base_path")
    if isinstance(base_override, str):
        base_path = (manifest_dir / base_override).resolve()
    else:
        base_path = manifest_dir

    for artifact in version_data["artifacts"]:
        artifact_channels = deployment.merge_channels(artifact.get("channels"))
        channels = default_channels.copy()
        channels.update(artifact_channels)
        protocols = artifact.get("protocols") or list(manifest.deployment.default_protocols)
        source_path = (base_path / artifact["filename"]).resolve()
        if not source_path.exists():
            raise ManifestValidationError(
                f"Artifact '{artifact['name']}' file '{source_path}' is missing"
            )
        actual_size = source_path.stat().st_size
        if actual_size != int(artifact["size"]):
            raise ManifestValidationError(
                f"Artifact '{artifact['name']}' size mismatch: manifest={artifact['size']} actual={actual_size}"
            )
        actual_hash = compute_sha256(source_path)
        if actual_hash != artifact["sha256"]:
            raise ManifestValidationError(
                f"Artifact '{artifact['name']}' checksum mismatch"
            )
        yield VersionArtifact(
            name=artifact["name"],
            source_path=source_path,
            filename=str(artifact["filename"]),
            size=int(artifact["size"]),
            sha256=str(artifact["sha256"]),
            protocols=[str(protocol) for protocol in protocols],
            channels=channels,
        )
