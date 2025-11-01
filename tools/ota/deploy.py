"""OTA deployment helper for TinyBMS Web Gateway firmware."""
from __future__ import annotations

import argparse
import logging
import ssl
import sys
from pathlib import Path
from typing import Dict, Iterable, Optional
from urllib.parse import urlparse
from urllib.request import Request, urlopen

try:
    from .common import (
        Manifest,
        ManifestValidationError,
        ManifestVersion,
        VersionArtifact,
        iter_artifacts,
        load_manifest,
        load_version_manifest,
        resolve_version,
    )
except ImportError:  # pragma: no cover - execution as a script
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from tools.ota.common import (  # type: ignore
        Manifest,
        ManifestValidationError,
        ManifestVersion,
        VersionArtifact,
        iter_artifacts,
        load_manifest,
        load_version_manifest,
        resolve_version,
    )

try:  # pragma: no cover - optional dependency
    import paho.mqtt.client as mqtt
except ImportError:  # pragma: no cover - optional dependency
    mqtt = None  # type: ignore

_LOGGER = logging.getLogger("ota.deploy")


def _load_artifacts(manifest: Manifest, version: ManifestVersion) -> Iterable[VersionArtifact]:
    version_data = load_version_manifest(manifest, version)
    return iter_artifacts(manifest, version, version_data)


def _deploy_mqtt(artifact: VersionArtifact, channel: Dict[str, object], dry_run: bool) -> None:
    broker_url = str(channel.get("broker", ""))
    topic = channel.get("topic")
    if not broker_url or not topic:
        raise ManifestValidationError("MQTT channel requires both 'broker' and 'topic'")

    qos = int(channel.get("qos", 1))
    retain = bool(channel.get("retain", False))
    client_id = str(channel.get("client_id", f"ota-{artifact.name}"))

    parsed = urlparse(broker_url)
    if parsed.scheme not in {"mqtt", "mqtts"}:
        raise ManifestValidationError("Unsupported MQTT scheme: expected mqtt:// or mqtts://")

    if dry_run:
        _LOGGER.info("[MQTT] Would publish %s (%d bytes, qos=%s) to %s", artifact.path, artifact.size, qos, topic)
        return

    if mqtt is None:
        raise RuntimeError("paho-mqtt is required for MQTT deployments")

    port = parsed.port or (8883 if parsed.scheme == "mqtts" else 1883)
    client = mqtt.Client(client_id=client_id)
    username = channel.get("username")
    if username:
        client.username_pw_set(username=str(username), password=str(channel.get("password", "")) or None)

    if parsed.scheme == "mqtts":
        context = ssl.create_default_context()
        if channel.get("insecure", False):
            context.check_hostname = False
            context.verify_mode = ssl.CERT_NONE
        client.tls_set_context(context)

    _LOGGER.debug("Connecting to MQTT broker %s:%d", parsed.hostname, port)
    client.connect(parsed.hostname, port, keepalive=int(channel.get("keepalive", 60)))
    with artifact.path.open("rb") as file_obj:
        payload = file_obj.read()
    result = client.publish(str(topic), payload=payload, qos=qos, retain=retain)
    result.wait_for_publish()
    client.disconnect()
    _LOGGER.info("[MQTT] Published %s (%d bytes) to %s", artifact.path.name, artifact.size, topic)


def _deploy_https(artifact: VersionArtifact, channel: Dict[str, object], dry_run: bool) -> None:
    url = channel.get("url")
    if not url:
        raise ManifestValidationError("HTTPS channel requires an 'url' entry")
    method = str(channel.get("method", "PUT")).upper()
    headers = {str(key): str(value) for key, value in (channel.get("headers", {}) or {}).items()}
    headers.setdefault("Content-Type", "application/octet-stream")

    if dry_run:
        _LOGGER.info("[HTTPS] Would send %s (%d bytes) to %s via %s", artifact.path, artifact.size, url, method)
        return

    verify = channel.get("verify", True)
    timeout = float(channel.get("timeout", 30))
    context = ssl.create_default_context()
    if not verify:
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE

    with artifact.path.open("rb") as file_obj:
        payload = file_obj.read()

    request = Request(str(url), data=payload, method=method, headers=headers)
    with urlopen(request, context=context, timeout=timeout) as response:
        response.read()  # drain body to ensure request completion
    _LOGGER.info("[HTTPS] Uploaded %s (%d bytes) to %s", artifact.path.name, artifact.size, url)


def deploy_version(
    manifest_path: Path,
    version_id: str,
    transports: Optional[Iterable[str]] = None,
    dry_run: bool = False,
) -> None:
    manifest = load_manifest(manifest_path)
    version = resolve_version(manifest, version_id)
    selected = {transport.lower() for transport in transports} if transports else None

    for artifact in _load_artifacts(manifest, version):
        available_channels = artifact.channels
        _LOGGER.debug("Preparing artifact %s using protocols %s", artifact.name, artifact.protocols)
        for protocol in artifact.protocols:
            protocol_name = protocol.lower()
            if selected and protocol_name not in selected:
                _LOGGER.debug("Skipping protocol %s because of user selection", protocol_name)
                continue
            channel = available_channels.get(protocol_name)
            if channel is None:
                raise ManifestValidationError(
                    f"Artifact '{artifact.name}' declares protocol '{protocol_name}' without a channel configuration"
                )
            if protocol_name == "mqtt":
                _deploy_mqtt(artifact, channel, dry_run)
            elif protocol_name == "https":
                _deploy_https(artifact, channel, dry_run)
            else:  # pragma: no cover - unknown protocols guarded in manifest validation
                raise ManifestValidationError(f"Unsupported protocol '{protocol_name}'")


def _parse_args(argv: Optional[Iterable[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Deploy an OTA firmware release using the manifest")
    parser.add_argument("--manifest", type=Path, default=Path("ota/manifest.json"), help="Path to the OTA manifest")
    parser.add_argument("--version", required=True, help="Version identifier to deploy")
    parser.add_argument(
        "--transport",
        dest="transports",
        action="append",
        choices=["mqtt", "https"],
        help="Restrict deployment to one or more transports",
    )
    parser.add_argument("--dry-run", action="store_true", help="Only log planned actions without sending data")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose logging output")
    return parser.parse_args(list(argv) if argv is not None else None)


def main(argv: Optional[Iterable[str]] = None) -> int:
    args = _parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO, format="%(levelname)s: %(message)s")
    try:
        deploy_version(args.manifest, args.version, args.transports, args.dry_run)
    except ManifestValidationError as exc:
        _LOGGER.error("Manifest error: %s", exc)
        return 2
    except Exception:  # pragma: no cover - unhandled error logging
        _LOGGER.exception("Unhandled exception during OTA deployment")
        return 1
    return 0


if __name__ == "__main__":  # pragma: no cover - CLI entrypoint
    sys.exit(main())
