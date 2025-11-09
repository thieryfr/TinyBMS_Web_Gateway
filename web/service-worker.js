/**
 * @file service-worker.js
 * @brief Service Worker for offline functionality and caching
 * @description Provides offline support with cache-first strategy for static assets
 *              and network-first strategy for API calls
 */

const CACHE_VERSION = 'tinybms-v1';
const CACHE_NAME = `${CACHE_VERSION}::static`;
const API_CACHE_NAME = `${CACHE_VERSION}::api`;

/**
 * Static assets to cache on install
 */
const STATIC_ASSETS = [
    '/',
    '/index.html',
    '/dashboard.html',
    '/config.html',
    '/alerts.html',
    '/src/css/tabler.min.css',
    '/src/css/tabler-icons.min.css',
    '/src/js/tabler.min.js',
    '/src/js/utils/notifications.js',
    '/src/js/utils/loading.js',
    '/src/js/utils/theme.js',
    '/src/js/utils/i18n.js',
];

/**
 * API endpoints to cache (network-first strategy)
 */
const API_ENDPOINTS = [
    '/api/status',
    '/api/config',
    '/api/mqtt/config',
    '/api/alerts/statistics',
];

/**
 * Install event - cache static assets
 */
self.addEventListener('install', (event) => {
    console.log('[Service Worker] Installing...');

    event.waitUntil(
        caches.open(CACHE_NAME)
            .then((cache) => {
                console.log('[Service Worker] Caching static assets');
                return cache.addAll(STATIC_ASSETS.map(url => new Request(url, { cache: 'reload' })));
            })
            .then(() => {
                console.log('[Service Worker] Installed successfully');
                return self.skipWaiting(); // Activate immediately
            })
            .catch((error) => {
                console.error('[Service Worker] Installation failed:', error);
            })
    );
});

/**
 * Activate event - clean up old caches
 */
self.addEventListener('activate', (event) => {
    console.log('[Service Worker] Activating...');

    event.waitUntil(
        caches.keys()
            .then((cacheNames) => {
                return Promise.all(
                    cacheNames
                        .filter((name) => {
                            // Delete old caches that don't match current version
                            return name.startsWith('tinybms-') && !name.startsWith(CACHE_VERSION);
                        })
                        .map((name) => {
                            console.log(`[Service Worker] Deleting old cache: ${name}`);
                            return caches.delete(name);
                        })
                );
            })
            .then(() => {
                console.log('[Service Worker] Activated successfully');
                return self.clients.claim(); // Take control of all pages
            })
    );
});

/**
 * Fetch event - serve from cache or network
 */
self.addEventListener('fetch', (event) => {
    const { request } = event;
    const url = new URL(request.url);

    // Skip cross-origin requests
    if (url.origin !== location.origin) {
        return;
    }

    // Skip WebSocket requests
    if (url.protocol === 'ws:' || url.protocol === 'wss:') {
        return;
    }

    // API requests: network-first strategy
    if (url.pathname.startsWith('/api/')) {
        event.respondWith(networkFirstStrategy(request));
        return;
    }

    // WebSocket endpoints: don't cache
    if (url.pathname.startsWith('/ws/')) {
        return;
    }

    // Static assets: cache-first strategy
    event.respondWith(cacheFirstStrategy(request));
});

/**
 * Cache-first strategy: serve from cache, fallback to network
 * Good for static assets that don't change often
 */
async function cacheFirstStrategy(request) {
    const cache = await caches.open(CACHE_NAME);

    // Try to get from cache first
    const cachedResponse = await cache.match(request);
    if (cachedResponse) {
        console.log(`[Service Worker] Serving from cache: ${request.url}`);
        return cachedResponse;
    }

    // Fallback to network
    try {
        console.log(`[Service Worker] Fetching from network: ${request.url}`);
        const networkResponse = await fetch(request);

        // Cache successful responses
        if (networkResponse && networkResponse.status === 200) {
            cache.put(request, networkResponse.clone());
        }

        return networkResponse;
    } catch (error) {
        console.error(`[Service Worker] Fetch failed: ${request.url}`, error);

        // Return offline page if available
        const offlinePage = await cache.match('/offline.html');
        if (offlinePage) {
            return offlinePage;
        }

        // Or return a generic error response
        return new Response('Offline - content not available', {
            status: 503,
            statusText: 'Service Unavailable',
            headers: new Headers({
                'Content-Type': 'text/plain',
            }),
        });
    }
}

/**
 * Network-first strategy: try network first, fallback to cache
 * Good for API calls where fresh data is important
 */
async function networkFirstStrategy(request) {
    const cache = await caches.open(API_CACHE_NAME);

    try {
        // Try network first
        console.log(`[Service Worker] Fetching API from network: ${request.url}`);
        const networkResponse = await fetch(request);

        // Cache successful responses
        if (networkResponse && networkResponse.status === 200) {
            cache.put(request, networkResponse.clone());
        }

        return networkResponse;
    } catch (error) {
        console.log(`[Service Worker] Network failed, trying cache: ${request.url}`);

        // Fallback to cache
        const cachedResponse = await cache.match(request);
        if (cachedResponse) {
            console.log(`[Service Worker] Serving stale API data from cache: ${request.url}`);
            return cachedResponse;
        }

        // No cache available
        console.error(`[Service Worker] No cache available for: ${request.url}`, error);
        return new Response(JSON.stringify({
            error: 'Offline - no cached data available',
            offline: true,
        }), {
            status: 503,
            statusText: 'Service Unavailable',
            headers: new Headers({
                'Content-Type': 'application/json',
            }),
        });
    }
}

/**
 * Message event - handle messages from clients
 */
self.addEventListener('message', (event) => {
    console.log('[Service Worker] Message received:', event.data);

    if (event.data.type === 'SKIP_WAITING') {
        self.skipWaiting();
    }

    if (event.data.type === 'CLEAR_CACHE') {
        event.waitUntil(
            caches.keys()
                .then((cacheNames) => {
                    return Promise.all(
                        cacheNames.map((name) => caches.delete(name))
                    );
                })
                .then(() => {
                    event.ports[0].postMessage({ success: true });
                })
        );
    }

    if (event.data.type === 'GET_VERSION') {
        event.ports[0].postMessage({ version: CACHE_VERSION });
    }
});

/**
 * Sync event - background sync for offline actions
 */
self.addEventListener('sync', (event) => {
    console.log('[Service Worker] Sync event:', event.tag);

    if (event.tag === 'sync-config') {
        event.waitUntil(syncConfiguration());
    }
});

/**
 * Sync configuration changes made while offline
 */
async function syncConfiguration() {
    // This would sync any pending configuration changes
    // For now, just log
    console.log('[Service Worker] Syncing configuration...');

    // In a real implementation, you would:
    // 1. Get pending changes from IndexedDB
    // 2. POST them to the API
    // 3. Clear pending changes on success
}
