/**
 * @file loading.js
 * @brief Loading states management (spinners, skeleton screens, button states)
 * @description Provides utilities to show loading indicators during async operations
 */

/**
 * Loading state tracker
 */
const loadingStates = new Map();

/**
 * Show a spinner inside an element
 *
 * @param {HTMLElement|string} target - Element or selector
 * @param {Object} options - Options
 * @param {string} options.size - Spinner size: sm, md, lg
 * @param {string} options.variant - Bootstrap variant: primary, secondary, etc.
 * @param {string} options.message - Optional message below spinner
 * @param {boolean} options.overlay - Show overlay background
 *
 * @returns {string} Loading ID for later removal
 *
 * @example
 * const loadingId = showSpinner('#content', { message: 'Chargement...' });
 * // ... async operation
 * hideSpinner(loadingId);
 */
export function showSpinner(target, options = {}) {
    const element = typeof target === 'string' ? document.querySelector(target) : target;
    if (!element) {
        console.warn('showSpinner: target element not found');
        return null;
    }

    const {
        size = 'md',
        variant = 'primary',
        message = '',
        overlay = false,
    } = options;

    const loadingId = `loading-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;

    // Create spinner container
    const container = document.createElement('div');
    container.className = `loading-spinner-container ${overlay ? 'with-overlay' : ''}`;
    container.dataset.loadingId = loadingId;

    // Size classes
    const sizeClass = {
        sm: 'spinner-border-sm',
        md: '',
        lg: 'spinner-border-lg',
    }[size] || '';

    // Build HTML
    container.innerHTML = `
        <div class="loading-spinner-content">
            <div class="spinner-border text-${variant} ${sizeClass}" role="status">
                <span class="visually-hidden">Chargement...</span>
            </div>
            ${message ? `<div class="loading-message mt-2 text-muted">${message}</div>` : ''}
        </div>
    `;

    // Add CSS if not already present
    if (!document.getElementById('loading-styles')) {
        const style = document.createElement('style');
        style.id = 'loading-styles';
        style.textContent = `
            .loading-spinner-container {
                display: flex;
                align-items: center;
                justify-content: center;
                min-height: 100px;
                width: 100%;
            }

            .loading-spinner-container.with-overlay {
                position: absolute;
                top: 0;
                left: 0;
                right: 0;
                bottom: 0;
                background: rgba(255, 255, 255, 0.9);
                z-index: 1000;
                backdrop-filter: blur(2px);
            }

            .loading-spinner-content {
                text-align: center;
            }

            .loading-message {
                font-size: 0.875rem;
            }

            .spinner-border-lg {
                width: 3rem;
                height: 3rem;
                border-width: 0.3em;
            }

            .skeleton {
                animation: skeleton-loading 1.5s infinite ease-in-out;
                background: linear-gradient(
                    90deg,
                    #f0f0f0 0%,
                    #e0e0e0 50%,
                    #f0f0f0 100%
                );
                background-size: 200% 100%;
                border-radius: 4px;
            }

            .skeleton-text {
                height: 1em;
                margin-bottom: 0.5em;
            }

            .skeleton-text:last-child {
                width: 80%;
            }

            .skeleton-avatar {
                width: 40px;
                height: 40px;
                border-radius: 50%;
            }

            .skeleton-card {
                height: 200px;
                border-radius: 8px;
            }

            @keyframes skeleton-loading {
                0% {
                    background-position: 200% 0;
                }
                100% {
                    background-position: -200% 0;
                }
            }

            .btn-loading {
                position: relative;
                pointer-events: none;
            }

            .btn-loading .btn-text {
                visibility: hidden;
            }

            .btn-loading::after {
                content: "";
                position: absolute;
                width: 1rem;
                height: 1rem;
                top: 50%;
                left: 50%;
                margin-left: -0.5rem;
                margin-top: -0.5rem;
                border: 2px solid currentColor;
                border-right-color: transparent;
                border-radius: 50%;
                animation: btn-loading-spinner 0.75s linear infinite;
            }

            @keyframes btn-loading-spinner {
                to {
                    transform: rotate(360deg);
                }
            }
        `;
        document.head.appendChild(style);
    }

    // If overlay, make parent position relative if it isn't already
    if (overlay) {
        const position = window.getComputedStyle(element).position;
        if (position === 'static') {
            element.style.position = 'relative';
        }
    }

    // Append to element
    element.appendChild(container);

    // Track loading state
    loadingStates.set(loadingId, { element, container });

    return loadingId;
}

/**
 * Hide a spinner
 *
 * @param {string} loadingId - Loading ID returned by showSpinner
 */
export function hideSpinner(loadingId) {
    const state = loadingStates.get(loadingId);
    if (!state) return;

    // Remove container
    if (state.container && state.container.parentNode) {
        state.container.remove();
    }

    // Remove from tracker
    loadingStates.delete(loadingId);
}

/**
 * Show loading state on a button
 *
 * @param {HTMLButtonElement|string} button - Button element or selector
 * @param {boolean} disabled - Also disable the button
 *
 * @example
 * setButtonLoading('#submit-btn', true);
 * await saveConfig();
 * setButtonLoading('#submit-btn', false);
 */
export function setButtonLoading(button, loading = true, disabled = true) {
    const btn = typeof button === 'string' ? document.querySelector(button) : button;
    if (!btn) {
        console.warn('setButtonLoading: button not found');
        return;
    }

    if (loading) {
        // Store original text
        if (!btn.dataset.originalText) {
            btn.dataset.originalText = btn.innerHTML;
        }

        // Add loading class and disable
        btn.classList.add('btn-loading');
        if (disabled) {
            btn.disabled = true;
        }

        // Wrap content in span for hiding
        if (!btn.querySelector('.btn-text')) {
            btn.innerHTML = `<span class="btn-text">${btn.innerHTML}</span>`;
        }
    } else {
        // Remove loading class
        btn.classList.remove('btn-loading');
        if (disabled) {
            btn.disabled = false;
        }

        // Restore original text
        if (btn.dataset.originalText) {
            btn.innerHTML = btn.dataset.originalText;
            delete btn.dataset.originalText;
        }
    }
}

/**
 * Create a skeleton screen placeholder
 *
 * @param {HTMLElement|string} target - Element or selector
 * @param {Object} options - Options
 * @param {string} options.type - Skeleton type: text, card, list, avatar
 * @param {number} options.lines - Number of lines for text skeleton
 * @param {number} options.items - Number of items for list skeleton
 *
 * @returns {string} Loading ID for later removal
 *
 * @example
 * const skeletonId = showSkeleton('#content', { type: 'card' });
 * const data = await fetch('/api/data');
 * hideSkeleton(skeletonId);
 * renderData(data);
 */
export function showSkeleton(target, options = {}) {
    const element = typeof target === 'string' ? document.querySelector(target) : target;
    if (!element) {
        console.warn('showSkeleton: target element not found');
        return null;
    }

    const {
        type = 'text',
        lines = 3,
        items = 3,
    } = options;

    const loadingId = `skeleton-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;

    // Create skeleton container
    const container = document.createElement('div');
    container.className = 'skeleton-container';
    container.dataset.loadingId = loadingId;

    // Generate skeleton based on type
    let html = '';

    switch (type) {
        case 'text':
            for (let i = 0; i < lines; i++) {
                html += '<div class="skeleton skeleton-text"></div>';
            }
            break;

        case 'card':
            html = '<div class="skeleton skeleton-card"></div>';
            break;

        case 'list':
            for (let i = 0; i < items; i++) {
                html += `
                    <div class="d-flex align-items-center mb-3">
                        <div class="skeleton skeleton-avatar me-3"></div>
                        <div class="flex-grow-1">
                            <div class="skeleton skeleton-text" style="width: 60%"></div>
                            <div class="skeleton skeleton-text" style="width: 40%"></div>
                        </div>
                    </div>
                `;
            }
            break;

        case 'avatar':
            html = '<div class="skeleton skeleton-avatar"></div>';
            break;

        default:
            console.warn(`Unknown skeleton type: ${type}`);
            html = '<div class="skeleton skeleton-text"></div>';
    }

    container.innerHTML = html;

    // Store original content
    if (!element.dataset.originalContent) {
        element.dataset.originalContent = element.innerHTML;
    }

    // Replace content with skeleton
    element.innerHTML = '';
    element.appendChild(container);

    // Track loading state
    loadingStates.set(loadingId, { element, container });

    return loadingId;
}

/**
 * Hide a skeleton and restore original content
 *
 * @param {string} loadingId - Loading ID returned by showSkeleton
 * @param {string|null} newContent - Optional new content to display instead of original
 */
export function hideSkeleton(loadingId, newContent = null) {
    const state = loadingStates.get(loadingId);
    if (!state) return;

    const { element } = state;

    // Restore content
    if (newContent !== null) {
        element.innerHTML = newContent;
    } else if (element.dataset.originalContent) {
        element.innerHTML = element.dataset.originalContent;
        delete element.dataset.originalContent;
    }

    // Remove from tracker
    loadingStates.delete(loadingId);
}

/**
 * Wrap an async function with loading indicator
 *
 * @param {Function} asyncFn - Async function to wrap
 * @param {HTMLElement|string} target - Element to show loading in
 * @param {Object} options - Loading options (passed to showSpinner or showSkeleton)
 * @param {string} options.indicatorType - 'spinner' or 'skeleton'
 *
 * @returns {Function} Wrapped function
 *
 * @example
 * const loadData = withLoading(async () => {
 *   const response = await fetch('/api/data');
 *   return response.json();
 * }, '#content', { indicatorType: 'skeleton', type: 'list' });
 *
 * const data = await loadData();
 */
export function withLoading(asyncFn, target, options = {}) {
    return async function(...args) {
        const { indicatorType = 'spinner', ...loadingOptions } = options;

        let loadingId;
        if (indicatorType === 'skeleton') {
            loadingId = showSkeleton(target, loadingOptions);
        } else {
            loadingId = showSpinner(target, loadingOptions);
        }

        try {
            const result = await asyncFn.apply(this, args);
            return result;
        } finally {
            if (indicatorType === 'skeleton') {
                hideSkeleton(loadingId);
            } else {
                hideSpinner(loadingId);
            }
        }
    };
}

/**
 * Clear all loading states
 */
export function clearAllLoading() {
    loadingStates.forEach((state, id) => {
        if (state.container && state.container.parentNode) {
            state.container.remove();
        }
    });
    loadingStates.clear();
}

// Export for global use
if (typeof window !== 'undefined') {
    window.showSpinner = showSpinner;
    window.hideSpinner = hideSpinner;
    window.setButtonLoading = setButtonLoading;
    window.showSkeleton = showSkeleton;
    window.hideSkeleton = hideSkeleton;
    window.withLoading = withLoading;
}
