import { SerialPort } from 'serialport';

const DEFAULT_BAUDRATE = 115200;
const DEFAULT_TIMEOUT_MS = 750;

class Mutex {
  constructor() {
    this._current = Promise.resolve();
  }

  runExclusive(task) {
    const run = this._current.then(() => task());
    this._current = run.then(
      () => undefined,
      () => undefined,
    );
    return run;
  }
}

function crc16(buffer) {
  let crc = 0xffff;
  for (let i = 0; i < buffer.length; i += 1) {
    crc ^= buffer[i];
    for (let bit = 0; bit < 8; bit += 1) {
      if (crc & 0x0001) {
        crc = ((crc >> 1) ^ 0xa001) & 0xffff;
      } else {
        crc = (crc >> 1) & 0xffff;
      }
    }
  }
  return crc & 0xffff;
}

function buildReadFrame(address) {
  const frame = Buffer.alloc(7);
  frame[0] = 0xaa;
  frame[1] = 0x07;
  frame[2] = 0x01;
  frame[3] = address & 0xff;
  frame[4] = (address >> 8) & 0xff;
  const crc = crc16(frame.subarray(0, 5));
  frame[5] = crc & 0xff;
  frame[6] = (crc >> 8) & 0xff;
  return frame;
}

function buildWriteFrame(address, rawValue) {
  const frame = Buffer.alloc(9);
  frame[0] = 0xaa;
  frame[1] = 0x0d;
  frame[2] = 0x04;
  frame[3] = address & 0xff;
  frame[4] = (address >> 8) & 0xff;
  frame[5] = rawValue & 0xff;
  frame[6] = (rawValue >> 8) & 0xff;
  const crc = crc16(frame.subarray(0, 7));
  frame[7] = crc & 0xff;
  frame[8] = (crc >> 8) & 0xff;
  return frame;
}

function buildRestartFrame() {
  const address = 0x0086;
  const value = 0xa55a;
  return buildWriteFrame(address, value);
}

function extractFrame(buffer) {
  if (!buffer || buffer.length === 0) {
    return { frame: null, buffer: Buffer.alloc(0) };
  }

  let working = buffer;
  const preambleIndex = working.indexOf(0xaa);
  if (preambleIndex === -1) {
    return { frame: null, buffer: Buffer.alloc(0) };
  }

  if (preambleIndex > 0) {
    working = working.slice(preambleIndex);
  }

  if (working.length < 5) {
    return { frame: null, buffer: working };
  }

  const payloadLength = working[2];
  const frameLength = 3 + payloadLength + 2;
  if (working.length < frameLength) {
    return { frame: null, buffer: working };
  }

  const frame = working.slice(0, frameLength);
  const expected = frame.readUInt16LE(frameLength - 2);
  const computed = crc16(frame.subarray(0, frameLength - 2));
  if (expected !== computed) {
    return extractFrame(working.slice(1));
  }

  return { frame, buffer: working.slice(frameLength) };
}

export async function listSerialPorts() {
  const ports = await SerialPort.list();
  return ports.map((port) => ({
    path: port.path,
    manufacturer: port.manufacturer || null,
    serialNumber: port.serialNumber || null,
    vendorId: port.vendorId || null,
    productId: port.productId || null,
  }));
}

export class TinyBmsSerial {
  constructor() {
    this.port = null;
    this._readBuffer = Buffer.alloc(0);
    this._pending = [];
    this._mutex = new Mutex();
    this._portInfo = null;
    this._onData = this._handleData.bind(this);
    this._onError = this._handleError.bind(this);
  }

  isOpen() {
    return Boolean(this.port && this.port.isOpen);
  }

  info() {
    if (!this.isOpen()) {
      return null;
    }
    return this._portInfo;
  }

  async open(path, { baudRate = DEFAULT_BAUDRATE } = {}) {
    if (!path) {
      throw new Error('Aucun port série fourni');
    }

    if (this.isOpen()) {
      await this.close();
    }

    const port = new SerialPort({
      path,
      baudRate,
      autoOpen: false,
    });

    await new Promise((resolve, reject) => {
      port.open((err) => {
        if (err) {
          reject(err);
        } else {
          resolve();
        }
      });
    });

    port.on('data', this._onData);
    port.on('error', this._onError);

    this.port = port;
    this._readBuffer = Buffer.alloc(0);
    this._pending = [];
    this._portInfo = { path, baudRate };
    return this._portInfo;
  }

  async close() {
    if (!this.port) {
      return;
    }

    await new Promise((resolve) => {
      this.port.removeListener('data', this._onData);
      this.port.removeListener('error', this._onError);
      this.port.close(() => resolve());
    });

    this._rejectAll(new Error('Port série fermé'));
    this.port = null;
    this._portInfo = null;
    this._readBuffer = Buffer.alloc(0);
  }

  async readRegister(address, timeoutMs = DEFAULT_TIMEOUT_MS) {
    return this._mutex.runExclusive(() => this._readRegisterLocked(address, timeoutMs));
  }

  async writeRegister(address, rawValue, timeoutMs = DEFAULT_TIMEOUT_MS) {
    return this._mutex.runExclusive(() => this._writeRegisterLocked(address, rawValue, timeoutMs));
  }

  async restartTinyBms(timeoutMs = DEFAULT_TIMEOUT_MS) {
    return this._mutex.runExclusive(async () => {
      await this._prepareTransaction();
      const frame = buildRestartFrame();
      const ackPromise = this._waitForFrame((received) => received[1] === 0x01 || received[1] === 0x81, timeoutMs);
      try {
        await this._writeFrame(frame);
      } catch (error) {
        if (typeof ackPromise.cancel === 'function') {
          ackPromise.cancel();
        }
        throw error;
      }
      const ack = await ackPromise;
      if (ack[1] === 0x81) {
        throw new Error('TinyBMS a renvoyé un NACK pour la commande de redémarrage');
      }
      return true;
    });
  }

  async readCatalogue(descriptors, timeoutMs = DEFAULT_TIMEOUT_MS) {
    return this._mutex.runExclusive(async () => {
      const results = [];
      for (const descriptor of descriptors) {
        const raw = await this._readRegisterLocked(descriptor.address, timeoutMs);
        results.push({ descriptor, raw });
      }
      return results;
    });
  }

  async _readRegisterLocked(address, timeoutMs) {
    await this._prepareTransaction();
    const request = buildReadFrame(address);
    const responsePromise = this._waitForFrame((frame) => frame[1] === 0x07, timeoutMs);
    try {
      await this._writeFrame(request);
    } catch (error) {
      if (typeof responsePromise.cancel === 'function') {
        responsePromise.cancel();
      }
      throw error;
    }
    const frame = await responsePromise;
    if (frame.length < 5 || frame[2] < 2) {
      throw new Error('Réponse TinyBMS invalide');
    }
    const raw = frame[3] | (frame[4] << 8);
    return raw;
  }

  async _writeRegisterLocked(address, rawValue, timeoutMs) {
    await this._prepareTransaction();
    const frame = buildWriteFrame(address, rawValue);
    const ackPromise = this._waitForFrame((received) => received[1] === 0x01 || received[1] === 0x81, timeoutMs);
    try {
      await this._writeFrame(frame);
    } catch (error) {
      if (typeof ackPromise.cancel === 'function') {
        ackPromise.cancel();
      }
      throw error;
    }
    const ack = await ackPromise;
    if (ack[1] === 0x81) {
      const errorCode = ack.length > 3 ? ack[3] : 0;
      throw new Error(`TinyBMS NACK (code 0x${errorCode.toString(16).padStart(2, '0')})`);
    }
    const readback = await this._readRegisterLocked(address, timeoutMs);
    return readback;
  }

  async _prepareTransaction() {
    if (!this.isOpen()) {
      throw new Error('Port série non connecté');
    }
    this._readBuffer = Buffer.alloc(0);
    await new Promise((resolve, reject) => {
      this.port.flush((err) => {
        if (err) {
          reject(err);
        } else {
          resolve();
        }
      });
    });
  }

  async _writeFrame(frame) {
    if (!this.isOpen()) {
      throw new Error('Port série non connecté');
    }
    await new Promise((resolve, reject) => {
      this.port.write(frame, (err) => {
        if (err) {
          reject(err);
        } else {
          resolve();
        }
      });
    });
    await new Promise((resolve, reject) => {
      this.port.drain((err) => {
        if (err) {
          reject(err);
        } else {
          resolve();
        }
      });
    });
  }

  _waitForFrame(matcher, timeoutMs) {
    let pendingRef = null;
    const promise = new Promise((resolve, reject) => {
      const pending = {
        matcher,
        resolve: (frame) => {
          clearTimeout(pending.timeoutHandle);
          resolve(frame);
        },
        reject: (error) => {
          clearTimeout(pending.timeoutHandle);
          reject(error);
        },
        timeoutHandle: null,
      };

      pending.timeoutHandle = setTimeout(() => {
        this._removePending(pending);
        reject(new Error('Timeout de réponse TinyBMS'));
      }, timeoutMs);

      pendingRef = pending;
      this._pending.push(pending);
    });

    promise.cancel = () => {
      if (pendingRef) {
        clearTimeout(pendingRef.timeoutHandle);
        this._removePending(pendingRef);
        pendingRef = null;
      }
    };

    return promise;
  }

  _handleData(data) {
    this._readBuffer = Buffer.concat([this._readBuffer, data]);
    let parsing = true;
    while (parsing) {
      const { frame, buffer } = extractFrame(this._readBuffer);
      this._readBuffer = buffer;
      if (!frame) {
        parsing = false;
        break;
      }
      this._dispatchFrame(frame);
    }
  }

  _handleError(error) {
    this._rejectAll(error);
  }

  _dispatchFrame(frame) {
    if (this._pending.length === 0) {
      return;
    }
    for (let i = 0; i < this._pending.length; i += 1) {
      const entry = this._pending[i];
      let matches = false;
      try {
        matches = Boolean(entry.matcher(frame));
      } catch (error) {
        entry.reject(error);
        this._pending.splice(i, 1);
        return;
      }
      if (matches) {
        this._pending.splice(i, 1);
        entry.resolve(frame);
        return;
      }
    }
  }

  _removePending(pending) {
    const index = this._pending.indexOf(pending);
    if (index >= 0) {
      this._pending.splice(index, 1);
    }
  }

  _rejectAll(error) {
    while (this._pending.length > 0) {
      const pending = this._pending.shift();
      if (pending) {
        pending.reject(error);
      }
    }
  }
}

export default TinyBmsSerial;
