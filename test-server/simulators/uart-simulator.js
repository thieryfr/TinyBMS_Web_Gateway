/**
 * UartSimulator
 * Génère des trames UART TinyBMS/Modbus réalistes.
 */

const COMMANDS = [0x03, 0x04, 0x10, 0x41, 0x42, 0x50];
const DIRECTIONS = ['TX', 'RX'];

function crc16Modbus(buffer) {
  let crc = 0xffff;

  for (let pos = 0; pos < buffer.length; pos += 1) {
    crc ^= buffer[pos];

    for (let i = 8; i !== 0; i -= 1) {
      if ((crc & 0x0001) !== 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}

function toHex(buffer) {
  return Buffer.from(buffer).toString('hex').toUpperCase();
}

export class UartSimulator {
  constructor() {
    this.sequence = 0;
    this.stats = {
      framesSent: 0,
      framesReceived: 0,
      checksumErrors: 0,
    };
    this.lastFrame = null;
  }

  #buildPayloadFromTelemetry(telemetry) {
    if (!telemetry) {
      return [0x00, 0x00, 0x00, 0x00];
    }

    const voltage = Math.round(telemetry.pack_voltage_v * 10);
    const current = Math.round(telemetry.pack_current_a * 10);
    const soc = Math.round(telemetry.state_of_charge_pct * 10);

    return [
      (voltage >> 8) & 0xff,
      voltage & 0xff,
      (current >> 8) & 0xff,
      current & 0xff,
      (soc >> 8) & 0xff,
      soc & 0xff,
    ];
  }

  generateFrame(telemetry = null) {
    const direction = DIRECTIONS[Math.random() < 0.7 ? 0 : 1];
    const address = 0x01;
    const command = COMMANDS[Math.floor(Math.random() * COMMANDS.length)];
    const payload = this.#buildPayloadFromTelemetry(telemetry);

    const frame = [address, command, payload.length, ...payload];
    const crc = crc16Modbus(frame);
    frame.push(crc & 0xff, (crc >> 8) & 0xff);

    const timestamp = Date.now();
    const crcValid = Math.random() > 0.02;
    if (!crcValid) {
      frame[frame.length - 1] ^= 0xff; // corruption volontaire
      this.stats.checksumErrors += 1;
    }

    const raw = toHex(frame);

    const decoded = {
      sequence: this.sequence,
      direction,
      timestamp_ms: timestamp,
      address,
      command,
      payload_length: payload.length,
      payload_hex: toHex(payload),
      crc_valid: crcValid,
    };

    this.sequence += 1;
    if (direction === 'TX') {
      this.stats.framesSent += 1;
    } else {
      this.stats.framesReceived += 1;
    }

    this.lastFrame = {
      raw,
      decoded,
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

export default UartSimulator;
