/**
 * @file fetchAPI.js
 * @brief Robust fetch wrapper with error handling and notifications
 * @description Provides a centralized fetch API with automatic error handling,
 *              retry logic, and user notifications
 */

/**
 * Configuration for fetch retry logic
 */
const FETCH_CONFIG = {
    maxRetries: 3,
    retryDelay: 1000, // ms
    timeout: 10000, // ms
    retryOn: [408, 429, 500, 502, 503, 504], // HTTP status codes to retry
};

/**
 * Show a notification to the user
 * @param {string} message - Notification message
 * @param {string} type - Notification type (success, error, warning, info)
 * @param {number} duration - Duration in ms (0 = persistent)
 */
function showNotification(message, type = 'info', duration = 5000) {
    // Look for existing notification container or create one
    let container = document.getElementById('notification-container');

    if (!container) {
        container = document.createElement('div');
        container.id = 'notification-container';
        container.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            z-index: 9999;
            max-width: 400px;
        `;
        document.body.appendChild(container);
    }

    // Create notification element
    const notification = document.createElement('div');
    notification.className = `alert alert-${type} alert-dismissible fade show`;
    notification.setAttribute('role', 'alert');

    const iconMap = {
        success: 'ti-check',
        error: 'ti-alert-circle',
        warning: 'ti-alert-triangle',
        info: 'ti-info-circle',
    };

    notification.innerHTML = `
        <div class="d-flex align-items-center gap-2">
            <i class="ti ${iconMap[type] || 'ti-info-circle'}"></i>
            <div class="flex-grow-1">${escapeHtml(message)}</div>
            <button type="button" class="btn-close" data-bs-dismiss="alert" aria-label="Close"></button>
        </div>
    `;

    container.appendChild(notification);

    // Auto-dismiss if duration > 0
    if (duration > 0) {
        setTimeout(() => {
            notification.classList.remove('show');
            setTimeout(() => notification.remove(), 150);
        }, duration);
    }

    return notification;
}

/**
 * Escape HTML to prevent XSS
 * @param {string} text - Text to escape
 * @returns {string} Escaped HTML-safe text
 */
function escapeHtml(text) {
    if (!text) return '';
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

/**
 * Sleep for specified duration
 * @param {number} ms - Duration in milliseconds
 * @returns {Promise} Promise that resolves after delay
 */
function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

/**
 * Fetch with timeout
 * @param {string} url - URL to fetch
 * @param {Object} options - Fetch options
 * @param {number} timeout - Timeout in ms
 * @returns {Promise<Response>} Fetch response
 */
async function fetchWithTimeout(url, options = {}, timeout = FETCH_CONFIG.timeout) {
    const controller = new AbortController();
    const id = setTimeout(() => controller.abort(), timeout);

    try {
        const response = await fetch(url, {
            ...options,
            signal: controller.signal,
        });
        clearTimeout(id);
        return response;
    } catch (error) {
        clearTimeout(id);
        if (error.name === 'AbortError') {
            throw new Error(`Request timeout after ${timeout}ms`);
        }
        throw error;
    }
}

/**
 * Enhanced fetch with error handling, retries, and notifications
 *
 * @param {string} url - URL to fetch
 * @param {Object} options - Fetch options
 * @param {Object} config - Additional configuration
 * @param {boolean} config.silent - Don't show error notifications (default: false)
 * @param {boolean} config.showSuccess - Show success notifications (default: false)
 * @param {string} config.successMessage - Custom success message
 * @param {boolean} config.retry - Enable retry logic (default: true)
 * @param {number} config.maxRetries - Max retry attempts
 * @param {number} config.timeout - Request timeout in ms
 * @returns {Promise<any>} Parsed JSON response
 * @throws {Error} If request fails after retries
 *
 * @example
 * // Basic usage
 * const data = await fetchAPI('/api/config');
 *
 * @example
 * // POST with custom config
 * const result = await fetchAPI('/api/config', {
 *   method: 'POST',
 *   body: JSON.stringify(config),
 * }, {
 *   showSuccess: true,
 *   successMessage: 'Configuration saved successfully'
 * });
 *
 * @example
 * // Silent request (no notifications)
 * const data = await fetchAPI('/api/status', {}, { silent: true });
 */
export async function fetchAPI(url, options = {}, config = {}) {
    const {
        silent = false,
        showSuccess = false,
        successMessage = 'Operation successful',
        retry = true,
        maxRetries = FETCH_CONFIG.maxRetries,
        timeout = FETCH_CONFIG.timeout,
    } = config;

    let lastError = null;
    let attempt = 0;

    while (attempt <= (retry ? maxRetries : 0)) {
        try {
            // Set default headers
            const headers = {
                'Content-Type': 'application/json',
                ...options.headers,
            };

            // Perform fetch with timeout
            const response = await fetchWithTimeout(url, { ...options, headers }, timeout);

            // Check if response is OK
            if (!response.ok) {
                // Determine if we should retry
                const shouldRetry = retry &&
                                  attempt < maxRetries &&
                                  FETCH_CONFIG.retryOn.includes(response.status);

                if (shouldRetry) {
                    attempt++;
                    const delay = FETCH_CONFIG.retryDelay * Math.pow(2, attempt - 1); // Exponential backoff
                    console.warn(`[fetchAPI] Request failed (${response.status}), retrying in ${delay}ms... (attempt ${attempt}/${maxRetries})`);
                    await sleep(delay);
                    continue;
                }

                // Throw error for non-retryable failures
                let errorMessage = `HTTP ${response.status}: ${response.statusText}`;

                // Try to get error details from response body
                try {
                    const errorData = await response.json();
                    if (errorData.error) {
                        errorMessage = errorData.error;
                    } else if (errorData.message) {
                        errorMessage = errorData.message;
                    }
                } catch (e) {
                    // Response body is not JSON, use status text
                }

                throw new Error(errorMessage);
            }

            // Parse response
            let data;
            const contentType = response.headers.get('content-type');

            if (contentType && contentType.includes('application/json')) {
                data = await response.json();
            } else {
                data = await response.text();
            }

            // Show success notification if requested
            if (showSuccess && !silent) {
                showNotification(successMessage, 'success');
            }

            return data;

        } catch (error) {
            lastError = error;

            // Check if we should retry on network errors
            const shouldRetry = retry &&
                              attempt < maxRetries &&
                              (error.name === 'TypeError' || error.message.includes('timeout'));

            if (shouldRetry) {
                attempt++;
                const delay = FETCH_CONFIG.retryDelay * Math.pow(2, attempt - 1);
                console.warn(`[fetchAPI] Network error, retrying in ${delay}ms... (attempt ${attempt}/${maxRetries})`);
                await sleep(delay);
                continue;
            }

            // No more retries, throw error
            break;
        }
    }

    // All retries failed
    const errorMessage = lastError?.message || 'Network request failed';

    if (!silent) {
        showNotification(`Erreur réseau: ${errorMessage}`, 'error', 8000);
    }

    console.error(`[fetchAPI] Request failed for ${url}:`, lastError);
    throw lastError;
}

/**
 * Convenience methods for common HTTP verbs
 */

export async function GET(url, config = {}) {
    return fetchAPI(url, { method: 'GET' }, config);
}

export async function POST(url, data, config = {}) {
    return fetchAPI(url, {
        method: 'POST',
        body: JSON.stringify(data),
    }, config);
}

export async function PUT(url, data, config = {}) {
    return fetchAPI(url, {
        method: 'PUT',
        body: JSON.stringify(data),
    }, config);
}

export async function DELETE(url, config = {}) {
    return fetchAPI(url, { method: 'DELETE' }, config);
}

/**
 * Batch fetch multiple endpoints in parallel
 * @param {Array<{url: string, options?: Object, config?: Object}>} requests - Array of requests
 * @returns {Promise<Array>} Array of results (or errors)
 *
 * @example
 * const results = await fetchBatch([
 *   { url: '/api/status' },
 *   { url: '/api/config' },
 *   { url: '/api/mqtt/status' }
 * ]);
 */
export async function fetchBatch(requests) {
    const promises = requests.map(({ url, options, config }) =>
        fetchAPI(url, options, { ...config, silent: true })
            .catch(error => ({ error: error.message }))
    );

    return Promise.all(promises);
}

// Export for global use if needed
if (typeof window !== 'undefined') {
    window.fetchAPI = fetchAPI;
    window.showNotification = showNotification;
}
