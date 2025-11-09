/**
 * @file validation.js
 * @brief Client-side form validation utilities
 * @description Provides validation functions for common input types
 */

/**
 * Validation rules for different input types
 */
export const ValidationRules = {
    // Network
    ipv4: {
        pattern: /^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/,
        message: 'Adresse IPv4 invalide (ex: 192.168.1.1)',
    },
    hostname: {
        pattern: /^[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(\.[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$/,
        message: 'Nom d\'hôte invalide',
    },
    port: {
        min: 1,
        max: 65535,
        message: 'Port doit être entre 1 et 65535',
    },
    macAddress: {
        pattern: /^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$/,
        message: 'Adresse MAC invalide (ex: 00:11:22:33:44:55)',
    },

    // MQTT
    mqttTopic: {
        pattern: /^[^#+\0]+$/,
        message: 'Topic MQTT invalide (caractères # + interdits)',
    },
    mqttQos: {
        min: 0,
        max: 2,
        message: 'QoS MQTT doit être 0, 1 ou 2',
    },

    // UART/Serial
    baudrate: {
        enum: [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200],
        message: 'Baudrate invalide',
    },
    gpioPin: {
        min: 0,
        max: 39,
        message: 'GPIO doit être entre 0 et 39 (ESP32)',
    },

    // CAN Bus
    canSpeed: {
        enum: [25000, 50000, 100000, 125000, 250000, 500000, 800000, 1000000],
        message: 'Vitesse CAN invalide',
    },

    // General
    nonEmpty: {
        minLength: 1,
        message: 'Ce champ est requis',
    },
    deviceName: {
        pattern: /^[a-zA-Z0-9_-]{1,32}$/,
        message: 'Nom invalide (1-32 caractères alphanumériques, _ ou -)',
    },
};

/**
 * Validate a single value against rules
 * @param {any} value - Value to validate
 * @param {Object} rules - Validation rules
 * @returns {Object} { valid: boolean, error: string|null }
 */
export function validate(value, rules) {
    // Check required
    if (rules.required && (value === null || value === undefined || value === '')) {
        return { valid: false, error: rules.message || 'Ce champ est requis' };
    }

    // Skip other validations if empty and not required
    if (!rules.required && (value === null || value === undefined || value === '')) {
        return { valid: true, error: null };
    }

    // Check pattern (regex)
    if (rules.pattern && !rules.pattern.test(value)) {
        return { valid: false, error: rules.message || 'Format invalide' };
    }

    // Check min/max for numbers
    if (typeof value === 'number' || !isNaN(Number(value))) {
        const numValue = Number(value);

        if (rules.min !== undefined && numValue < rules.min) {
            return { valid: false, error: rules.message || `Valeur minimale: ${rules.min}` };
        }

        if (rules.max !== undefined && numValue > rules.max) {
            return { valid: false, error: rules.message || `Valeur maximale: ${rules.max}` };
        }
    }

    // Check minLength/maxLength for strings
    if (typeof value === 'string') {
        if (rules.minLength !== undefined && value.length < rules.minLength) {
            return { valid: false, error: rules.message || `Longueur minimale: ${rules.minLength}` };
        }

        if (rules.maxLength !== undefined && value.length > rules.maxLength) {
            return { valid: false, error: rules.message || `Longueur maximale: ${rules.maxLength}` };
        }
    }

    // Check enum (allowed values)
    if (rules.enum && !rules.enum.includes(value) && !rules.enum.includes(Number(value))) {
        return { valid: false, error: rules.message || `Valeur non autorisée` };
    }

    // Custom validator function
    if (rules.validator && typeof rules.validator === 'function') {
        const customResult = rules.validator(value);
        if (customResult !== true) {
            return { valid: false, error: customResult || rules.message || 'Validation échouée' };
        }
    }

    return { valid: true, error: null };
}

/**
 * Validate an entire form
 * @param {HTMLFormElement} form - Form element
 * @param {Object} rulesMap - Map of field names to validation rules
 * @returns {Object} { valid: boolean, errors: Object }
 *
 * @example
 * const result = validateForm(form, {
 *   device_name: ValidationRules.deviceName,
 *   uart_baudrate: ValidationRules.baudrate,
 *   mqtt_port: ValidationRules.port
 * });
 */
export function validateForm(form, rulesMap) {
    const errors = {};
    let isValid = true;

    for (const [fieldName, rules] of Object.entries(rulesMap)) {
        const input = form.elements[fieldName];

        if (!input) {
            console.warn(`Field "${fieldName}" not found in form`);
            continue;
        }

        let value = input.value;

        // Handle checkboxes
        if (input.type === 'checkbox') {
            value = input.checked;
        }

        // Handle numeric inputs
        if (input.type === 'number') {
            value = input.value === '' ? '' : Number(input.value);
        }

        const result = validate(value, rules);

        if (!result.valid) {
            errors[fieldName] = result.error;
            isValid = false;

            // Add visual feedback
            input.classList.add('is-invalid');

            // Add/update error message
            let feedback = input.parentElement.querySelector('.invalid-feedback');
            if (!feedback) {
                feedback = document.createElement('div');
                feedback.className = 'invalid-feedback';
                input.parentElement.appendChild(feedback);
            }
            feedback.textContent = result.error;
        } else {
            // Remove error state
            input.classList.remove('is-invalid');
            const feedback = input.parentElement.querySelector('.invalid-feedback');
            if (feedback) {
                feedback.remove();
            }
        }
    }

    return { valid: isValid, errors };
}

/**
 * Attach real-time validation to form inputs
 * @param {HTMLFormElement} form - Form element
 * @param {Object} rulesMap - Map of field names to validation rules
 *
 * @example
 * attachFormValidation(form, {
 *   device_name: ValidationRules.deviceName,
 *   mqtt_port: ValidationRules.port
 * });
 */
export function attachFormValidation(form, rulesMap) {
    for (const [fieldName, rules] of Object.entries(rulesMap)) {
        const input = form.elements[fieldName];

        if (!input) {
            console.warn(`Field "${fieldName}" not found in form`);
            continue;
        }

        // Validate on blur
        input.addEventListener('blur', () => {
            let value = input.value;

            if (input.type === 'checkbox') {
                value = input.checked;
            } else if (input.type === 'number') {
                value = input.value === '' ? '' : Number(input.value);
            }

            const result = validate(value, rules);

            if (!result.valid) {
                input.classList.add('is-invalid');

                let feedback = input.parentElement.querySelector('.invalid-feedback');
                if (!feedback) {
                    feedback = document.createElement('div');
                    feedback.className = 'invalid-feedback';
                    input.parentElement.appendChild(feedback);
                }
                feedback.textContent = result.error;
            } else {
                input.classList.remove('is-invalid');
                const feedback = input.parentElement.querySelector('.invalid-feedback');
                if (feedback) {
                    feedback.remove();
                }
            }
        });

        // Clear error on input
        input.addEventListener('input', () => {
            if (input.classList.contains('is-invalid')) {
                input.classList.remove('is-invalid');
                const feedback = input.parentElement.querySelector('.invalid-feedback');
                if (feedback) {
                    feedback.remove();
                }
            }
        });
    }
}

/**
 * Sanitize string input (remove dangerous characters)
 * @param {string} input - Input string
 * @param {Object} options - Sanitization options
 * @returns {string} Sanitized string
 */
export function sanitizeInput(input, options = {}) {
    const {
        allowNewlines = false,
        allowHTML = false,
        maxLength = 1000,
    } = options;

    if (typeof input !== 'string') {
        return '';
    }

    let sanitized = input;

    // Trim whitespace
    sanitized = sanitized.trim();

    // Limit length
    if (sanitized.length > maxLength) {
        sanitized = sanitized.substring(0, maxLength);
    }

    // Remove HTML if not allowed
    if (!allowHTML) {
        const div = document.createElement('div');
        div.textContent = sanitized;
        sanitized = div.innerHTML;
    }

    // Remove newlines if not allowed
    if (!allowNewlines) {
        sanitized = sanitized.replace(/[\r\n]/g, '');
    }

    // Remove null bytes
    sanitized = sanitized.replace(/\0/g, '');

    return sanitized;
}

/**
 * Common validation patterns
 */
export const Patterns = {
    email: /^[^\s@]+@[^\s@]+\.[^\s@]+$/,
    url: /^https?:\/\/.+/,
    hexColor: /^#([A-Fa-f0-9]{6}|[A-Fa-f0-9]{3})$/,
    alphanumeric: /^[a-zA-Z0-9]+$/,
    alphanumericDash: /^[a-zA-Z0-9_-]+$/,
    numeric: /^[0-9]+$/,
    float: /^-?[0-9]+(\.[0-9]+)?$/,
};

// Export for global use
if (typeof window !== 'undefined') {
    window.ValidationRules = ValidationRules;
    window.validate = validate;
    window.validateForm = validateForm;
    window.attachFormValidation = attachFormValidation;
}
