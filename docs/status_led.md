# Status LED behaviour

The ESP32 status LED connected to `GPIO_NUM_2` exposes a concise visual summary of the
TinyBMS Web Gateway runtime state. The firmware drives the LED through the
`status_led` module which subscribes to the global event bus and reacts to system
notifications.

## Pattern summary

| Context | Pattern | Notes |
| --- | --- | --- |
| Boot & hardware initialisation | 4 Hz blink (125 ms on / 125 ms off) | Stops once `app_main` finishes initialisation and calls `status_led_notify_system_ready()` |
| Ready but not connected | 1 Hz blink (500 ms on / 500 ms off) | Applies while the Wi-Fi station is connecting or when no network is configured |
| Wi-Fi station connected & has IP | Solid on with 100 ms off pulses on bus activity | Activity pulses are triggered by CAN, UART, MQTT, telemetry and UI events |
| Access point / maintenance mode | 0.5 Hz blink (1 s on / 1 s off) | Exposes that the fallback AP is active |
| Flash storage unavailable | Double flash (two 100 ms pulses followed by 1 s pause) | Cleared automatically when the history volume mounts |
| OTA upload in progress | 250 ms blink | Automatically expires after 30 s if no follow-up event arrives |

## Activity feedback

Whenever frames are exchanged on the CAN bus, UART telemetry arrives or the
system generates MQTT/UI events, the LED briefly turns off for 100 ms while in
“connected” mode. This allows monitoring of live traffic without masking the
connection state.

## Failure handling

If the history storage is reported as unavailable the double-flash pattern takes
precedence over any other state. OTA uploads temporarily override the current
pattern until the 30 s window elapses, after which the LED reverts to the most
recent non-error state.

## Extending the mapping

The controller is implemented in `main/status_led/status_led.c`. Additional
patterns can be mapped by publishing new events on the event bus and updating
the switch statement in `status_led_handle_event()`.
