/**
 * @file i18n.js
 * @brief Internationalization (i18n) system with support for FR and EN
 * @description Provides translation functions with dynamic language switching
 *              and localStorage persistence
 */

/**
 * Available languages
 */
const LANGUAGES = {
    FR: 'fr',
    EN: 'en',
};

/**
 * Storage key for language preference
 */
const STORAGE_KEY = 'tinybms-language';

/**
 * Current language
 */
let currentLanguage = LANGUAGES.FR;

/**
 * Translation dictionaries
 */
const translations = {
    fr: {},
    en: {},
};

/**
 * Loaded state
 */
let isLoaded = false;

/**
 * Get browser language preference
 */
function getBrowserLanguage() {
    const lang = navigator.language || navigator.userLanguage;
    if (lang.startsWith('en')) return LANGUAGES.EN;
    if (lang.startsWith('fr')) return LANGUAGES.FR;
    return LANGUAGES.FR; // Default to French
}

/**
 * Translate a key
 *
 * @param {string} key - Translation key (can use dot notation: 'common.save')
 * @param {Object} params - Parameters to interpolate into translation
 * @param {string} fallback - Fallback text if key not found
 *
 * @returns {string} Translated text
 *
 * @example
 * t('common.save'); // → "Enregistrer"
 * t('alerts.count', { count: 5 }); // → "5 alertes"
 * t('unknown.key', {}, 'Default text'); // → "Default text"
 */
export function t(key, params = {}, fallback = null) {
    // Get translation for current language
    let translation = getNestedValue(translations[currentLanguage], key);

    // Fallback to French if not found
    if (translation === undefined && currentLanguage !== LANGUAGES.FR) {
        translation = getNestedValue(translations[LANGUAGES.FR], key);
    }

    // Use fallback or return key if still not found
    if (translation === undefined) {
        if (fallback !== null) return fallback;
        console.warn(`Translation missing for key: ${key}`);
        return key;
    }

    // Interpolate parameters
    return interpolate(translation, params);
}

/**
 * Get nested value from object using dot notation
 */
function getNestedValue(obj, path) {
    const keys = path.split('.');
    let value = obj;

    for (const key of keys) {
        if (value === undefined || value === null) return undefined;
        value = value[key];
    }

    return value;
}

/**
 * Interpolate parameters into translation string
 * Supports {{param}} syntax
 */
function interpolate(text, params) {
    if (typeof text !== 'string') return text;

    return text.replace(/\{\{(\w+)\}\}/g, (match, key) => {
        return params.hasOwnProperty(key) ? params[key] : match;
    });
}

/**
 * Set current language
 *
 * @param {string} language - Language code ('fr' or 'en')
 * @param {boolean} persist - Save to localStorage
 *
 * @example
 * setLanguage('en');
 * setLanguage('fr', true);
 */
export function setLanguage(language, persist = true) {
    if (!Object.values(LANGUAGES).includes(language)) {
        console.warn(`Invalid language: ${language}. Using French.`);
        language = LANGUAGES.FR;
    }

    currentLanguage = language;

    // Update HTML lang attribute
    document.documentElement.lang = language;

    // Save to localStorage
    if (persist) {
        try {
            localStorage.setItem(STORAGE_KEY, language);
        } catch (e) {
            console.warn('Failed to save language preference:', e);
        }
    }

    // Trigger custom event
    const event = new CustomEvent('languagechange', {
        detail: { language }
    });
    window.dispatchEvent(event);

    // Update page if loaded
    if (isLoaded) {
        updatePage();
    }
}

/**
 * Get current language
 *
 * @returns {string} Current language code
 */
export function getLanguage() {
    return currentLanguage;
}

/**
 * Load translations from embedded dictionaries
 *
 * @param {Object} fr - French translations
 * @param {Object} en - English translations
 */
export function loadTranslations(fr = {}, en = {}) {
    translations.fr = { ...translations.fr, ...fr };
    translations.en = { ...translations.en, ...en };
    isLoaded = true;
}

/**
 * Update page elements with data-i18n attribute
 */
function updatePage() {
    const elements = document.querySelectorAll('[data-i18n]');

    elements.forEach(element => {
        const key = element.dataset.i18n;
        const params = element.dataset.i18nParams
            ? JSON.parse(element.dataset.i18nParams)
            : {};

        const translated = t(key, params);

        // Update based on element type
        if (element.tagName === 'INPUT' && element.hasAttribute('placeholder')) {
            element.placeholder = translated;
        } else if (element.tagName === 'INPUT' && element.type === 'button') {
            element.value = translated;
        } else {
            element.textContent = translated;
        }
    });
}

/**
 * Initialize i18n system
 *
 * @param {Object} options - Options
 * @param {string} options.defaultLanguage - Default language if no preference
 * @param {boolean} options.respectBrowser - Use browser language if no saved preference
 * @param {Object} options.translations - Initial translations {fr: {...}, en: {...}}
 * @param {boolean} options.createSelector - Auto-create language selector
 * @param {Object} options.selectorOptions - Options for language selector
 *
 * @example
 * initializeI18n({
 *   defaultLanguage: 'fr',
 *   respectBrowser: true,
 *   translations: {
 *     fr: { common: { save: 'Enregistrer' } },
 *     en: { common: { save: 'Save' } }
 *   },
 *   createSelector: true
 * });
 */
export function initializeI18n(options = {}) {
    const {
        defaultLanguage = LANGUAGES.FR,
        respectBrowser = true,
        translations: initialTranslations = {},
        createSelector = false,
        selectorOptions = {},
    } = options;

    // Load translations
    if (initialTranslations.fr || initialTranslations.en) {
        loadTranslations(initialTranslations.fr, initialTranslations.en);
    }

    // Determine language to use
    let language = defaultLanguage;

    // Try to load from localStorage
    try {
        const stored = localStorage.getItem(STORAGE_KEY);
        if (stored && Object.values(LANGUAGES).includes(stored)) {
            language = stored;
        } else if (respectBrowser) {
            language = getBrowserLanguage();
        }
    } catch (e) {
        console.warn('Failed to load language preference:', e);
        if (respectBrowser) {
            language = getBrowserLanguage();
        }
    }

    // Set language
    setLanguage(language, false);

    // Create language selector if requested
    if (createSelector) {
        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', () => {
                createLanguageSelector(selectorOptions);
            });
        } else {
            createLanguageSelector(selectorOptions);
        }
    }

    // Auto-update page on language change
    window.addEventListener('languagechange', updatePage);

    // Initial page update
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', updatePage);
    } else {
        updatePage();
    }
}

/**
 * Create language selector dropdown
 *
 * @param {Object} options - Options
 * @param {string} options.targetSelector - Selector where to insert selector
 * @param {string} options.position - Position: 'before', 'after', 'prepend', 'append'
 * @param {string} options.className - Additional CSS classes
 * @param {boolean} options.showFlag - Show flag emoji
 *
 * @example
 * createLanguageSelector({
 *   targetSelector: '.navbar-nav',
 *   position: 'append',
 *   showFlag: true
 * });
 */
export function createLanguageSelector(options = {}) {
    const {
        targetSelector = 'body',
        position = 'append',
        className = 'dropdown',
        showFlag = true,
    } = options;

    const target = document.querySelector(targetSelector);
    if (!target) {
        console.warn(`Language selector target not found: ${targetSelector}`);
        return null;
    }

    const flags = {
        fr: '🇫🇷',
        en: '🇬🇧',
    };

    const names = {
        fr: 'Français',
        en: 'English',
    };

    // Create dropdown
    const container = document.createElement('div');
    container.className = className;
    container.id = 'language-selector';

    container.innerHTML = `
        <button class="btn btn-ghost-secondary dropdown-toggle" type="button"
                data-bs-toggle="dropdown" aria-expanded="false">
            ${showFlag ? `<span class="me-2">${flags[currentLanguage]}</span>` : ''}
            <span>${names[currentLanguage]}</span>
        </button>
        <ul class="dropdown-menu">
            ${Object.values(LANGUAGES).map(lang => `
                <li>
                    <a class="dropdown-item ${lang === currentLanguage ? 'active' : ''}"
                       href="#" data-lang="${lang}">
                        ${showFlag ? `<span class="me-2">${flags[lang]}</span>` : ''}
                        ${names[lang]}
                    </a>
                </li>
            `).join('')}
        </ul>
    `;

    // Add click handlers
    container.querySelectorAll('[data-lang]').forEach(link => {
        link.addEventListener('click', (e) => {
            e.preventDefault();
            const lang = link.dataset.lang;
            setLanguage(lang);

            // Update button and active state
            const button = container.querySelector('.dropdown-toggle span');
            button.textContent = names[lang];

            if (showFlag) {
                const flagSpan = container.querySelector('.dropdown-toggle span.me-2');
                if (flagSpan) flagSpan.textContent = flags[lang];
            }

            container.querySelectorAll('.dropdown-item').forEach(item => {
                item.classList.remove('active');
            });
            link.classList.add('active');
        });
    });

    // Insert selector
    switch (position) {
        case 'before':
            target.parentNode.insertBefore(container, target);
            break;
        case 'after':
            target.parentNode.insertBefore(container, target.nextSibling);
            break;
        case 'prepend':
            target.insertBefore(container, target.firstChild);
            break;
        case 'append':
        default:
            target.appendChild(container);
    }

    return container;
}

/**
 * Listen to language changes
 *
 * @param {Function} callback - Callback function(language)
 * @returns {Function} Cleanup function
 *
 * @example
 * const cleanup = onLanguageChange((lang) => {
 *   console.log(`Language changed to: ${lang}`);
 * });
 */
export function onLanguageChange(callback) {
    const handler = (event) => {
        callback(event.detail.language);
    };

    window.addEventListener('languagechange', handler);

    return () => {
        window.removeEventListener('languagechange', handler);
    };
}

/**
 * Common translations for TinyBMS
 */
const commonTranslations = {
    fr: {
        common: {
            save: 'Enregistrer',
            cancel: 'Annuler',
            delete: 'Supprimer',
            edit: 'Modifier',
            close: 'Fermer',
            loading: 'Chargement...',
            error: 'Erreur',
            success: 'Succès',
            warning: 'Attention',
            info: 'Information',
            confirm: 'Confirmer',
            yes: 'Oui',
            no: 'Non',
            search: 'Rechercher',
            filter: 'Filtrer',
            refresh: 'Actualiser',
            settings: 'Paramètres',
        },
        battery: {
            voltage: 'Tension',
            current: 'Courant',
            temperature: 'Température',
            soc: 'État de charge',
            soh: 'État de santé',
            cells: 'Cellules',
            pack: 'Pack',
        },
        alerts: {
            active: 'Alertes actives',
            history: 'Historique',
            acknowledge: 'Acquitter',
            clear: 'Effacer',
            count: '{{count}} alerte(s)',
        },
        config: {
            mqtt: 'Configuration MQTT',
            wifi: 'Configuration WiFi',
            system: 'Configuration système',
            apply: 'Appliquer',
            reset: 'Réinitialiser',
        },
    },
    en: {
        common: {
            save: 'Save',
            cancel: 'Cancel',
            delete: 'Delete',
            edit: 'Edit',
            close: 'Close',
            loading: 'Loading...',
            error: 'Error',
            success: 'Success',
            warning: 'Warning',
            info: 'Information',
            confirm: 'Confirm',
            yes: 'Yes',
            no: 'No',
            search: 'Search',
            filter: 'Filter',
            refresh: 'Refresh',
            settings: 'Settings',
        },
        battery: {
            voltage: 'Voltage',
            current: 'Current',
            temperature: 'Temperature',
            soc: 'State of Charge',
            soh: 'State of Health',
            cells: 'Cells',
            pack: 'Pack',
        },
        alerts: {
            active: 'Active Alerts',
            history: 'History',
            acknowledge: 'Acknowledge',
            clear: 'Clear',
            count: '{{count}} alert(s)',
        },
        config: {
            mqtt: 'MQTT Configuration',
            wifi: 'WiFi Configuration',
            system: 'System Configuration',
            apply: 'Apply',
            reset: 'Reset',
        },
    },
};

// Load common translations
loadTranslations(commonTranslations.fr, commonTranslations.en);

// Export constants and for global use
export { LANGUAGES };

if (typeof window !== 'undefined') {
    window.i18n = {
        t,
        setLanguage,
        getLanguage,
        initializeI18n,
        createLanguageSelector,
        onLanguageChange,
        loadTranslations,
        LANGUAGES,
    };
}
