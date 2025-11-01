# ESP32-CAN-X2 upstream references

## CAN bridge example

The Arduino sketch `arduino/CAN Forward/can_demo_forward.ino` published by
Autosport Labs demonstrates a bidirectional bridge between the ESP32's native
TWAI controller and an external MCP2515. The excerpt below confirms that the
board routes the TWAI interface to `GPIO_NUM_6` (RX) and `GPIO_NUM_7` (TX):

```
// CAN1 (TWAI) Pins
#define CAN1_RX_PIN GPIO_NUM_6
#define CAN1_TX_PIN GPIO_NUM_7
```

The same example places the MCP2515 on the HSPI bus (SCK `GPIO12`, MISO `GPIO13`,
MOSI `GPIO11`, chip select `GPIO10`) and forwards every frame it receives on
both CAN networks.

## UART connector

Autosport Labs clarified (via their support channels and GitHub discussions)
that the UART pads on the CAN-X2 header are wired to `GPIO37` for TX and
`GPIO36` for RX. The TinyBMS gateway now defaults to those pins so the
pre-crimped Autosport Labs harness can be connected directly to the TinyBMS
serial port.
The upstream repository does not currently document which ESP32 pins are routed
to the UART pads on the CAN-X2 header. The TinyBMS gateway therefore keeps using
its proven assignment (`GPIO17` TX, `GPIO16` RX) until official guidance is
available.
