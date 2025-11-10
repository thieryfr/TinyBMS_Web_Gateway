# OTA Firmware Workflow

This repository ships with a minimal over-the-air (OTA) packaging and deployment
workflow tailored for the TinyBMS Web Gateway firmware. The workflow is powered
by two helper scripts located under `tools/ota/`:

* `deploy.py` – reads the OTA manifest and delivers the selected firmware image
  through MQTT and/or HTTPS.
* `package.py` – produces the per-version metadata and optionally updates the
  root OTA manifest after a successful `idf.py build`.

The sections below describe the manifest layout, how to package a freshly built
image, and how to deploy it to fleet devices.

## Manifest structure

The root manifest (`ota/manifest.json`) is intentionally small so that it can be
tracked in source control. It references version-specific manifests stored in
`ota/versions/<version>/ota.json`.

```json
{
  "$schema": "https://tinybms-web-gateway.local/schemas/ota-manifest.schema.json",
  "schema_version": 1,
  "product": {
    "name": "TinyBMS Web Gateway",
    "hardware": "esp32",
    "description": "OTA manifest describing firmware deployment targets.",
    "maintainer": "TinyBMS Team"
  },
  "deployment": {
    "default_protocols": ["mqtt", "https"],
    "channels": {
      "mqtt": {
        "broker": "mqtts://broker.example.com:8883",
        "topic": "tinybms/gateway/ota",
        "qos": 1,
        "client_id": "tinybms-gateway-updater",
        "keepalive": 60
      },
      "https": {
        "url": "https://ota.example.com/api/v1/firmware",
        "method": "PUT",
        "headers": {"Authorization": "Bearer <token>"},
        "timeout": 30
      }
    }
  },
  "versions": [
    {
      "id": "0.0.1",
      "label": "Example firmware entry included with the repository",
      "manifest": "versions/0.0.1/ota.json"
    }
  ]
}
```

* `schema_version` allows future upgrades to the manifest format.
* `product` collects metadata about the firmware line.
* `deployment` defines the default delivery channels. Each channel is a JSON
  object describing connection parameters (MQTT broker URL, HTTPS endpoint,
  authentication headers, etc.).
* `versions` lists the available firmware revisions and points to the detailed
  manifests produced by `package.py`.

A version manifest contains all the information required to send the firmware to
a device. It typically lives in `ota/versions/<version>/ota.json` and resembles
the following structure:

```json
{
  "version": "1.2.3",
  "created_at": "2024-04-12T09:10:11.123456Z",
  "channels": {"mqtt": {"topic": "tinybms/gateway/ota"}},
  "artifacts": [
    {
      "name": "TinyBMS Web Gateway",
      "filename": "TinyBMS_Web_Gateway-1.2.3.bin",
      "size": 1310720,
      "sha256": "...",
      "protocols": ["mqtt", "https"]
    }
  ]
}
```

The `channels` section can override the root defaults for a specific release or
artifact (useful for canary deployments).

## Packaging a build

`tools/ota/package.py` is executed automatically at the end of `idf.py build`
and can also be invoked manually:

```bash
python tools/ota/package.py \
  --manifest ota/manifest.json \
  --binary build/TinyBMS_WebGateway.bin \
  --version 1.2.3 \
  --update-manifest
```

The script will:

1. Copy the compiled binary into `ota/versions/1.2.3/`.
2. Generate `ota/versions/1.2.3/ota.json` with size, checksum, and channel
   information.
3. Update `ota/manifest.json` to reference the new version when
   `--update-manifest` is supplied.

Use `--dry-run` to preview the actions without touching the filesystem, and
`--output` if the OTA artifacts must be written outside of the repository tree.

## Deploying a firmware version

The deployment helper reads the manifest and forwards the binary via the selected
protocols. MQTT uses `paho-mqtt` when available, while HTTPS relies solely on
Python’s standard library.

```bash
python tools/ota/deploy.py --version 1.2.3 --dry-run
```

To restrict the transport to MQTT only:

```bash
python tools/ota/deploy.py --version 1.2.3 --transport mqtt
```

When `--dry-run` is omitted, the script publishes the binary payload to the
configured channels. Ensure that credentials, certificates, and network
firewalls permit the connection before executing a live deployment.

## Integration with the build system

`idf.py build` invokes the packaging script automatically. The generated
artifacts are created under `ota/versions/<version>/`. Because the script updates
the manifest when `--update-manifest` is passed by CMake, remember to commit the
resulting JSON changes when preparing a release.

## Smoke tests

A convenience test runner lives at `tools/test_ota.py`. It performs a dry-run
packaging cycle in a temporary directory, validates the JSON schema used by the
repository, and exercises the deployment script without sending network traffic.
Execute it as part of CI or before submitting a patch:

```bash
python tools/test_ota.py
```
