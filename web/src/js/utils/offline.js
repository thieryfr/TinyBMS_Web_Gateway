/**
 * @file offline.js
 * @brief Service Worker registration and offline mode management
 * @description Handles Service Worker lifecycle and offline/online state
 */

/**
 * Service Worker registration
 */
let registration = null;
let isOnline = navigator.onLine;

/**
 * Register Service Worker
 *
 * @param {Object} options - Options
 * @param {string} options.path - Path to service worker file
 * @param {Function} options.onUpdate - Callback when update is available
 * @param {Function} options.onOffline - Callback when going offline
 * @param {Function} options.onOnline - Callback when coming back online
 * @param {boolean} options.autoUpdate - Automatically update service worker
 *
 * @returns {Promise<ServiceWorkerRegistration|null>}
 *
 * @example
 * registerServiceWorker({
 *   path: '/service-worker.js',
 *   onUpdate: () => showUpdateNotification(),
 *   onOffline: () => showOfflineBanner(),
 *   onOnline: () => hideOfflineBanner()
 * });
 */
export async function registerServiceWorker(options = {}) {
    const {
        path = '/service-worker.js',
        onUpdate = null,
        onOffline = null,
        onOnline = null,
        autoUpdate = false,
    } = options;

    // Check if Service Worker is supported
    if (!('serviceWorker' in navigator)) {
        console.warn('[Offline] Service Worker not supported');
        return null;
    }

    try {
        // Register Service Worker
        registration = await navigator.serviceWorker.register(path, {
            scope: '/',
        });

        console.log('[Offline] Service Worker registered:', registration.scope);

        // Check for updates
        registration.addEventListener('updatefound', () => {
            const newWorker = registration.installing;
            console.log('[Offline] Service Worker update found');

            newWorker.addEventListener('statechange', () => {
                if (newWorker.state === 'installed' && navigator.serviceWorker.controller) {
                    console.log('[Offline] Service Worker update available');

                    if (autoUpdate) {
                        // Auto-update: skip waiting and reload
                        newWorker.postMessage({ type: 'SKIP_WAITING' });
                        window.location.reload();
                    } else if (onUpdate) {
                        // Notify user of update
                        onUpdate(newWorker);
                    }
                }
            });
        });

        // Set up online/offline listeners
        setupOnlineOfflineListeners(onOffline, onOnline);

        return registration;
    } catch (error) {
        console.error('[Offline] Service Worker registration failed:', error);
        return null;
    }
}

/**
 * Unregister Service Worker
 */
export async function unregisterServiceWorker() {
    if (!registration) {
        console.warn('[Offline] No Service Worker registered');
        return false;
    }

    try {
        const result = await registration.unregister();
        console.log('[Offline] Service Worker unregistered:', result);
        registration = null;
        return result;
    } catch (error) {
        console.error('[Offline] Service Worker unregistration failed:', error);
        return false;
    }
}

/**
 * Update Service Worker
 */
export async function updateServiceWorker() {
    if (!registration) {
        console.warn('[Offline] No Service Worker registered');
        return;
    }

    try {
        await registration.update();
        console.log('[Offline] Service Worker update check complete');
    } catch (error) {
        console.error('[Offline] Service Worker update failed:', error);
    }
}

/**
 * Skip waiting and activate new Service Worker
 */
export function activateServiceWorkerUpdate() {
    if (!registration || !registration.waiting) {
        console.warn('[Offline] No Service Worker update waiting');
        return;
    }

    registration.waiting.postMessage({ type: 'SKIP_WAITING' });

    // Reload page after activation
    navigator.serviceWorker.addEventListener('controllerchange', () => {
        window.location.reload();
    });
}

/**
 * Clear all caches
 */
export async function clearAllCaches() {
    if (!registration) {
        console.warn('[Offline] No Service Worker registered');
        return false;
    }

    try {
        const messageChannel = new MessageChannel();

        const promise = new Promise((resolve) => {
            messageChannel.port1.onmessage = (event) => {
                resolve(event.data.success);
            };
        });

        registration.active.postMessage(
            { type: 'CLEAR_CACHE' },
            [messageChannel.port2]
        );

        return await promise;
    } catch (error) {
        console.error('[Offline] Failed to clear caches:', error);
        return false;
    }
}

/**
 * Get Service Worker version
 */
export async function getServiceWorkerVersion() {
    if (!registration || !registration.active) {
        return null;
    }

    try {
        const messageChannel = new MessageChannel();

        const promise = new Promise((resolve) => {
            messageChannel.port1.onmessage = (event) => {
                resolve(event.data.version);
            };
        });

        registration.active.postMessage(
            { type: 'GET_VERSION' },
            [messageChannel.port2]
        );

        return await promise;
    } catch (error) {
        console.error('[Offline] Failed to get version:', error);
        return null;
    }
}

/**
 * Set up online/offline event listeners
 */
function setupOnlineOfflineListeners(onOffline, onOnline) {
    window.addEventListener('online', () => {
        console.log('[Offline] Back online');
        isOnline = true;

        // Trigger custom event
        window.dispatchEvent(new CustomEvent('appstatuschange', {
            detail: { online: true }
        }));

        if (onOnline) {
            onOnline();
        }
    });

    window.addEventListener('offline', () => {
        console.log('[Offline] Gone offline');
        isOnline = false;

        // Trigger custom event
        window.dispatchEvent(new CustomEvent('appstatuschange', {
            detail: { online: false }
        }));

        if (onOffline) {
            onOffline();
        }
    });
}

/**
 * Check if currently online
 */
export function checkIsOnline() {
    return isOnline;
}

/**
 * Listen to online/offline status changes
 *
 * @param {Function} callback - Callback function(isOnline)
 * @returns {Function} Cleanup function
 */
export function onStatusChange(callback) {
    const handler = (event) => {
        callback(event.detail.online);
    };

    window.addEventListener('appstatuschange', handler);

    return () => {
        window.removeEventListener('appstatuschange', handler);
    };
}

/**
 * Create offline indicator banner
 *
 * @param {Object} options - Options
 * @param {string} options.message - Message to display
 * @param {string} options.className - Additional CSS classes
 * @param {string} options.position - Position: 'top' or 'bottom'
 */
export function createOfflineIndicator(options = {}) {
    const {
        message = 'Mode hors ligne - Données en cache',
        className = 'alert alert-warning',
        position = 'top',
    } = options;

    let indicator = document.getElementById('offline-indicator');

    if (!indicator) {
        indicator = document.createElement('div');
        indicator.id = 'offline-indicator';
        indicator.className = `${className} offline-indicator position-${position}`;
        indicator.style.cssText = `
            position: fixed;
            ${position}: 0;
            left: 0;
            right: 0;
            z-index: 9999;
            margin: 0;
            border-radius: 0;
            text-align: center;
            display: none;
        `;
        indicator.innerHTML = `
            <i class="ti ti-wifi-off me-2"></i>
            <span>${message}</span>
        `;

        document.body.appendChild(indicator);
    }

    // Show/hide based on online status
    const updateIndicator = (online) => {
        indicator.style.display = online ? 'none' : 'block';
    };

    // Initial state
    updateIndicator(isOnline);

    // Listen to status changes
    onStatusChange(updateIndicator);

    return indicator;
}

/**
 * Initialize offline mode
 *
 * @param {Object} options - Combined options for registration and UI
 *
 * @example
 * initializeOfflineMode({
 *   serviceWorkerPath: '/service-worker.js',
 *   autoUpdate: false,
 *   showIndicator: true,
 *   onUpdate: (newWorker) => {
 *     showNotification({
 *       type: 'info',
 *       message: 'Une mise à jour est disponible',
 *       actions: [
 *         { label: 'Mettre à jour', onClick: () => activateServiceWorkerUpdate() }
 *       ]
 *     });
 *   }
 * });
 */
export async function initializeOfflineMode(options = {}) {
    const {
        serviceWorkerPath = '/service-worker.js',
        autoUpdate = false,
        showIndicator = true,
        onUpdate = null,
        onOffline = null,
        onOnline = null,
    } = options;

    // Register Service Worker
    await registerServiceWorker({
        path: serviceWorkerPath,
        onUpdate,
        onOffline,
        onOnline,
        autoUpdate,
    });

    // Create offline indicator if requested
    if (showIndicator) {
        createOfflineIndicator();
    }
}

// Export for global use
if (typeof window !== 'undefined') {
    window.offlineManager = {
        registerServiceWorker,
        unregisterServiceWorker,
        updateServiceWorker,
        activateServiceWorkerUpdate,
        clearAllCaches,
        getServiceWorkerVersion,
        checkIsOnline,
        onStatusChange,
        createOfflineIndicator,
        initializeOfflineMode,
    };
}
