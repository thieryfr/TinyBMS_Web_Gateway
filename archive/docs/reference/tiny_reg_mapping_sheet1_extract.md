# Tiny reg mapping – sheet1 extract

Derived from `Tiny_reg_mapping 2.xlsx` (Sheet1).

| PGN | Tiny Reg | Tiny Name | Tiny Type | Tiny Scale | Field | CAN Type | Mapping | Inputs | Scale | Formula |
|-----|----------|-----------|-----------|-----------|-------|----------|---------|--------|-------|---------|
| 0x351 | - | Charge Voltage Limit (CVL) | - | - | ChargeVoltageLimit | un16/0.1V | Compute | fullyChargedVoltage,maxCellVoltage,bmsTemp,minChargeVoltage | - | CVL = max(minChargeVoltage, fullyChargedVoltage - fV(maxCellVoltage) - fT(bmsTemp)) |
| 0x351 | 103 | Max Charge Current | UINT16 | 0.1 A | MaxChargeCurrent CCL | sn16/0.1A | Compute | maxChargeCurrent,bmsTemp,soc | ×1→(0.1A) | CCL = min(maxChargeCurrent, tempDerate, socDerate) |
| 0x351 | 102 | Max Discharge Current | UINT16 | 0.1 A | MaxDischargeCurrent DCL | sn16/0.1A | Compute | maxDischargeCurrent,bmsTemp,soc | ×1→(0.1A) | DCL = min(maxDischargeCurrent, tempDerate, socDerate) |
| 0x355 | 46 | State Of Charge | UINT32 | 0.002 % | SOC | un16/% | Direct | - | ×0.002→% (round) | SOC from Tiny high-res |
| 0x355 | 45 | State Of Health | UINT16 | 0.002 % | SOH | un16/% | Direct | - | ×0.002→% (round) | SOH from Tiny |
| 0x356 | 36 | Battery Pack Voltage | FLOAT | 1 V | BatteryVoltage | sn16/0.01V | Direct | - | ×100 | Quantize to 0.01 V |
| 0x356 | 38 | Battery Pack Current | FLOAT | 0,1 A | BatteryCurrent | sn16/0.1A | Direct | - | ×10 | Sign: charge + / discharge – |
| 0x356 | 48 | Internal Temperature | INT16 | 0.1 °C | BatteryTemperature | sn16/0.1°C | Direct | - | ×1→(0.1°C) | — |
| 0x35A | 36.314999999999998 | Battery High Voltage Alarm | UINT8 | - | BatteryHighVoltageAlarm | bits2 | Compute | packVoltage,highVoltageCutoff | - | if packVoltage > highVoltageCutoff then 1 else 0 |
| 0x35A | 36.316000000000003 | Battery Low Voltage Alarm | UINT8 | - | BatteryLowVoltageAlarm | bits2 | Compute | packVoltage,lowVoltageCutoff | - | if packVoltage < lowVoltageCutoff then 1 else 0 |
| 0x35A | 113.318 | Battery High Temperature Alarm | UINT8 | - | BatteryHighTempAlarm | bits2 | Compute | maxTemp,highTempCutoff | - | if maxTemp > highTempCutoff then 1 else 0 |
| 0x35A | 113.319 | Battery Low Temperature Alarm | UINT8 | - | BatteryLowTempAlarm | bits2 | Compute | minTemp,lowTempCutoff | - | if minTemp < lowTempCutoff then 1 else 0 |
| 0x35A | 42.319000000000003 | Battery High Temp Charge Alarm | UINT8 | - | BatteryHighTempChargeAlarm | bits2 | Compute | externalTemp,highTempChargeCutoff | - | if externalTemp > highTempChargeCutoff then 1 else 0 |
| 0x35A | 42.32 | Battery Low Temp Charge Warning | UINT8 | - | BatteryLowTempChargeWarning | bits2 | Compute | externalTemp,lowTempChargeCutoff | - | if externalTemp < lowTempChargeCutoff then 1 else 0 |
| 0x35A | 38.317 | Battery High Current Alarm | UINT8 | - | BatteryHighCurrentAlarm | bits2 | Compute | packCurrent,overCurrentCutoff | - | if packCurrent > overCurrentCutoff then 1 else 0 |
| 0x35A | 38.317999999999998 | Battery High Charge Current Alarm | UINT8 | - | BatteryHighChargeCurrentAlarm | bits2 | Compute | packCurrent,overChargeCurrentCutoff | - | if packCurrent > overChargeCurrentCutoff then 1 else 0 |
| 0x35A | 52 | Cell Imbalance Alarm | UINT8 | - | CellImbalanceAlarm | bits2 | Compute | balancingState | - | if balancingState != 0 then 1 else 0 |
| 0x35A | 36.314999999999998 | Battery High Voltage Warning | UINT8 | - | BatteryHighVoltageWarning | bits2 | Compute | packVoltage,highVoltageCutoff | - | if packVoltage > (highVoltageCutoff*0.8) then 1 else 0 |
| 0x35A | 36.316000000000003 | Battery Low Voltage Warning | UINT8 | - | BatteryLowVoltageWarning | bits2 | Compute | packVoltage,lowVoltageCutoff | - | if packVoltage < (lowVoltageCutoff*1.2) then 1 else 0 |
| 0x35A | 113.318 | Battery High Temperature Warning | UINT8 | - | BatteryHighTempWarning | bits2 | Compute | maxTemp,highTempCutoff | - | if maxTemp > (highTempCutoff*0.8) then 1 else 0 |
| 0x35A | 113.319 | Battery Low Temperature Warning | UINT8 | - | BatteryLowTempWarning | bits2 | Compute | minTemp,lowTempCutoff | - | if minTemp < (lowTempCutoff*1.2) then 1 else 0 |
| 0x35A | 42.319000000000003 | Battery High Temp Charge Warning | UINT8 | - | BatteryHighTempChargeWarning | bits2 | Compute | externalTemp,highTempChargeCutoff | - | if externalTemp > (highTempChargeCutoff*0.8) then 1 else 0 |
| 0x35A | 42.32 | Battery Low Temp Charge Warning | UINT8 | - | BatteryLowTempChargeWarning | bits2 | Compute | externalTemp,lowTempChargeCutoff | - | if externalTemp < (lowTempChargeCutoff*1.2) then 1 else 0 |
| 0x35A | 38.317 | Battery High Current Warning | UINT8 | - | BatteryHighCurrentWarning | bits2 | Compute | packCurrent,overCurrentCutoff | - | if packCurrent > (overCurrentCutoff*0.8) then 1 else 0 |
| 0x35A | 38.317999999999998 | Battery High Charge Current Warning | UINT8 | - | BatteryHighChargeCurrentWarning | bits2 | Compute | packCurrent,overChargeCurrentCutoff | - | if packCurrent > (overChargeCurrentCutoff*0.8) then 1 else 0 |
| 0x35A | 50 | BMS Internal Warning | UINT16 | - | BMSInternalWarning | bits2 | Compute | systemStatusCode | - | if systemStatusCode == 0x9B then 1 else 0 |
| 0x35A | 51 | Need Balancing | UINT16 | - | CellImbalanceWarning | bits2 | Compute | balancingDecision | - | if balancingDecision != 0 then 1 else 0 |
| 0x35A | 50 | System Status (online/offline) | UINT16 | - | SystemStatus | bits2 | Compute | systemStatusCode | - | if systemStatusCode != 0x9B then 1 else 0 |
| 0x35E | 500 | Manufacturer Name | STRING(8) | ASCII | ManufacturerName | ASCII | Direct | - | - | Constant (8 chars, pad with 0) |
| 0x35F | 501.50200000000001 | Firmware Version | UINT16 | - | FirmwareVersion | un16 | Direct | firmwareMajor,firmwareMinor | - | firmware = (major<<8)|minor |
| 0x371 | 306 | Online Capacity | UINT16 | 0.01 Ah | OnlineCapacity | un16/Ah | Direct | reg306 | - | onlineCapacity = reg306*100 |
| 0x378 | - | Energy In | — | — | EnergyIn | un32/100Wh | Compute | packVoltage,packCurrent,Δt | - | E_in += max(packCurrent,0)*packVoltage*Δt/3600 |
| 0x378 | - | Energy Out | — | — | EnergyOut | un32/100Wh | Compute | packVoltage,packCurrent,Δt | - | E_out += max(-packCurrent,0)*packVoltage*Δt/3600 |
| 0x379 | - | Installed Capacity | UINT16 | Ah | InstalledCapacity | un16/Ah | Compute | nominalCapacity,stateOfHealth | - | installedCapacity = nominalCapacity * SOH / 100 |
| 0x382 | 502 | Battery Family | STRING(8) | ASCII | BatteryFamily | ASCII | Direct | - | - | Constant family (8 chars) |
| 0x305 | 305 | CAN Keep-Alive (Inverter→BMS) | — | — | KeepAlive | — | InputOnly | — | — | — |
| 0x307 | 307 | Identification handshake (Inv→BMS) | — | — | Handshake | — | InputOnly | — | — | — |
