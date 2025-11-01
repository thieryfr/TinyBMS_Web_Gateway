# TinyBMS Configuration Registers (300–343)

This document summarizes the configuration registers exposed by the TinyBMS web gateway for the Modbus holding register range 300–343 (0x012C–0x0157). Each register listed here is available for both reading and writing through the web dashboard's **Configuration** tab and the `/api/registers` REST endpoints.

| Register (dec / hex) | JSON Key | UI Label | Description |
| --- | --- | --- | --- |
| 300 / 0x012C | `fully_charged_voltage_mv` | Fully Charged Voltage | Cell voltage that marks the pack as fully charged. |
| 301 / 0x012D | `fully_discharged_voltage_mv` | Fully Discharged Voltage | Cell voltage that marks the pack as fully discharged. |
| 303 / 0x012F | `early_balancing_threshold_mv` | Early Balancing Threshold | Threshold that triggers early cell balancing. |
| 304 / 0x0130 | `charge_finished_current_ma` | Charge Finished Current | Charge current that declares the charging process finished. |
| 305 / 0x0131 | `peak_discharge_current_a` | Peak Discharge Current Cutoff | Instantaneous discharge current limit. |
| 306 / 0x0132 | `battery_capacity_ah` | Battery Capacity | Pack capacity used for State of Charge (SoC) calculations. |
| 307 / 0x0133 | `cell_count` | Number of Series Cells | Number of series-connected cells in the pack. |
| 308 / 0x0134 | `allowed_disbalance_mv` | Allowed Cell Disbalance | Maximum allowed cell voltage imbalance. |
| 310 / 0x0136 | `charger_startup_delay_s` | Charger Startup Delay | Delay before the charger output is enabled. |
| 311 / 0x0137 | `charger_disable_delay_s` | Charger Disable Delay | Delay before the charger output is disabled after a fault. |
| 315 / 0x013B | `overvoltage_cutoff_mv` | Over-voltage Cutoff | Charge cutoff voltage to prevent cell over-voltage. |
| 316 / 0x013C | `undervoltage_cutoff_mv` | Under-voltage Cutoff | Discharge cutoff voltage to prevent cell under-voltage. |
| 317 / 0x013D | `discharge_overcurrent_a` | Discharge Over-current Cutoff | Continuous discharge current limit. |
| 318 / 0x013E | `charge_overcurrent_a` | Charge Over-current Cutoff | Continuous charge current limit. |
| 319 / 0x013F | `overheat_cutoff_c` | Overheat Cutoff | Temperature threshold that disables the system. |
| 320 / 0x0140 | `low_temp_charge_cutoff_c` | Low Temperature Charge Cutoff | Low temperature threshold that blocks charging. |
| 321 / 0x0141 | `charge_restart_level_percent` | Charge Restart Level | SoC level that restarts charging. |
| 322 / 0x0142 | `battery_max_cycles` | Battery Maximum Cycles Count | Maximum allowed cycle count. |
| 323 / 0x0143 | `state_of_health_permille` | State Of Health | Manually adjustable State of Health (‰). |
| 328 / 0x0148 | `state_of_charge_permille` | State Of Charge | Manual State of Charge override (‰). |
| 329 / 0x0149 | `invert_ext_current_sensor` | Invert External Current Sensor | Inverts the polarity of the external current sensor. |
| 330 / 0x014A | `charger_type` | Charger Type | Charger output control mode. |
| 331 / 0x014B | `load_switch_type` | Load Switch Type | Output channel used for the load switch. |
| 332 / 0x014C | `automatic_recovery_count` | Automatic Recovery Attempts | Number of automatic recovery attempts. |
| 333 / 0x014D | `charger_switch_type` | Charger Switch Type | Output channel driving the charger. |
| 334 / 0x014E | `ignition_source` | Ignition Source | Input used as ignition detection. |
| 335 / 0x014F | `charger_detection_source` | Charger Detection Source | Source used to detect charger presence. |
| 337 / 0x0151 | `precharge_pin` | Precharge Output | Output controlling the precharge contactor. |
| 338 / 0x0152 | `precharge_duration` | Precharge Duration | Duration of the precharge phase. |
| 339 / 0x0153 | `temperature_sensor_type` | Temperature Sensor Type | Temperature sensor type selection. |
| 340 / 0x0154 | `operation_mode` | BMS Operation Mode | Dual-port or single-port mode selection. |
| 341 / 0x0155 | `single_port_switch_type` | Single Port Switch Type | Output used in single-port mode. |
| 342 / 0x0156 | `broadcast_interval` | Broadcast Interval | UART broadcast period. |
| 343 / 0x0157 | `communication_protocol` | Communication Protocol | UART communication protocol selection. |

All registers in this range are defined with read-write access (`CONFIG_MANAGER_ACCESS_RW`) and can be modified via the gateway.
