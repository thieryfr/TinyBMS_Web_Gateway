import express from 'express';
import path from 'path';
import { fileURLToPath } from 'url';

import {
  getRegisterCatalogue,
  findRegisterDescriptorByKey,
  rawToUserValue,
  userToRawValue,
} from './registers.js';
import TinyBmsSerial, { listSerialPorts } from './serial.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, '..', '..');
const publicDir = path.join(__dirname, '..', 'public');
const tinyComponentsDir = path.join(repoRoot, 'web', 'src', 'components', 'tiny');

const app = express();
const serial = new TinyBmsSerial();
const catalogue = getRegisterCatalogue();

const PORT = Number.parseInt(process.env.MAC_LOCAL_PORT || '5173', 10);

app.use(express.json({ limit: '1mb' }));
app.use(express.static(publicDir));
app.use('/assets/tiny', express.static(tinyComponentsDir));

function ensureConnected() {
  if (!serial.isOpen()) {
    const error = new Error('Port série non connecté');
    error.statusCode = 503;
    throw error;
  }
}

app.get('/api/ports', async (req, res, next) => {
  try {
    const ports = await listSerialPorts();
    res.json({ ports });
  } catch (error) {
    next(error);
  }
});

app.get('/api/connection/status', (req, res) => {
  res.json({ connected: serial.isOpen(), port: serial.info() });
});

app.post('/api/connection/open', async (req, res, next) => {
  try {
    const { path: portPath, baudRate } = req.body || {};
    if (!portPath) {
      const error = new Error('Champ "path" manquant');
      error.statusCode = 400;
      throw error;
    }
    const info = await serial.open(portPath, { baudRate });
    res.json({ connected: true, port: info });
  } catch (error) {
    next(error);
  }
});

app.post('/api/connection/close', async (req, res, next) => {
  try {
    await serial.close();
    res.json({ connected: false });
  } catch (error) {
    next(error);
  }
});

app.get('/api/registers', async (req, res, next) => {
  try {
    ensureConnected();
    const { group } = req.query;
    const filteredCatalogue = group
      ? catalogue.filter((descriptor) => descriptor.group === group)
      : catalogue;
    const results = await serial.readCatalogue(filteredCatalogue);
    const registers = results.map(({ descriptor, raw }) => {
      const value = rawToUserValue(descriptor, raw);
      return {
        key: descriptor.key,
        label: descriptor.label,
        unit: descriptor.unit,
        group: descriptor.group,
        type: descriptor.type,
        access: descriptor.access,
        comment: descriptor.comment,
        address: descriptor.address,
        address_hex: descriptor.addressHex,
        scale: descriptor.scale,
        precision: descriptor.precision,
        raw,
        value,
        current_user_value: value,
        default: descriptor.defaultUser,
        min: descriptor.minUser,
        max: descriptor.maxUser,
        step: descriptor.stepUser,
        enum: descriptor.enum,
      };
    });
    res.json({ total: registers.length, registers });
  } catch (error) {
    next(error);
  }
});

app.post('/api/registers', async (req, res, next) => {
  try {
    ensureConnected();
    const { key, value } = req.body || {};
    if (!key || typeof value === 'undefined') {
      const error = new Error('Payload invalide: { key, value } requis');
      error.statusCode = 400;
      throw error;
    }

    const descriptor = findRegisterDescriptorByKey(key);
    if (!descriptor) {
      const error = new Error(`Registre inconnu: ${key}`);
      error.statusCode = 404;
      throw error;
    }

    const numericValue = typeof value === 'number' ? value : Number(value);
    if (Number.isNaN(numericValue)) {
      const error = new Error(`Valeur numérique attendue pour ${key}`);
      error.statusCode = 400;
      throw error;
    }

    const rawValue = userToRawValue(descriptor, numericValue);
    const readback = await serial.writeRegister(descriptor.address, rawValue);
    const userValue = rawToUserValue(descriptor, readback);

    res.json({
      status: 'updated',
      key,
      address: descriptor.address,
      raw: readback,
      value: userValue,
      current_user_value: userValue,
    });
  } catch (error) {
    next(error);
  }
});

app.post('/api/system/restart', async (req, res, next) => {
  try {
    ensureConnected();
    await serial.restartTinyBms();
    res.json({ status: 'restarting' });
  } catch (error) {
    next(error);
  }
});

app.post('/api/ota', (req, res) => {
  res.status(501).json({ error: 'Mise à jour OTA non supportée via l’interface Mac locale.' });
});

app.use('/api', (err, req, res, next) => {
  const status = err.statusCode || 500;
  res.status(status).json({
    error: err.message || 'Erreur interne',
  });
});

app.get('*', (req, res) => {
  res.sendFile(path.join(publicDir, 'index.html'));
});

app.listen(PORT, () => {
  /* eslint-disable no-console */
  console.log(`TinyBMS local UI ready on http://localhost:${PORT}`);
  /* eslint-enable no-console */
});

process.on('SIGINT', async () => {
  if (serial.isOpen()) {
    await serial.close();
  }
  process.exit(0);
});
