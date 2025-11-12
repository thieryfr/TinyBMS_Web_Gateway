#!/usr/bin/env node
import { getRegisterCatalogue } from '../src/registers.js';

const catalogue = getRegisterCatalogue();

const headers = [
  'Address',
  'Hex',
  'Key',
  'Label',
  'Access',
  'Type',
  'Group',
  'Unit',
];

function escapePipe(value) {
  return String(value ?? '')
    .replace(/\|/g, '\\|')
    .replace(/\n/g, ' ')
    .trim();
}

console.log('# TinyBMS register catalogue');
console.log();
console.log(`Total registers: ${catalogue.length}`);
console.log();
console.log(`| ${headers.join(' | ')} |`);
console.log(`| ${headers.map(() => '---').join(' | ')} |`);

catalogue
  .slice()
  .sort((a, b) => a.address - b.address)
  .forEach((descriptor) => {
    const row = [
      descriptor.address,
      descriptor.addressHex,
      descriptor.key,
      descriptor.label,
      descriptor.access,
      descriptor.type,
      descriptor.group,
      descriptor.unit || '',
    ].map(escapePipe);
    console.log(`| ${row.join(' | ')} |`);
  });
