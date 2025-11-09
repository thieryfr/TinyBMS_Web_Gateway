/**
 * @file notifications.js
 * @brief Advanced notification/toast system with queue management
 * @description Provides a robust notification system with animations,
 *              queue management, and customizable appearance
 */

/**
 * Notification configuration
 */
const NOTIFICATION_CONFIG = {
    maxVisible: 3,           // Max simultaneous notifications
    defaultDuration: 5000,   // Default duration in ms
    stackSpacing: 10,        // Spacing between stacked notifications (px)
    position: 'top-right',   // top-right, top-left, bottom-right, bottom-left, top-center, bottom-center
    animation: 'slide',      // slide, fade, bounce
};

/**
 * Notification queue and state
 */
let notificationQueue = [];
let activeNotifications = [];
let notificationIdCounter = 0;

/**
 * Icon mapping for notification types
 */
const ICON_MAP = {
    success: 'ti-check-circle',
    error: 'ti-alert-circle',
    warning: 'ti-alert-triangle',
    info: 'ti-info-circle',
};

/**
 * Create notification container if it doesn't exist
 */
function ensureContainer() {
    let container = document.getElementById('notification-container');

    if (!container) {
        container = document.createElement('div');
        container.id = 'notification-container';
        container.className = `notification-container position-${NOTIFICATION_CONFIG.position}`;

        // Add CSS styles dynamically if not already present
        if (!document.getElementById('notification-styles')) {
            const style = document.createElement('style');
            style.id = 'notification-styles';
            style.textContent = `
                .notification-container {
                    position: fixed;
                    z-index: 10000;
                    max-width: 420px;
                    pointer-events: none;
                }

                .notification-container.position-top-right {
                    top: 20px;
                    right: 20px;
                }

                .notification-container.position-top-left {
                    top: 20px;
                    left: 20px;
                }

                .notification-container.position-bottom-right {
                    bottom: 20px;
                    right: 20px;
                }

                .notification-container.position-bottom-left {
                    bottom: 20px;
                    left: 20px;
                }

                .notification-container.position-top-center {
                    top: 20px;
                    left: 50%;
                    transform: translateX(-50%);
                }

                .notification-container.position-bottom-center {
                    bottom: 20px;
                    left: 50%;
                    transform: translateX(-50%);
                }

                .notification-toast {
                    pointer-events: auto;
                    margin-bottom: ${NOTIFICATION_CONFIG.stackSpacing}px;
                    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
                    border-radius: 8px;
                    overflow: hidden;
                    max-width: 100%;
                }

                .notification-toast.anim-slide {
                    animation: slideIn 0.3s ease-out;
                }

                .notification-toast.anim-slide.removing {
                    animation: slideOut 0.3s ease-in;
                }

                .notification-toast.anim-fade {
                    animation: fadeIn 0.3s ease-out;
                }

                .notification-toast.anim-fade.removing {
                    animation: fadeOut 0.3s ease-in;
                }

                .notification-toast.anim-bounce {
                    animation: bounceIn 0.5s cubic-bezier(0.68, -0.55, 0.265, 1.55);
                }

                .notification-toast.anim-bounce.removing {
                    animation: fadeOut 0.3s ease-in;
                }

                .notification-icon {
                    font-size: 1.5rem;
                    line-height: 1;
                }

                .notification-progress {
                    position: absolute;
                    bottom: 0;
                    left: 0;
                    height: 3px;
                    background-color: rgba(255, 255, 255, 0.3);
                    transition: width linear;
                }

                .notification-actions {
                    display: flex;
                    gap: 8px;
                    margin-top: 8px;
                }

                .notification-actions .btn {
                    font-size: 0.875rem;
                    padding: 0.25rem 0.75rem;
                }

                @keyframes slideIn {
                    from {
                        transform: translateX(100%);
                        opacity: 0;
                    }
                    to {
                        transform: translateX(0);
                        opacity: 1;
                    }
                }

                @keyframes slideOut {
                    from {
                        transform: translateX(0);
                        opacity: 1;
                    }
                    to {
                        transform: translateX(100%);
                        opacity: 0;
                    }
                }

                @keyframes fadeIn {
                    from {
                        opacity: 0;
                        transform: scale(0.9);
                    }
                    to {
                        opacity: 1;
                        transform: scale(1);
                    }
                }

                @keyframes fadeOut {
                    from {
                        opacity: 1;
                        transform: scale(1);
                    }
                    to {
                        opacity: 0;
                        transform: scale(0.9);
                    }
                }

                @keyframes bounceIn {
                    0% {
                        transform: scale(0.3);
                        opacity: 0;
                    }
                    50% {
                        transform: scale(1.05);
                    }
                    70% {
                        transform: scale(0.9);
                    }
                    100% {
                        transform: scale(1);
                        opacity: 1;
                    }
                }
            `;
            document.head.appendChild(style);
        }

        document.body.appendChild(container);
    }

    return container;
}

/**
 * Escape HTML to prevent XSS
 */
function escapeHtml(text) {
    if (!text) return '';
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

/**
 * Remove a notification
 */
function removeNotification(id) {
    const notification = activeNotifications.find(n => n.id === id);
    if (!notification) return;

    // Mark as removing
    notification.element.classList.add('removing');

    // Remove from active list
    activeNotifications = activeNotifications.filter(n => n.id !== id);

    // Remove from DOM after animation
    setTimeout(() => {
        if (notification.element && notification.element.parentNode) {
            notification.element.remove();
        }

        // Process queue if there's space
        processQueue();
    }, 300);
}

/**
 * Process notification queue
 */
function processQueue() {
    while (activeNotifications.length < NOTIFICATION_CONFIG.maxVisible && notificationQueue.length > 0) {
        const notification = notificationQueue.shift();
        showNotificationInternal(notification);
    }
}

/**
 * Show notification (internal)
 */
function showNotificationInternal(options) {
    const container = ensureContainer();

    const {
        id,
        type = 'info',
        title,
        message,
        duration = NOTIFICATION_CONFIG.defaultDuration,
        closable = true,
        actions = [],
        onClose,
        showProgress = duration > 0,
    } = options;

    // Create notification element
    const notification = document.createElement('div');
    notification.className = `notification-toast alert alert-${type} anim-${NOTIFICATION_CONFIG.animation}`;
    notification.setAttribute('role', 'alert');
    notification.dataset.notificationId = id;

    // Build HTML
    let html = `
        <div class="d-flex align-items-start gap-3">
            <div class="notification-icon">
                <i class="ti ${ICON_MAP[type] || ICON_MAP.info}"></i>
            </div>
            <div class="flex-grow-1">
    `;

    if (title) {
        html += `<div class="fw-bold mb-1">${escapeHtml(title)}</div>`;
    }

    html += `<div class="notification-message">${escapeHtml(message)}</div>`;

    // Add actions if any
    if (actions.length > 0) {
        html += '<div class="notification-actions">';
        actions.forEach((action, index) => {
            html += `
                <button type="button" class="btn btn-sm btn-${action.variant || 'light'}"
                        data-action-index="${index}">
                    ${escapeHtml(action.label)}
                </button>
            `;
        });
        html += '</div>';
    }

    html += '</div>';

    // Add close button if closable
    if (closable) {
        html += `
            <button type="button" class="btn-close" aria-label="Close"></button>
        `;
    }

    html += '</div>';

    // Add progress bar if duration > 0
    if (showProgress && duration > 0) {
        html += '<div class="notification-progress" style="width: 100%"></div>';
    }

    notification.innerHTML = html;

    // Add event listeners
    if (closable) {
        const closeBtn = notification.querySelector('.btn-close');
        closeBtn.addEventListener('click', () => {
            removeNotification(id);
            if (onClose) onClose();
        });
    }

    // Add action listeners
    actions.forEach((action, index) => {
        const btn = notification.querySelector(`[data-action-index="${index}"]`);
        if (btn) {
            btn.addEventListener('click', () => {
                if (action.onClick) action.onClick();
                if (action.closeOnClick !== false) {
                    removeNotification(id);
                }
            });
        }
    });

    // Append to container
    container.appendChild(notification);

    // Track active notification
    const notificationObj = {
        id,
        element: notification,
        timer: null,
    };
    activeNotifications.push(notificationObj);

    // Auto-dismiss after duration
    if (duration > 0) {
        // Animate progress bar
        const progressBar = notification.querySelector('.notification-progress');
        if (progressBar) {
            // Force reflow to restart animation
            void progressBar.offsetWidth;
            progressBar.style.transition = `width ${duration}ms linear`;
            progressBar.style.width = '0%';
        }

        notificationObj.timer = setTimeout(() => {
            removeNotification(id);
            if (onClose) onClose();
        }, duration);
    }

    return id;
}

/**
 * Show a notification
 *
 * @param {Object} options - Notification options
 * @param {string} options.type - Type: success, error, warning, info
 * @param {string} options.title - Optional title
 * @param {string} options.message - Message to display
 * @param {number} options.duration - Duration in ms (0 = persistent)
 * @param {boolean} options.closable - Show close button
 * @param {Array} options.actions - Array of action buttons {label, onClick, variant, closeOnClick}
 * @param {Function} options.onClose - Callback when closed
 * @param {boolean} options.showProgress - Show progress bar
 *
 * @returns {number} Notification ID
 *
 * @example
 * showNotification({
 *   type: 'success',
 *   title: 'Success!',
 *   message: 'Configuration saved',
 *   duration: 5000
 * });
 *
 * @example
 * showNotification({
 *   type: 'warning',
 *   message: 'Are you sure?',
 *   duration: 0,
 *   actions: [
 *     { label: 'Confirm', variant: 'danger', onClick: () => deleteItem() },
 *     { label: 'Cancel', variant: 'secondary' }
 *   ]
 * });
 */
export function showNotification(options) {
    const id = ++notificationIdCounter;
    const notification = { id, ...options };

    // Add to queue if max visible reached
    if (activeNotifications.length >= NOTIFICATION_CONFIG.maxVisible) {
        notificationQueue.push(notification);
        return id;
    }

    // Show immediately
    return showNotificationInternal(notification);
}

/**
 * Shorthand methods for common notification types
 */

export function notifySuccess(message, title = null, duration = 5000) {
    return showNotification({ type: 'success', title, message, duration });
}

export function notifyError(message, title = 'Erreur', duration = 8000) {
    return showNotification({ type: 'error', title, message, duration });
}

export function notifyWarning(message, title = 'Attention', duration = 6000) {
    return showNotification({ type: 'warning', title, message, duration });
}

export function notifyInfo(message, title = null, duration = 5000) {
    return showNotification({ type: 'info', title, message, duration });
}

/**
 * Close a specific notification
 */
export function closeNotification(id) {
    removeNotification(id);
}

/**
 * Close all notifications
 */
export function closeAllNotifications() {
    [...activeNotifications].forEach(n => removeNotification(n.id));
    notificationQueue = [];
}

/**
 * Configure notification system
 */
export function configureNotifications(config) {
    Object.assign(NOTIFICATION_CONFIG, config);
}

/**
 * Get active notifications count
 */
export function getActiveNotificationsCount() {
    return activeNotifications.length;
}

/**
 * Get queued notifications count
 */
export function getQueuedNotificationsCount() {
    return notificationQueue.length;
}

// Export for global use if needed
if (typeof window !== 'undefined') {
    window.showNotification = showNotification;
    window.notifySuccess = notifySuccess;
    window.notifyError = notifyError;
    window.notifyWarning = notifyWarning;
    window.notifyInfo = notifyInfo;
    window.closeNotification = closeNotification;
    window.closeAllNotifications = closeAllNotifications;
}
