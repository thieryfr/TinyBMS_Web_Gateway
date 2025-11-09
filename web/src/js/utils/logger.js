/**
 * @file logger.js
 * @brief Structured logging system with configurable levels and formatting
 * @description Provides a comprehensive logging system for frontend debugging
 *              and production monitoring with multiple output targets
 */

/**
 * Log levels (in order of severity)
 */
const LOG_LEVELS = {
    DEBUG: 0,
    INFO: 1,
    WARN: 2,
    ERROR: 3,
    NONE: 4, // Disable all logging
};

/**
 * Log level names
 */
const LEVEL_NAMES = ['DEBUG', 'INFO', 'WARN', 'ERROR', 'NONE'];

/**
 * Log level colors for console
 */
const LEVEL_COLORS = {
    DEBUG: '#6c757d', // Gray
    INFO: '#0dcaf0',  // Cyan
    WARN: '#ffc107',  // Yellow
    ERROR: '#dc3545', // Red
};

/**
 * Log level emoji icons
 */
const LEVEL_ICONS = {
    DEBUG: '🔍',
    INFO: 'ℹ️',
    WARN: '⚠️',
    ERROR: '❌',
};

/**
 * Logger configuration
 */
const config = {
    level: LOG_LEVELS.INFO,
    enableConsole: true,
    enableStorage: false,
    storageKey: 'tinybms-logs',
    maxStoredLogs: 100,
    timestampFormat: 'iso', // 'iso', 'locale', 'time'
    includeStackTrace: true,
    groupSimilarLogs: true,
    outputs: [], // Custom output functions
};

/**
 * Log storage
 */
let logHistory = [];
let groupedLogs = new Map();

/**
 * Format timestamp
 */
function formatTimestamp(date, format) {
    switch (format) {
        case 'iso':
            return date.toISOString();
        case 'locale':
            return date.toLocaleString('fr-FR');
        case 'time':
            return date.toLocaleTimeString('fr-FR');
        default:
            return date.toISOString();
    }
}

/**
 * Create log entry
 */
function createLogEntry(level, message, data = {}, error = null) {
    const entry = {
        level: LEVEL_NAMES[level],
        message,
        timestamp: new Date(),
        data,
    };

    if (error && config.includeStackTrace) {
        entry.error = {
            message: error.message,
            stack: error.stack,
            name: error.name,
        };
    }

    // Add context if available
    if (typeof window !== 'undefined') {
        entry.context = {
            url: window.location.href,
            userAgent: navigator.userAgent,
        };
    }

    return entry;
}

/**
 * Write to console
 */
function writeToConsole(entry) {
    const levelName = entry.level;
    const color = LEVEL_COLORS[levelName];
    const icon = LEVEL_ICONS[levelName];
    const timestamp = formatTimestamp(entry.timestamp, config.timestampFormat);

    // Format console message
    const style = `color: ${color}; font-weight: bold;`;

    console.groupCollapsed(
        `%c${icon} [${levelName}]%c ${timestamp} - ${entry.message}`,
        style,
        'color: inherit; font-weight: normal;'
    );

    if (Object.keys(entry.data).length > 0) {
        console.log('Data:', entry.data);
    }

    if (entry.error) {
        console.error('Error:', entry.error.message);
        if (entry.error.stack) {
            console.log('Stack:', entry.error.stack);
        }
    }

    if (entry.context) {
        console.log('Context:', entry.context);
    }

    console.groupEnd();
}

/**
 * Write to localStorage
 */
function writeToStorage(entry) {
    try {
        // Add to history
        logHistory.push(entry);

        // Limit history size
        if (logHistory.length > config.maxStoredLogs) {
            logHistory = logHistory.slice(-config.maxStoredLogs);
        }

        // Save to localStorage
        localStorage.setItem(config.storageKey, JSON.stringify(logHistory));
    } catch (error) {
        // localStorage might be full or disabled
        console.warn('Failed to store log:', error);
    }
}

/**
 * Check if log should be grouped
 */
function shouldGroup(entry) {
    if (!config.groupSimilarLogs) return false;

    const key = `${entry.level}:${entry.message}`;
    const now = Date.now();

    if (groupedLogs.has(key)) {
        const group = groupedLogs.get(key);

        // Group if within 5 seconds
        if (now - group.lastTimestamp < 5000) {
            group.count++;
            group.lastTimestamp = now;
            return true;
        }
    }

    // Create new group
    groupedLogs.set(key, {
        count: 1,
        lastTimestamp: now,
    });

    return false;
}

/**
 * Log function
 */
function log(level, message, data = {}, error = null) {
    // Check if level is enabled
    if (level < config.level) {
        return;
    }

    // Create log entry
    const entry = createLogEntry(level, message, data, error);

    // Check grouping
    if (shouldGroup(entry)) {
        const key = `${entry.level}:${entry.message}`;
        const group = groupedLogs.get(key);

        if (config.enableConsole) {
            console.log(
                `%c[${entry.level}] ${entry.message} (×${group.count})`,
                `color: ${LEVEL_COLORS[entry.level]}`
            );
        }

        return;
    }

    // Write to console
    if (config.enableConsole) {
        writeToConsole(entry);
    }

    // Write to storage
    if (config.enableStorage) {
        writeToStorage(entry);
    }

    // Write to custom outputs
    config.outputs.forEach(outputFn => {
        try {
            outputFn(entry);
        } catch (error) {
            console.error('Logger output function failed:', error);
        }
    });
}

/**
 * Public logging methods
 */

/**
 * Log debug message
 * @param {string} message - Log message
 * @param {Object} data - Additional data
 *
 * @example
 * logger.debug('WebSocket connected', { url: 'ws://...' });
 */
export function debug(message, data = {}) {
    log(LOG_LEVELS.DEBUG, message, data);
}

/**
 * Log info message
 * @param {string} message - Log message
 * @param {Object} data - Additional data
 *
 * @example
 * logger.info('Configuration loaded', { config });
 */
export function info(message, data = {}) {
    log(LOG_LEVELS.INFO, message, data);
}

/**
 * Log warning message
 * @param {string} message - Log message
 * @param {Object} data - Additional data
 *
 * @example
 * logger.warn('API response slow', { duration: 3000 });
 */
export function warn(message, data = {}) {
    log(LOG_LEVELS.WARN, message, data);
}

/**
 * Log error message
 * @param {string} message - Log message
 * @param {Error|Object} errorOrData - Error object or additional data
 *
 * @example
 * logger.error('Failed to save config', error);
 * logger.error('Validation failed', { errors: [...] });
 */
export function error(message, errorOrData = {}) {
    if (errorOrData instanceof Error) {
        log(LOG_LEVELS.ERROR, message, {}, errorOrData);
    } else {
        log(LOG_LEVELS.ERROR, message, errorOrData);
    }
}

/**
 * Configure logger
 *
 * @param {Object} options - Configuration options
 * @param {string} options.level - Log level: 'DEBUG', 'INFO', 'WARN', 'ERROR', 'NONE'
 * @param {boolean} options.enableConsole - Enable console output
 * @param {boolean} options.enableStorage - Enable localStorage storage
 * @param {number} options.maxStoredLogs - Maximum logs to store
 * @param {string} options.timestampFormat - Timestamp format: 'iso', 'locale', 'time'
 * @param {boolean} options.includeStackTrace - Include stack traces for errors
 * @param {boolean} options.groupSimilarLogs - Group repeated logs
 *
 * @example
 * logger.configure({
 *   level: 'DEBUG',
 *   enableStorage: true,
 *   maxStoredLogs: 500
 * });
 */
export function configure(options = {}) {
    if (options.level !== undefined) {
        if (typeof options.level === 'string') {
            const levelIndex = LEVEL_NAMES.indexOf(options.level.toUpperCase());
            if (levelIndex !== -1) {
                config.level = levelIndex;
            }
        } else {
            config.level = options.level;
        }
    }

    if (options.enableConsole !== undefined) {
        config.enableConsole = options.enableConsole;
    }

    if (options.enableStorage !== undefined) {
        config.enableStorage = options.enableStorage;

        // Load existing logs if enabling storage
        if (config.enableStorage) {
            loadLogsFromStorage();
        }
    }

    if (options.storageKey !== undefined) {
        config.storageKey = options.storageKey;
    }

    if (options.maxStoredLogs !== undefined) {
        config.maxStoredLogs = options.maxStoredLogs;
    }

    if (options.timestampFormat !== undefined) {
        config.timestampFormat = options.timestampFormat;
    }

    if (options.includeStackTrace !== undefined) {
        config.includeStackTrace = options.includeStackTrace;
    }

    if (options.groupSimilarLogs !== undefined) {
        config.groupSimilarLogs = options.groupSimilarLogs;
    }
}

/**
 * Add custom output function
 *
 * @param {Function} outputFn - Function(logEntry)
 *
 * @example
 * logger.addOutput((entry) => {
 *   if (entry.level === 'ERROR') {
 *     sendToErrorTracking(entry);
 *   }
 * });
 */
export function addOutput(outputFn) {
    if (typeof outputFn === 'function') {
        config.outputs.push(outputFn);
    }
}

/**
 * Remove custom output function
 */
export function removeOutput(outputFn) {
    const index = config.outputs.indexOf(outputFn);
    if (index !== -1) {
        config.outputs.splice(index, 1);
    }
}

/**
 * Get log history
 *
 * @param {Object} filters - Filter options
 * @param {string} filters.level - Filter by level
 * @param {number} filters.limit - Limit number of logs
 * @param {string} filters.search - Search in messages
 *
 * @returns {Array} Filtered log entries
 *
 * @example
 * const errors = logger.getHistory({ level: 'ERROR' });
 * const recent = logger.getHistory({ limit: 10 });
 */
export function getHistory(filters = {}) {
    let logs = [...logHistory];

    // Filter by level
    if (filters.level) {
        logs = logs.filter(entry => entry.level === filters.level.toUpperCase());
    }

    // Search in messages
    if (filters.search) {
        const search = filters.search.toLowerCase();
        logs = logs.filter(entry =>
            entry.message.toLowerCase().includes(search)
        );
    }

    // Limit
    if (filters.limit) {
        logs = logs.slice(-filters.limit);
    }

    return logs;
}

/**
 * Clear log history
 */
export function clearHistory() {
    logHistory = [];
    groupedLogs.clear();

    if (config.enableStorage) {
        try {
            localStorage.removeItem(config.storageKey);
        } catch (error) {
            console.warn('Failed to clear stored logs:', error);
        }
    }

    info('Log history cleared');
}

/**
 * Load logs from localStorage
 */
function loadLogsFromStorage() {
    try {
        const stored = localStorage.getItem(config.storageKey);
        if (stored) {
            logHistory = JSON.parse(stored);

            // Convert timestamp strings back to Date objects
            logHistory.forEach(entry => {
                entry.timestamp = new Date(entry.timestamp);
            });

            info(`Loaded ${logHistory.length} logs from storage`);
        }
    } catch (error) {
        warn('Failed to load logs from storage', { error: error.message });
    }
}

/**
 * Export logs as JSON
 *
 * @returns {string} JSON string of logs
 */
export function exportLogsJSON() {
    return JSON.stringify(logHistory, null, 2);
}

/**
 * Export logs as CSV
 *
 * @returns {string} CSV string of logs
 */
export function exportLogsCSV() {
    const headers = ['Timestamp', 'Level', 'Message', 'Data', 'Error'];
    const rows = logHistory.map(entry => [
        formatTimestamp(entry.timestamp, 'iso'),
        entry.level,
        entry.message,
        JSON.stringify(entry.data || {}),
        entry.error ? entry.error.message : '',
    ]);

    return [
        headers.join(','),
        ...rows.map(row => row.map(cell => `"${cell}"`).join(','))
    ].join('\n');
}

/**
 * Download logs as file
 *
 * @param {string} format - Format: 'json' or 'csv'
 */
export function downloadLogs(format = 'json') {
    let content, filename, type;

    if (format === 'csv') {
        content = exportLogsCSV();
        filename = `tinybms-logs-${Date.now()}.csv`;
        type = 'text/csv';
    } else {
        content = exportLogsJSON();
        filename = `tinybms-logs-${Date.now()}.json`;
        type = 'application/json';
    }

    const blob = new Blob([content], { type });
    const url = URL.createObjectURL(blob);

    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.click();

    URL.revokeObjectURL(url);

    info(`Logs downloaded: ${filename}`);
}

/**
 * Get logger statistics
 */
export function getStats() {
    const stats = {
        total: logHistory.length,
        byLevel: {},
    };

    LEVEL_NAMES.forEach(level => {
        stats.byLevel[level] = logHistory.filter(e => e.level === level).length;
    });

    return stats;
}

/**
 * Create scoped logger (prefix all messages)
 *
 * @param {string} scope - Scope name
 * @returns {Object} Scoped logger instance
 *
 * @example
 * const moduleLogger = logger.createScope('MyModule');
 * moduleLogger.info('Module initialized'); // → [INFO] [MyModule] Module initialized
 */
export function createScope(scope) {
    return {
        debug: (msg, data) => debug(`[${scope}] ${msg}`, data),
        info: (msg, data) => info(`[${scope}] ${msg}`, data),
        warn: (msg, data) => warn(`[${scope}] ${msg}`, data),
        error: (msg, errorOrData) => error(`[${scope}] ${msg}`, errorOrData),
    };
}

// Export constants
export { LOG_LEVELS, LEVEL_NAMES };

// Export for global use
if (typeof window !== 'undefined') {
    window.logger = {
        debug,
        info,
        warn,
        error,
        configure,
        addOutput,
        removeOutput,
        getHistory,
        clearHistory,
        exportLogsJSON,
        exportLogsCSV,
        downloadLogs,
        getStats,
        createScope,
        LOG_LEVELS,
    };
}
