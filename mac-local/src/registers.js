import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, '..', '..');
const generatedFilePath = path.join(
  repoRoot,
  'main',
  'config_manager',
  'generated_tiny_rw_registers.inc',
);
const bundledCataloguePath = path.join(__dirname, '..', 'data', 'registers.json');

let cachedCatalogue = null;

function readGeneratedFile() {
  return fs.readFileSync(generatedFilePath, 'utf8');
}

function loadBundledCatalogue() {
  try {
    const contents = fs.readFileSync(bundledCataloguePath, 'utf8');
    const data = JSON.parse(contents);
    if (!Array.isArray(data) || data.length === 0) {
      return null;
    }
    return data;
  } catch (error) {
    return null;
  }
}

function parseEnumTables(source) {
  const enumRegex = /static const config_manager_enum_entry_t s_enum_options_(\d+)\[\] = \{([\s\S]*?)\};/g;
  const enums = new Map();
  let match;
  while ((match = enumRegex.exec(source)) !== null) {
    const registerNumber = Number.parseInt(match[1], 10);
    const tableBody = match[2];
    const entryRegex = /\{\s*\.value\s*=\s*([^,]+),\s*\.label\s*=\s*"([^"]*)"\s*\}/g;
    const entries = [];
    let entryMatch;
    while ((entryMatch = entryRegex.exec(tableBody)) !== null) {
      const valueLiteral = entryMatch[1].trim();
      const numericValue = Number.parseInt(valueLiteral.replace(/u$/, ''), 10);
      entries.push({ value: numericValue, label: entryMatch[2] });
    }
    enums.set(registerNumber, entries);
  }
  return enums;
}

function extractDescriptorBlocks(source) {
  const sectionMatch = source.match(
    /static const config_manager_register_descriptor_t s_register_descriptors\[\] = \{([\s\S]*?)\};/,
  );
  if (!sectionMatch) {
    throw new Error('Impossible de trouver la section des descripteurs de registres.');
  }
  const content = sectionMatch[1];
  const blocks = [];
  let depth = 0;
  let current = '';
  for (const char of content) {
    if (char === '{') {
      if (depth === 0) {
        current = '';
      } else {
        current += char;
      }
      depth += 1;
      continue;
    }
    if (char === '}') {
      depth -= 1;
      if (depth === 0) {
        blocks.push(current.trim());
        current = '';
        continue;
      }
      if (depth > 0) {
        current += char;
      }
      continue;
    }
    if (depth > 0) {
      current += char;
    }
  }
  return blocks;
}

function parseLiteral(value) {
  if (value === undefined) {
    return undefined;
  }
  const trimmed = value.trim();
  if (trimmed === 'NULL') {
    return null;
  }
  if (trimmed === 'true' || trimmed === 'false') {
    return trimmed === 'true';
  }
  if (trimmed.startsWith('"') && trimmed.endsWith('"')) {
    return JSON.parse(trimmed);
  }
  if (/^CONFIG_MANAGER_ACCESS_/.test(trimmed)) {
    return trimmed.split('_').pop().toLowerCase();
  }
  if (/^CONFIG_MANAGER_VALUE_/.test(trimmed)) {
    return trimmed.split('_').pop().toLowerCase();
  }
  if (/^s_enum_options_/.test(trimmed)) {
    const enumId = Number.parseInt(trimmed.replace(/\D/g, ''), 10);
    return { enumRef: enumId };
  }
  if (/^0x[0-9A-Fa-f]+u?$/.test(trimmed)) {
    return Number.parseInt(trimmed.replace(/u$/, ''), 16);
  }
  if (/^-?\d+u?$/.test(trimmed)) {
    return Number.parseInt(trimmed.replace(/u$/, ''), 10);
  }
  if (/^-?\d+\.\d+f?$/.test(trimmed)) {
    return Number.parseFloat(trimmed.replace(/f$/, ''));
  }
  return trimmed;
}

function parseDescriptorBlock(block, enums) {
  const descriptor = {};
  const lineRegex = /\.([a-z_]+)\s*=\s*(.+?),\s*$/gm;
  let match;
  while ((match = lineRegex.exec(block)) !== null) {
    const key = match[1];
    const rawValue = match[2];
    const parsed = parseLiteral(rawValue);
    descriptor[key] = parsed;
  }

  if (descriptor.address === undefined) {
    throw new Error('Descripteur de registre sans adresse détecté.');
  }

  const address = Number(descriptor.address);
  const registerNumber = address;

  const valueClass = descriptor.value_class === 'enum' ? 'enum' : 'numeric';
  let enumValues = [];
  if (descriptor.enum_values && typeof descriptor.enum_values === 'object') {
    const ref = descriptor.enum_values.enumRef;
    enumValues = enums.get(ref) || [];
  }

  const scale = typeof descriptor.scale === 'number' ? descriptor.scale : 1;
  const precision = typeof descriptor.precision === 'number' ? descriptor.precision : 0;
  const stepRaw = typeof descriptor.step_raw === 'number' ? descriptor.step_raw : 0;
  const minRaw = typeof descriptor.min_raw === 'number' ? descriptor.min_raw : null;
  const maxRaw = typeof descriptor.max_raw === 'number' ? descriptor.max_raw : null;
  const defaultRaw = typeof descriptor.default_raw === 'number' ? descriptor.default_raw : 0;

  const hasMin = Boolean(descriptor.has_min);
  const hasMax = Boolean(descriptor.has_max);

  const rawToUser = (raw) => {
    if (valueClass === 'enum') {
      return raw;
    }
    return raw * scale;
  };

  const roundValue = (value) => {
    if (valueClass === 'enum') {
      return value;
    }
    const factor = 10 ** precision;
    return factor > 0 ? Math.round(value * factor) / factor : value;
  };

  const descriptorObject = {
    address: registerNumber,
    addressHex: `0x${address.toString(16).padStart(4, '0')}`,
    key: descriptor.key || `reg${registerNumber}`,
    label: descriptor.label || descriptor.key || `Register ${registerNumber}`,
    unit: descriptor.unit || '',
    group: descriptor.group || 'general',
    comment: descriptor.comment || '',
    type: descriptor.type || 'uint16',
    access: descriptor.access || 'ro',
    scale,
    precision,
    hasMin,
    hasMax,
    minRaw,
    maxRaw,
    stepRaw,
    defaultRaw,
    valueClass,
    enum: enumValues,
  };

  const minUser = hasMin && valueClass !== 'enum' && minRaw !== null ? roundValue(rawToUser(minRaw)) : null;
  const maxUser = hasMax && valueClass !== 'enum' && maxRaw !== null ? roundValue(rawToUser(maxRaw)) : null;
  const defaultUser = valueClass === 'enum' ? defaultRaw : roundValue(rawToUser(defaultRaw));
  const stepUser = valueClass === 'enum' ? null : roundValue(stepRaw * scale);

  return {
    ...descriptorObject,
    minUser,
    maxUser,
    defaultUser,
    stepUser,
  };
}

function buildCatalogueFromGeneratedSource() {
  if (!fs.existsSync(generatedFilePath)) {
    throw new Error(
      "Impossible de charger le catalogue des registres depuis le firmware (fichier 'main/config_manager/generated_tiny_rw_registers.inc' introuvable).",
    );
  }
  const source = readGeneratedFile();
  const enums = parseEnumTables(source);
  const blocks = extractDescriptorBlocks(source);
  return blocks.map((block) => parseDescriptorBlock(block, enums));
}

function buildCatalogue() {
  const bundled = loadBundledCatalogue();
  if (bundled) {
    return bundled;
  }
  return buildCatalogueFromGeneratedSource();
}

export function getRegisterCatalogue() {
  if (!cachedCatalogue) {
    cachedCatalogue = buildCatalogue();
  }
  return cachedCatalogue;
}

export function getGeneratedCatalogue() {
  return buildCatalogueFromGeneratedSource();
}

export function findRegisterDescriptorByKey(key) {
  return getRegisterCatalogue().find((descriptor) => descriptor.key === key);
}

export function rawToUserValue(descriptor, raw) {
  if (!descriptor) {
    return raw;
  }
  if (descriptor.valueClass === 'enum') {
    return raw;
  }
  const value = raw * descriptor.scale;
  const factor = 10 ** descriptor.precision;
  return descriptor.precision > 0 ? Math.round(value * factor) / factor : value;
}

export function clampUserValue(descriptor, userValue) {
  if (!descriptor || descriptor.valueClass === 'enum') {
    return userValue;
  }
  let value = userValue;
  if (descriptor.hasMin && typeof descriptor.minUser === 'number') {
    value = Math.max(value, descriptor.minUser);
  }
  if (descriptor.hasMax && typeof descriptor.maxUser === 'number') {
    value = Math.min(value, descriptor.maxUser);
  }
  const factor = 10 ** descriptor.precision;
  return descriptor.precision > 0 ? Math.round(value * factor) / factor : value;
}

export function userToRawValue(descriptor, userValue) {
  if (!descriptor) {
    throw new Error('Descripteur de registre introuvable.');
  }
  if (descriptor.valueClass === 'enum') {
    const candidate = Number.parseInt(userValue, 10);
    if (!descriptor.enum.some((entry) => entry.value === candidate)) {
      throw new Error(`Valeur ${userValue} non valide pour ${descriptor.key}`);
    }
    return candidate;
  }

  if (descriptor.scale === 0) {
    throw new Error(`Registre ${descriptor.key} possède une échelle invalide.`);
  }

  const requestedRaw = userValue / descriptor.scale;
  let alignedRaw = requestedRaw;
  const step = descriptor.stepRaw || 0;
  if (step > 0) {
    const base = descriptor.hasMin && typeof descriptor.minRaw === 'number' ? descriptor.minRaw : 0;
    const steps = Math.round((alignedRaw - base) / step);
    alignedRaw = base + steps * step;
  }

  if (descriptor.hasMin && typeof descriptor.minRaw === 'number' && alignedRaw < descriptor.minRaw) {
    throw new Error(`Valeur trop basse pour ${descriptor.key}`);
  }
  if (descriptor.hasMax && typeof descriptor.maxRaw === 'number' && alignedRaw > descriptor.maxRaw) {
    throw new Error(`Valeur trop élevée pour ${descriptor.key}`);
  }

  if (alignedRaw < 0 || alignedRaw > 0xffff) {
    throw new Error(`Valeur hors limites pour ${descriptor.key}`);
  }

  return Math.round(alignedRaw);
}

export default {
  getRegisterCatalogue,
  getGeneratedCatalogue,
  findRegisterDescriptorByKey,
  rawToUserValue,
  userToRawValue,
  clampUserValue,
};
