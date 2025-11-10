/**
 * RegistersManager
 * Maintient un ensemble de registres simulés avec lecture/écriture.
 */

function createRegister(address, name, value, writable = true, unit = '', category = 'general') {
  return {
    address,
    name,
    value,
    writable,
    unit,
    category,
    updated_at_ms: Date.now(),
  };
}

export class RegistersManager {
  constructor() {
    this.registers = this.#initializeRegisters();
  }

  #initializeRegisters() {
    const now = Date.now();

    const blocks = {
      system: [
        createRegister(0x0001, 'Device Model', 'TinyBMS-GW', false, '', 'system'),
        createRegister(0x0002, 'Firmware Version', '2.0.0', false, '', 'system'),
        createRegister(0x0003, 'Uptime Seconds', 0, false, 's', 'system'),
        createRegister(0x0004, 'Reset Reason', 1, false, '', 'system'),
      ],
      battery: [
        createRegister(0x0100, 'Pack Voltage', 52.4, false, 'V', 'battery'),
        createRegister(0x0101, 'Pack Current', -12.4, false, 'A', 'battery'),
        createRegister(0x0102, 'State of Charge', 78.5, false, '%', 'battery'),
        createRegister(0x0103, 'State of Health', 96.2, false, '%', 'battery'),
        createRegister(0x0104, 'Cycle Count', 128, false, '', 'battery'),
      ],
      protection: [
        createRegister(0x0200, 'Charge Enable', 1, true, '', 'protection'),
        createRegister(0x0201, 'Discharge Enable', 1, true, '', 'protection'),
        createRegister(0x0202, 'Balance Enable', 1, true, '', 'protection'),
        createRegister(0x0203, 'High Voltage Cutoff', 3.65, true, 'V', 'protection'),
        createRegister(0x0204, 'Low Voltage Cutoff', 2.5, true, 'V', 'protection'),
      ],
      communication: [
        createRegister(0x0300, 'UART Baudrate', 115200, true, 'bps', 'communication'),
        createRegister(0x0301, 'CAN Bitrate', 500000, true, 'bps', 'communication'),
        createRegister(0x0302, 'MQTT Enabled', 1, true, '', 'communication'),
        createRegister(0x0303, 'Web Auth Enabled', 0, true, '', 'communication'),
      ],
      calibration: [
        createRegister(0x0400, 'Voltage Gain', 1.002, true, '', 'calibration'),
        createRegister(0x0401, 'Current Gain', 0.998, true, '', 'calibration'),
        createRegister(0x0402, 'Temperature Offset', -0.2, true, '°C', 'calibration'),
      ],
    };

    Object.values(blocks).forEach((list) => {
      list.forEach((register) => {
        register.updated_at_ms = now;
      });
    });

    return blocks;
  }

  /**
   * Retourne l'ensemble des registres ou un groupe spécifique.
   */
  getRegisters(category = null) {
    if (!category) {
      return this.registers;
    }

    return this.registers[category] || [];
  }

  /**
   * Met à jour un tableau de registres.
   */
  updateRegisters(updates = []) {
    if (!Array.isArray(updates)) {
      throw new Error('Invalid registers payload');
    }

    const updated = [];

    updates.forEach((update) => {
      const { address, value } = update;
      const register = this.#findRegister(address);

      if (!register) {
        return;
      }

      if (!register.writable) {
        throw new Error(`Register 0x${address.toString(16)} is read-only`);
      }

      register.value = value;
      register.updated_at_ms = Date.now();
      updated.push({ address, value });
    });

    return {
      success: true,
      updated,
      count: updated.length,
    };
  }

  /**
   * Exporte la table complète des registres.
   */
  exportRegisters() {
    return this.registers;
  }

  /**
   * Importe un ensemble de registres sérialisés.
   */
  importRegisters(serializedRegisters) {
    if (typeof serializedRegisters !== 'object' || serializedRegisters === null) {
      throw new Error('Invalid register dump');
    }

    let imported = 0;

    Object.entries(serializedRegisters).forEach(([category, list]) => {
      if (!Array.isArray(list)) {
        return;
      }

      if (!this.registers[category]) {
        this.registers[category] = [];
      }

      list.forEach((incoming) => {
        const existing = this.#findRegister(incoming.address);

        if (existing) {
          existing.value = incoming.value;
          existing.updated_at_ms = Date.now();
        } else {
          this.registers[category].push({ ...incoming, updated_at_ms: Date.now() });
        }

        imported += 1;
      });
    });

    return { imported };
  }

  /**
   * Nombre total de registres.
   */
  getRegisterCount() {
    return Object.values(this.registers).reduce((sum, group) => sum + group.length, 0);
  }

  /**
   * Nombre de registres modifiables.
   */
  getWritableCount() {
    return Object.values(this.registers)
      .flat()
      .filter((register) => register.writable)
      .length;
  }

  /**
   * Recherche un registre par adresse.
   */
  #findRegister(address) {
    const groups = Object.values(this.registers);

    for (const group of groups) {
      const match = group.find((register) => register.address === address);
      if (match) {
        return match;
      }
    }

    return null;
  }
}

export default RegistersManager;
