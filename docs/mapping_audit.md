# TinyBMS UART ↔ CAN mapping audit

## Overview

- excel: 68 fields
- json: 67 fields

## Potential issues

### Scale mismatch
- UART reg 50 @ CAN 0x35A bytes 7 bits 2-3: scale mismatch Excel=None JSON='1'
- UART reg 52 @ CAN 0x35A bytes 3 bits 0-1: scale mismatch Excel=None JSON='1'
- UART reg 500 @ CAN 0x35E bytes 0-7: scale mismatch Excel=None JSON='1'
- UART reg 502 @ CAN 0x382 bytes 0-7: scale mismatch Excel=None JSON='1'
- UART reg 504 @ CAN 0x380 bytes 0-7: scale mismatch Excel=None JSON='1'
- UART reg 505 @ CAN 0x381 bytes 0-7: scale mismatch Excel=None JSON='1'

### Unit mismatch
- UART reg 50 @ CAN 0x35A bytes 7 bits 2-3: unit mismatch Excel=None JSON='enum'
- UART reg 52 @ CAN 0x35A bytes 3 bits 0-1: unit mismatch Excel=None JSON='enum'
- UART reg 500 @ CAN 0x35E bytes 0-7: unit mismatch Excel=None JSON='string'
- UART reg 502 @ CAN 0x382 bytes 0-7: unit mismatch Excel=None JSON='string'
- UART reg 504 @ CAN 0x380 bytes 0-7: unit mismatch Excel=None JSON='string'
- UART reg 505 @ CAN 0x381 bytes 0-7: unit mismatch Excel=None JSON='string'

### Validation notes
- UART registers 50 and 52 now rely on the consolidated **enum** encoder that
  normalizes both the scale (fixed to `1`) and the unit metadata. The Excel
  workbook has not yet been updated with the encoder metadata, so the unit and
  scale fields appear missing in that source. JSON values are correct.
- UART registers 500, 502, 504 and 505 use the new **string** encoder. Their
  JSON definitions carry the expected `scale=1` and `unit=string` annotations
  while the spreadsheet still leaves these columns blank. No functional
  mismatch was detected after inspecting the generated CAN frames; the
  differences originate solely from documentation lag.
