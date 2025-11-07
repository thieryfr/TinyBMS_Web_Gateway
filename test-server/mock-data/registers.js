/**
 * Mock BMS registers data for TinyBMS-GW
 * Simulates BMS register catalog for reading/writing
 */

let registers = [
  // Legacy registers (0x00 - 0x0F)
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
  },

  // TinyBMS Settings Registers (300-343)
  {
    address: 300,
    name: "Fully Charged Voltage",
    value: 3700,
    unit: "mV",
    writable: true,
    min: 1200,
    max: 4500,
    description: "Tension de charge complète par cellule"
  },
  {
    address: 301,
    name: "Fully Discharged Voltage",
    value: 3000,
    unit: "mV",
    writable: true,
    min: 1000,
    max: 3500,
    description: "Tension de décharge complète par cellule"
  },
  {
    address: 303,
    name: "Early Balancing Threshold",
    value: 3400,
    unit: "mV",
    writable: true,
    min: 1000,
    max: 4500,
    description: "Seuil de début d'équilibrage"
  },
  {
    address: 304,
    name: "Charge Finished Current",
    value: 1000,
    unit: "mA",
    writable: true,
    min: 100,
    max: 5000,
    description: "Courant de fin de charge"
  },
  {
    address: 305,
    name: "Peak Discharge Current Cutoff",
    value: 100,
    unit: "A",
    writable: true,
    min: 1,
    max: 750,
    description: "Seuil de coupure courant de décharge crête"
  },
  {
    address: 306,
    name: "Battery Capacity",
    value: 10000,
    unit: "0.01Ah",
    writable: true,
    min: 10,
    max: 65500,
    description: "Capacité de la batterie"
  },
  {
    address: 307,
    name: "Number Of Series Cells",
    value: 13,
    unit: "cells",
    writable: true,
    min: 4,
    max: 16,
    description: "Nombre de cellules en série"
  },
  {
    address: 308,
    name: "Allowed Disbalance",
    value: 30,
    unit: "mV",
    writable: true,
    min: 15,
    max: 100,
    description: "Déséquilibre autorisé entre cellules"
  },
  {
    address: 310,
    name: "Charger Startup Delay",
    value: 20,
    unit: "s",
    writable: true,
    min: 5,
    max: 60,
    description: "Délai de démarrage du chargeur"
  },
  {
    address: 311,
    name: "Charger Disable Delay",
    value: 5,
    unit: "s",
    writable: true,
    min: 0,
    max: 60,
    description: "Délai de désactivation du chargeur"
  },
  {
    address: 312,
    name: "Pulses Per Unit (LSB)",
    value: 1000,
    unit: "pulses",
    writable: true,
    min: 1,
    max: 100000,
    description: "Impulsions par unité (partie basse)"
  },
  {
    address: 313,
    name: "Pulses Per Unit (MSB)",
    value: 0,
    unit: "pulses",
    writable: true,
    min: 0,
    max: 100000,
    description: "Impulsions par unité (partie haute)"
  },
  {
    address: 314,
    name: "Distance Unit Name",
    value: 2,
    unit: "enum",
    writable: true,
    min: 1,
    max: 5,
    description: "Unité de distance (1=Meter, 2=Kilometer, 3=Feet, 4=Mile, 5=Yard)"
  },
  {
    address: 315,
    name: "Over-Voltage Cutoff",
    value: 4200,
    unit: "mV",
    writable: true,
    min: 1200,
    max: 4500,
    description: "Seuil de coupure surtension"
  },
  {
    address: 316,
    name: "Under-Voltage Cutoff",
    value: 2800,
    unit: "mV",
    writable: true,
    min: 800,
    max: 3500,
    description: "Seuil de coupure sous-tension"
  },
  {
    address: 317,
    name: "Discharge Over-Current Cutoff",
    value: 60,
    unit: "A",
    writable: true,
    min: 1,
    max: 750,
    description: "Seuil de coupure surintensité décharge"
  },
  {
    address: 318,
    name: "Charge Over-Current Cutoff",
    value: 20,
    unit: "A",
    writable: true,
    min: 1,
    max: 750,
    description: "Seuil de coupure surintensité charge"
  },
  {
    address: 319,
    name: "Over-Heat Cutoff",
    value: 60,
    unit: "°C",
    writable: true,
    min: 20,
    max: 90,
    description: "Seuil de coupure surchauffe"
  },
  {
    address: 320,
    name: "Low Temperature Charger Cutoff",
    value: 0,
    unit: "°C",
    writable: true,
    min: -40,
    max: 10,
    description: "Seuil de coupure basse température charge"
  },
  {
    address: 321,
    name: "Charge Restart Level",
    value: 90,
    unit: "%",
    writable: true,
    min: 60,
    max: 95,
    description: "Niveau de redémarrage de la charge"
  },
  {
    address: 322,
    name: "Battery Maximum Cycles Count",
    value: 3000,
    unit: "cycles",
    writable: true,
    min: 10,
    max: 65000,
    description: "Nombre maximum de cycles batterie"
  },
  {
    address: 323,
    name: "State Of Health",
    value: 50000,
    unit: "0.002%",
    writable: true,
    min: 0,
    max: 50000,
    description: "État de santé de la batterie"
  },
  {
    address: 328,
    name: "State Of Charge",
    value: 37500,
    unit: "0.002%",
    writable: true,
    min: 0,
    max: 50000,
    description: "État de charge de la batterie"
  },
  {
    address: 329,
    name: "Flags Register",
    value: 0,
    unit: "flags",
    writable: true,
    min: 0,
    max: 7,
    description: "Registre de flags (bit 0: Invert Current Sensor, bit 1: Disable Diagnostics, bit 2: Enable Restart Level)"
  },
  {
    address: 330,
    name: "Charger Type & Discharge Timeout",
    value: 1,
    unit: "enum",
    writable: true,
    min: 0,
    max: 2,
    description: "Type de chargeur (0=Variable, 1=CC/CV, 2=CAN)"
  },
  {
    address: 331,
    name: "Load Switch Type",
    value: 0,
    unit: "enum",
    writable: true,
    min: 0,
    max: 8,
    description: "Type de switch de charge (0=FET, 1=AIDO1, 2=AIDO2, etc.)"
  },
  {
    address: 332,
    name: "Automatic Recovery",
    value: 5,
    unit: "s",
    writable: true,
    min: 1,
    max: 30,
    description: "Récupération automatique"
  },
  {
    address: 333,
    name: "Charger Switch Type",
    value: 1,
    unit: "enum",
    writable: true,
    min: 1,
    max: 9,
    description: "Type de switch du chargeur (1=Charge FET, 2=AIDO1, etc.)"
  },
  {
    address: 334,
    name: "Ignition",
    value: 0,
    unit: "enum",
    writable: true,
    min: 0,
    max: 6,
    description: "Configuration de l'ignition (0=Disabled, 1=AIDO1, etc.)"
  },
  {
    address: 335,
    name: "Charger Detection",
    value: 1,
    unit: "enum",
    writable: true,
    min: 1,
    max: 7,
    description: "Détection du chargeur (1=Internal, 2=AIDO1, etc.)"
  },
  {
    address: 336,
    name: "Speed Sensor Input",
    value: 0,
    unit: "enum",
    writable: true,
    min: 0,
    max: 2,
    description: "Entrée du capteur de vitesse (0=Disabled, 1=DIDO1, 2=DIDO2)"
  },
  {
    address: 337,
    name: "Precharge Pin",
    value: 0,
    unit: "enum",
    writable: true,
    min: 0,
    max: 10,
    description: "Pin de précharge (0=Disabled, 2=Discharge FET, etc.)"
  },
  {
    address: 338,
    name: "Precharge Duration",
    value: 3,
    unit: "enum",
    writable: true,
    min: 0,
    max: 7,
    description: "Durée de précharge (0=0.1s, 1=0.2s, 2=0.5s, 3=1s, etc.)"
  },
  {
    address: 339,
    name: "Temperature Sensor Type",
    value: 0,
    unit: "enum",
    writable: true,
    min: 0,
    max: 1,
    description: "Type de capteur de température (0=Dual 10K NTC, 1=Multipoint Active Sensor)"
  },
  {
    address: 340,
    name: "BMS Operation Mode",
    value: 0,
    unit: "enum",
    writable: true,
    min: 0,
    max: 1,
    description: "Mode d'opération du BMS (0=Dual Port, 1=Single Port)"
  },
  {
    address: 341,
    name: "Single Port Switch Type",
    value: 0,
    unit: "enum",
    writable: true,
    min: 0,
    max: 8,
    description: "Type de switch en mode single port"
  },
  {
    address: 342,
    name: "Broadcast Time",
    value: 0,
    unit: "enum",
    writable: true,
    min: 0,
    max: 7,
    description: "Intervalle de diffusion (0=Disabled, 1=0.1s, 2=0.2s, etc.)"
  },
  {
    address: 343,
    name: "Protocol",
    value: 0,
    unit: "enum",
    writable: true,
    min: 0,
    max: 2,
    description: "Protocole de diffusion (0=CA V3, 1=ASCII, 2=SOC BAR)"
  }
];

module.exports = {
  /**
   * Get all registers in the format expected by the UI
   */
  getRegisters() {
    // Convert to UI format with current_user_value
    return registers.map(r => ({
      address: r.address,
      name: r.name,
      current_user_value: r.value,
      unit: r.unit,
      writable: r.writable,
      min_value: r.min,
      max_value: r.max,
      has_min: r.min !== undefined,
      has_max: r.max !== undefined,
      description: r.description,
      access: r.writable ? 'rw' : 'ro'
    }));
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
