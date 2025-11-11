const AUTH_STORAGE_KEY = 'tinybms:basic-auth';
const CSRF_TTL_FALLBACK_MS = 15 * 60 * 1000;
const METHODS_REQUIRING_CSRF = new Set(['POST', 'PUT', 'PATCH', 'DELETE']);

let initialized = false;
let cachedAuth = null;
let csrfToken = null;
let csrfExpiry = 0;
let authPromise = null;

function base64Encode(text) {
    const encoder = new TextEncoder();
    const bytes = encoder.encode(text);
    let binary = '';
    bytes.forEach(byte => {
        binary += String.fromCharCode(byte);
    });
    return window.btoa(binary);
}

function loadAuthFromSession() {
    try {
        const raw = window.sessionStorage?.getItem(AUTH_STORAGE_KEY);
        if (!raw) {
            return null;
        }
        const parsed = JSON.parse(raw);
        if (parsed && typeof parsed.username === 'string' && typeof parsed.token === 'string') {
            return parsed;
        }
    } catch (error) {
        console.warn('[security] Failed to load credentials from session:', error);
    }
    return null;
}

function storeAuth(auth) {
    try {
        window.sessionStorage?.setItem(AUTH_STORAGE_KEY, JSON.stringify(auth));
    } catch (error) {
        console.warn('[security] Unable to persist credentials:', error);
    }
}

function invalidateCsrfToken() {
    csrfToken = null;
    csrfExpiry = 0;
}

function invalidateCredentials() {
    cachedAuth = null;
    authPromise = null;
    invalidateCsrfToken();
    try {
        window.sessionStorage?.removeItem(AUTH_STORAGE_KEY);
    } catch (error) {
        console.warn('[security] Unable to clear persisted credentials:', error);
    }
}

async function promptForCredentials() {
    const username = window.prompt('Nom d\'utilisateur (HTTP Basic)');
    if (username === null || username.trim() === '') {
        throw new Error('Authentication cancelled');
    }
    const password = window.prompt('Mot de passe (HTTP Basic)');
    if (password === null) {
        throw new Error('Authentication cancelled');
    }
    const token = base64Encode(`${username}:${password}`);
    const auth = { username: username.trim(), token };
    cachedAuth = auth;
    storeAuth(auth);
    invalidateCsrfToken();
    return auth;
}

async function ensureCredentials(forcePrompt = false) {
    if (!forcePrompt && cachedAuth) {
        return cachedAuth;
    }

    if (!forcePrompt) {
        const stored = loadAuthFromSession();
        if (stored) {
            cachedAuth = stored;
            return stored;
        }
    }

    if (!authPromise) {
        authPromise = (async () => {
            try {
                return await promptForCredentials();
            } finally {
                authPromise = null;
            }
        })();
    }

    return authPromise;
}

async function ensureCsrfToken(auth, originalFetch) {
    const now = Date.now();
    if (csrfToken && now < csrfExpiry) {
        return csrfToken;
    }

    const response = await originalFetch('/api/security/csrf', {
        method: 'GET',
        headers: {
            Authorization: `Basic ${auth.token}`,
            'Cache-Control': 'no-store',
        },
        cache: 'no-store',
    });

    if (response.status === 401) {
        invalidateCredentials();
        throw new Error('Authentication required');
    }

    if (!response.ok) {
        throw new Error(`CSRF token fetch failed (${response.status})`);
    }

    let payload;
    try {
        payload = await response.json();
    } catch (error) {
        throw new Error('Invalid CSRF token response');
    }

    if (!payload || typeof payload.token !== 'string') {
        throw new Error('Invalid CSRF token response');
    }

    csrfToken = payload.token;
    const ttl = typeof payload.expires_in === 'number' ? payload.expires_in : CSRF_TTL_FALLBACK_MS;
    csrfExpiry = Date.now() + ttl - 5000;
    return csrfToken;
}

function shouldIntercept(url) {
    try {
        const resolved = new URL(url, window.location.origin);
        if (resolved.origin !== window.location.origin) {
            return false;
        }
        return resolved.pathname.startsWith('/api/');
    } catch (error) {
        return false;
    }
}

export function initSecurityInterceptors() {
    if (initialized) {
        return;
    }
    if (typeof window === 'undefined' || typeof window.fetch !== 'function') {
        return;
    }

    const originalFetch = window.fetch.bind(window);

    window.fetch = async (input, init = {}) => {
        const request = new Request(input, init);
        if (!shouldIntercept(request.url)) {
            return originalFetch(request);
        }

        const method = (request.method || 'GET').toUpperCase();
        const needsCsrf = METHODS_REQUIRING_CSRF.has(method);
        const baseRequest = new Request(request);

        for (let attempt = 0; attempt < 2; attempt += 1) {
            let auth;
            try {
                auth = await ensureCredentials(attempt > 0);
            } catch (error) {
                return Promise.reject(error);
            }

            const headers = new Headers(baseRequest.headers);
            headers.set('Authorization', `Basic ${auth.token}`);

            if (needsCsrf) {
                try {
                    const token = await ensureCsrfToken(auth, originalFetch);
                    headers.set('X-CSRF-Token', token);
                } catch (error) {
                    invalidateCsrfToken();
                    invalidateCredentials();
                    if (attempt === 0) {
                        continue;
                    }
                    return Promise.reject(error);
                }
            }

            const authedRequest = new Request(baseRequest, { headers });
            const response = await originalFetch(authedRequest);

            if (response.status === 401) {
                invalidateCredentials();
                if (attempt === 0) {
                    continue;
                }
            } else if (response.status === 403 && needsCsrf) {
                invalidateCsrfToken();
                if (attempt === 0) {
                    continue;
                }
            }

            return response;
        }

        return originalFetch(baseRequest);
    };

    initialized = true;
}

export function resetSecurityStateForTesting() {
    initialized = false;
    cachedAuth = null;
    authPromise = null;
    invalidateCsrfToken();
}
