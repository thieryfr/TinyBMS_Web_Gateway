# ESP32-CAN-X2 pinout summary

The following table reproduces the connector description from
`archive/docs/ESP32-CAN-X2_Wiki_Autosport-Labs.pdf` so that the firmware
configuration can be cross-referenced without opening the PDF.

| Pin | Function          | Description                                               |
| --- | ----------------- | --------------------------------------------------------- |
| 1   | CAN1H / 2.7D      | High-level signal for the first CAN channel               |
| 2   | CAN1L / 2.7D      | Low-level signal for the first CAN channel                |
| 3   | CAN2H / 2.7C      | High-level signal for the second CAN channel              |
| 4   | CAN2L / 2.7C      | Low-level signal for the second CAN channel               |
| 5   | RX Pin            | USART RX                                                  |
| 6   | TX Pin            | USART TX                                                  |
| 7–20| GPIO Pins         | 3.3 V tolerant GPIO (avoid applying higher voltages)      |

The default CAN1 GPIO assignments exposed through `menuconfig`
(`GPIO7` for TX and `GPIO6` for RX) correspond to the CAN1 differential
pair on this header, as confirmed by the upstream CAN bridge example
(`can_demo_forward.ino`) that also ships with the ESP32-CAN-X2
repository.【F:archive/docs/reference/esp32_can_x2_repo_notes.md†L4-L18】 The
CAN2 lines are intentionally left free so that a secondary controller
such as an MCP2515 can be added later if required.

Autosport Labs have not yet published the ESP32 pin numbers associated
with the UART pads on this connector. The TinyBMS gateway therefore keeps
its UART driver on `GPIO17` (TX) and `GPIO16` (RX) until official
documentation becomes available.【F:archive/docs/reference/esp32_can_x2_repo_notes.md†L20-L26】
