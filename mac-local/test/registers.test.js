import test from 'node:test';
import assert from 'node:assert/strict';

import {
  getRegisterCatalogue,
  findRegisterDescriptorByKey,
  userToRawValue,
  rawToUserValue,
  clampUserValue,
} from '../src/registers.js';

test('catalogue loads bundled data with expected shape', () => {
  const catalogue = getRegisterCatalogue();
  assert.ok(Array.isArray(catalogue), 'Catalogue must be an array');
  assert.ok(catalogue.length > 0, 'Catalogue must not be empty');
  for (const descriptor of catalogue) {
    assert.equal(typeof descriptor.address, 'number');
    assert.equal(typeof descriptor.key, 'string');
    assert.ok(descriptor.key.length > 0);
    assert.ok(['enum', 'numeric'].includes(descriptor.valueClass));
  }
});

test('enum descriptors enforce allowed values', () => {
  const descriptor = findRegisterDescriptorByKey('operation_mode');
  assert.ok(descriptor, 'operation_mode descriptor should exist');
  const allowedValue = descriptor.enum[0].value;
  assert.equal(userToRawValue(descriptor, allowedValue), allowedValue);
  assert.throws(() => userToRawValue(descriptor, 9999));
});

test('numeric descriptors handle scaling, clamping and stepping', () => {
  const descriptor = findRegisterDescriptorByKey('fully_charged_voltage_mv');
  assert.ok(descriptor, 'fully_charged_voltage_mv descriptor should exist');
  const clamped = clampUserValue(descriptor, 100000);
  assert.equal(clamped, descriptor.maxUser);
  const raw = userToRawValue(descriptor, descriptor.defaultUser + 5);
  assert.equal(raw, descriptor.defaultRaw + descriptor.stepRaw);
  const backToUser = rawToUserValue(descriptor, raw);
  assert.equal(backToUser, descriptor.defaultUser + descriptor.stepUser);
});
