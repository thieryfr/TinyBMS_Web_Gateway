/**
 * CanSimulator
 * Génère des trames CAN inspirées des protocoles Victron/Pylontech.
 */

const CAN_IDS = [
  0x351, // Victron battery status
  0x355, // Victron alarms
  0x356, // Victron SOC
  0x35A, // Victron voltages
  0x35E, // Victron temperature
  0x370, // Generic power data
];

function randomFloat(min, max, precision = 2) {
  const value = Math.random() * (max - min) + min;
  return parseFloat(value.toFixed(precision));
}

export class CanSimulator {
  constructor() {
    this.stats = {
      framesSent: 0,
      framesReceived: 0,
      errors: 0,
    };
    this.lastFrame = null;
  }

  #buildDataForId(id, telemetry) {
    switch (id) {
      case 0x351: {
        const voltage = Math.round((telemetry?.pack_voltage_v ?? randomFloat(48, 54)) * 10);
        const current = Math.round((telemetry?.pack_current_a ?? randomFloat(-30, 30)) * 10);
        return [
          voltage & 0xff,
          (voltage >> 8) & 0xff,
          current & 0xff,
          (current >> 8) & 0xff,
          0x00,
          0x00,
          0x00,
          0x00,
        ];
      }
      case 0x356: {
        const soc = Math.round((telemetry?.state_of_charge_pct ?? randomFloat(20, 95)) * 10);
        return [
          soc & 0xff,
          (soc >> 8) & 0xff,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
        ];
      }
      case 0x35E: {
        const temp = Math.round((telemetry?.average_temperature_c ?? randomFloat(20, 40)) * 10);
        return [
          temp & 0xff,
          (temp >> 8) & 0xff,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
        ];
      }
      default:
        return Array.from({ length: 8 }, () => Math.floor(Math.random() * 256));
    }
  }

  generateFrame(telemetry = null) {
    const id = CAN_IDS[Math.floor(Math.random() * CAN_IDS.length)];
    const data = this.#buildDataForId(id, telemetry);
    const timestamp = Date.now();
    const direction = Math.random() > 0.8 ? 'RX' : 'TX';

    if (Math.random() < 0.01) {
      this.stats.errors += 1;
    }

    if (direction === 'TX') {
      this.stats.framesSent += 1;
    } else {
      this.stats.framesReceived += 1;
    }

    this.lastFrame = {
      id,
      direction,
      timestamp_ms: timestamp,
      data,
      data_hex: Buffer.from(data).toString('hex').toUpperCase(),
      dlc: data.length,
    };

    return this.lastFrame;
  }

  getStats() {
    return { ...this.stats };
  }

  getLastFrame() {
    return this.lastFrame;
  }
}

export default CanSimulator;
