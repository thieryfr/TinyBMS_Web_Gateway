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
