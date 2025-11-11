import test from 'node:test';
import assert from 'node:assert/strict';
import express from 'express';
import { createServer } from 'node:http';
import { once } from 'node:events';

import { requireAuth, issueCsrfToken, DEFAULT_USERNAME, DEFAULT_PASSWORD, resetSecurityState } from '../security/auth.js';

const BASIC_HEADER = `Basic ${Buffer.from(`${DEFAULT_USERNAME}:${DEFAULT_PASSWORD}`).toString('base64')}`;

async function startTestServer() {
  const app = express();
  app.use(express.json());

  app.get('/api/config', requireAuth(), (req, res) => {
    res.json({ status: 'ok' });
  });

  app.post('/api/config', requireAuth({ requireCsrf: true }), (req, res) => {
    res.json({ success: true });
  });

  app.get('/api/security/csrf', requireAuth(), (req, res) => {
    res.json(issueCsrfToken(req.auth.username));
  });

  const server = createServer(app);
  server.listen(0);
  await once(server, 'listening');
  const { port } = server.address();
  const baseUrl = `http://127.0.0.1:${port}`;

  return { server, baseUrl };
}

test('GET /api/config requires authentication', async t => {
  const { server, baseUrl } = await startTestServer();
  t.after(() => server.close());
  resetSecurityState();

  const res = await fetch(`${baseUrl}/api/config`);
  assert.equal(res.status, 401);
  assert.equal(res.headers.get('www-authenticate')?.includes('Basic'), true);
});

test('GET /api/config succeeds with Basic auth', async t => {
  const { server, baseUrl } = await startTestServer();
  t.after(() => server.close());
  resetSecurityState();

  const res = await fetch(`${baseUrl}/api/config`, {
    headers: { Authorization: BASIC_HEADER },
  });
  assert.equal(res.status, 200);
  const body = await res.json();
  assert.equal(body.status, 'ok');
});

test('POST /api/config rejects missing CSRF token', async t => {
  const { server, baseUrl } = await startTestServer();
  t.after(() => server.close());
  resetSecurityState();

  const res = await fetch(`${baseUrl}/api/config`, {
    method: 'POST',
    headers: {
      Authorization: BASIC_HEADER,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ foo: 'bar' }),
  });
  assert.equal(res.status, 403);
});

test('POST /api/config accepts valid CSRF token', async t => {
  const { server, baseUrl } = await startTestServer();
  t.after(() => server.close());
  resetSecurityState();

  const csrfResponse = await fetch(`${baseUrl}/api/security/csrf`, {
    headers: { Authorization: BASIC_HEADER },
  });
  assert.equal(csrfResponse.status, 200);
  const { token } = await csrfResponse.json();
  assert.ok(typeof token === 'string');

  const res = await fetch(`${baseUrl}/api/config`, {
    method: 'POST',
    headers: {
      Authorization: BASIC_HEADER,
      'Content-Type': 'application/json',
      'X-CSRF-Token': token,
    },
    body: JSON.stringify({ foo: 'bar' }),
  });
  assert.equal(res.status, 200);
  const payload = await res.json();
  assert.equal(payload.success, true);
});
