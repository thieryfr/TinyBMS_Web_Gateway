/**
 * Mock BMS registers data for TinyBMS-GW
 * Simulates BMS register catalog for reading/writing
 */

let registers = [
  {
    address: 0x00,
    name: "Cell Overvoltage Protection",
    value: 3650,
    unit: "mV",
    writable: true,
    min: 3000,
    max: 4200,
    description: "Cell overvoltage protection threshold"
  },
  {
    address: 0x01,
    name: "Cell Undervoltage Protection",
    value: 2500,
    unit: "mV",
    writable: true,
    min: 2000,
    max: 3000,
    description: "Cell undervoltage protection threshold"
  },
  {
    address: 0x02,
    name: "Discharge Overcurrent Protection",
    value: 100,
    unit: "A",
    writable: true,
    min: 10,
    max: 200,
    description: "Maximum discharge current"
  },
  {
    address: 0x03,
    name: "Charge Overcurrent Protection",
    value: 50,
    unit: "A",
    writable: true,
    min: 5,
    max: 100,
    description: "Maximum charge current"
  },
  {
    address: 0x04,
    name: "Cell Count",
    value: 16,
    unit: "cells",
    writable: false,
    min: 1,
    max: 32,
    description: "Number of battery cells in series"
  },
  {
    address: 0x05,
    name: "Battery Capacity",
    value: 100,
    unit: "Ah",
    writable: true,
    min: 1,
    max: 1000,
    description: "Nominal battery capacity"
  },
  {
    address: 0x06,
    name: "Balance Start Voltage",
    value: 3400,
    unit: "mV",
    writable: true,
    min: 3000,
    max: 4000,
    description: "Cell voltage to start balancing"
  },
  {
    address: 0x07,
    name: "Balance Voltage Difference",
    value: 30,
    unit: "mV",
    writable: true,
    min: 5,
    max: 100,
    description: "Voltage difference to trigger balancing"
  },
  {
    address: 0x08,
    name: "High Temperature Protection",
    value: 55,
    unit: "°C",
    writable: true,
    min: 40,
    max: 80,
    description: "High temperature protection threshold"
  },
  {
    address: 0x09,
    name: "Low Temperature Protection",
    value: -10,
    unit: "°C",
    writable: true,
    min: -20,
    max: 10,
    description: "Low temperature protection threshold"
  },
  {
    address: 0x0A,
    name: "BMS Firmware Version",
    value: 0x0120,
    unit: "hex",
    writable: false,
    description: "BMS firmware version (v1.20)"
  },
  {
    address: 0x0B,
    name: "BMS Hardware Version",
    value: 0x0300,
    unit: "hex",
    writable: false,
    description: "BMS hardware version (v3.00)"
  },
  {
    address: 0x0C,
    name: "Cycle Count",
    value: 127,
    unit: "cycles",
    writable: false,
    description: "Number of charge/discharge cycles"
  },
  {
    address: 0x0D,
    name: "Full Charge Capacity",
    value: 98000,
    unit: "mAh",
    writable: false,
    description: "Current full charge capacity"
  },
  {
    address: 0x0E,
    name: "Remaining Capacity",
    value: 73500,
    unit: "mAh",
    writable: false,
    description: "Current remaining capacity"
  },
  {
    address: 0x0F,
    name: "MOSFET Control",
    value: 0b11,
    unit: "bits",
    writable: true,
    min: 0,
    max: 3,
    description: "MOSFET control (bit0=charge, bit1=discharge)"
  }
];

module.exports = {
  /**
   * Get all registers
   */
  getRegisters() {
    return JSON.parse(JSON.stringify(registers)); // Deep copy
  },

  /**
   * Get register by address
   */
  getRegister(address) {
    return registers.find(r => r.address === address);
  },

  /**
   * Update register value
   */
  updateRegister(address, value) {
    const register = registers.find(r => r.address === address);

    if (!register) {
      throw new Error(`Register 0x${address.toString(16)} not found`);
    }

    if (!register.writable) {
      throw new Error(`Register 0x${address.toString(16)} is read-only`);
    }

    if (register.min !== undefined && value < register.min) {
      throw new Error(`Value ${value} below minimum ${register.min}`);
    }

    if (register.max !== undefined && value > register.max) {
      throw new Error(`Value ${value} above maximum ${register.max}`);
    }

    register.value = value;
    return register;
  },

  /**
   * Batch update registers
   */
  updateRegisters(updates) {
    const results = [];
    const errors = [];

    updates.forEach(update => {
      try {
        const result = this.updateRegister(update.address, update.value);
        results.push(result);
      } catch (error) {
        errors.push({
          address: update.address,
          error: error.message
        });
      }
    });

    return {
      success: errors.length === 0,
      updated: results,
      errors: errors
    };
  }
};
