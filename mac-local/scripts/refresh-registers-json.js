#!/usr/bin/env node
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

import { getGeneratedCatalogue } from '../src/registers.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const dataDir = path.join(__dirname, '..', 'data');
const outputPath = path.join(dataDir, 'registers.json');

function main() {
  const catalogue = getGeneratedCatalogue();
  fs.mkdirSync(dataDir, { recursive: true });
  fs.writeFileSync(outputPath, JSON.stringify(catalogue, null, 2));
  /* eslint-disable no-console */
  console.log(`Catalogue exporté (${catalogue.length} registres) → ${outputPath}`);
  /* eslint-enable no-console */
}

main();
