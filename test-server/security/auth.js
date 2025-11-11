import crypto from 'node:crypto';

const METHODS_REQUIRING_CSRF = new Set(['POST', 'PUT', 'PATCH', 'DELETE']);
export const CSRF_TTL_MS = 15 * 60 * 1000;
export const DEFAULT_USERNAME = process.env.TINYBMS_TEST_USERNAME || 'admin';
export const DEFAULT_PASSWORD = process.env.TINYBMS_TEST_PASSWORD || 'changeme';

const csrfTokens = new Map();

function parseBasicAuthorization(header) {
  if (!header || typeof header !== 'string') {
    return null;
  }
  const trimmed = header.trim();
  if (!trimmed.toLowerCase().startsWith('basic ')) {
    return null;
  }
  const encoded = trimmed.slice(6).trim();
  try {
    const decoded = Buffer.from(encoded, 'base64').toString('utf8');
    const separator = decoded.indexOf(':');
    if (separator === -1) {
      return null;
    }
    const username = decoded.slice(0, separator);
    const password = decoded.slice(separator + 1);
    return [username, password];
  } catch (error) {
    return null;
  }
}

function validateCsrfToken(username, token) {
  const entry = csrfTokens.get(username);
  if (!entry) {
    return false;
  }
  if (entry.expiresAt < Date.now()) {
    csrfTokens.delete(username);
    return false;
  }
  return entry.token === token;
}

export function issueCsrfToken(username) {
  const token = crypto.randomBytes(32).toString('hex');
  csrfTokens.set(username, { token, expiresAt: Date.now() + CSRF_TTL_MS });
  return { token, expires_in: CSRF_TTL_MS };
}

export function requireAuth({ requireCsrf = false } = {}) {
  return (req, res, next) => {
    const credentials = parseBasicAuthorization(req.headers.authorization);
    if (!credentials) {
      res.set('WWW-Authenticate', 'Basic realm="TinyBMS-GW", charset="UTF-8"');
      return res.status(401).json({ error: 'authentication_required' });
    }

    const [username, password] = credentials;
    if (username !== DEFAULT_USERNAME || password !== DEFAULT_PASSWORD) {
      res.set('WWW-Authenticate', 'Basic realm="TinyBMS-GW", charset="UTF-8"');
      return res.status(401).json({ error: 'authentication_failed' });
    }

    req.auth = { username };

    if (requireCsrf && METHODS_REQUIRING_CSRF.has(req.method.toUpperCase())) {
      const token = req.get('x-csrf-token');
      if (!token || !validateCsrfToken(username, token)) {
        return res.status(403).json({ error: 'invalid_csrf_token' });
      }
    }

    return next();
  };
}

export function resetSecurityState() {
  csrfTokens.clear();
}
