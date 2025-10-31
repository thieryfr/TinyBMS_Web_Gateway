# TinyBMS Web Gateway

This repository hosts the TinyBMS Web Gateway firmware and web assets. The current snapshot provides the project structure and stub modules that will be fleshed out in future iterations.

## Project Layout

- `CMakeLists.txt` – Root build file for ESP-IDF.
- `sdkconfig.defaults` – Default SDK configuration template.
- `partitions.csv` – Partition table with OTA and SPIFFS slots.
- `main/` – Application source tree split by functional domain.
- `web/` – Static web dashboard assets served by the gateway.
- `test/` – Placeholder for upcoming unit tests.
- `docs/` – Reference documentation and design artifacts.

## Getting Started

1. Install the [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) toolchain.
2. Configure the project: `idf.py set-target esp32 && idf.py menuconfig`.
3. Build and flash: `idf.py build flash`.

## License

See individual files for license information.
