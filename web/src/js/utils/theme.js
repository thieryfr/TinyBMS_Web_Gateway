/**
 * @file theme.js
 * @brief Dark/Light theme management with system preference support
 * @description Provides theme switching functionality with localStorage persistence
 *              and automatic detection of system preferences
 */

/**
 * Theme modes
 */
const THEMES = {
    LIGHT: 'light',
    DARK: 'dark',
    AUTO: 'auto',
};

/**
 * Storage key for theme preference
 */
const STORAGE_KEY = 'tinybms-theme-preference';

/**
 * Current theme state
 */
let currentTheme = THEMES.AUTO;
let systemPrefersDark = false;
let mediaQuery = null;

/**
 * Get system color scheme preference
 */
function getSystemPreference() {
    if (!window.matchMedia) {
        return false;
    }
    return window.matchMedia('(prefers-color-scheme: dark)').matches;
}

/**
 * Get effective theme (resolve 'auto' to light/dark)
 */
function getEffectiveTheme() {
    if (currentTheme === THEMES.AUTO) {
        return systemPrefersDark ? THEMES.DARK : THEMES.LIGHT;
    }
    return currentTheme;
}

/**
 * Apply theme to document
 */
function applyTheme(theme) {
    const effectiveTheme = theme === THEMES.AUTO
        ? (systemPrefersDark ? THEMES.DARK : THEMES.LIGHT)
        : theme;

    // Update data-theme attribute on html element
    document.documentElement.setAttribute('data-theme', effectiveTheme);

    // Also add class for Tabler compatibility
    if (effectiveTheme === THEMES.DARK) {
        document.body.classList.add('theme-dark');
        document.body.classList.remove('theme-light');
    } else {
        document.body.classList.add('theme-light');
        document.body.classList.remove('theme-dark');
    }

    // Trigger custom event
    const event = new CustomEvent('themechange', {
        detail: { theme: effectiveTheme, preference: currentTheme }
    });
    window.dispatchEvent(event);

    // Update toggle button if it exists
    updateToggleButton();
}

/**
 * Set theme preference
 *
 * @param {string} theme - Theme to set: 'light', 'dark', or 'auto'
 * @param {boolean} persist - Save to localStorage
 *
 * @example
 * setTheme('dark');
 * setTheme('auto', true);
 */
export function setTheme(theme, persist = true) {
    if (!Object.values(THEMES).includes(theme)) {
        console.warn(`Invalid theme: ${theme}. Using 'auto'.`);
        theme = THEMES.AUTO;
    }

    currentTheme = theme;

    // Save to localStorage
    if (persist) {
        try {
            localStorage.setItem(STORAGE_KEY, theme);
        } catch (e) {
            console.warn('Failed to save theme preference:', e);
        }
    }

    // Apply theme
    applyTheme(theme);
}

/**
 * Get current theme preference
 *
 * @returns {string} Current theme preference ('light', 'dark', or 'auto')
 */
export function getTheme() {
    return currentTheme;
}

/**
 * Get effective applied theme (always 'light' or 'dark', never 'auto')
 *
 * @returns {string} Effective theme ('light' or 'dark')
 */
export function getEffectiveThemeValue() {
    return getEffectiveTheme();
}

/**
 * Toggle between light and dark themes
 * (cycles through: light → dark → auto → light)
 */
export function toggleTheme() {
    const themes = [THEMES.LIGHT, THEMES.DARK, THEMES.AUTO];
    const currentIndex = themes.indexOf(currentTheme);
    const nextIndex = (currentIndex + 1) % themes.length;
    setTheme(themes[nextIndex]);
}

/**
 * Toggle between light and dark only (no auto)
 */
export function toggleThemeSimple() {
    const effectiveTheme = getEffectiveTheme();
    const nextTheme = effectiveTheme === THEMES.LIGHT ? THEMES.DARK : THEMES.LIGHT;
    setTheme(nextTheme);
}

/**
 * Update theme toggle button state
 */
function updateToggleButton() {
    const toggleBtn = document.getElementById('theme-toggle');
    if (!toggleBtn) return;

    const effectiveTheme = getEffectiveTheme();
    const icon = toggleBtn.querySelector('i');

    if (icon) {
        // Update icon
        icon.className = effectiveTheme === THEMES.DARK
            ? 'ti ti-sun'
            : 'ti ti-moon';
    }

    // Update title
    const titles = {
        [THEMES.LIGHT]: 'Activer le mode sombre',
        [THEMES.DARK]: 'Activer le mode clair',
        [THEMES.AUTO]: 'Mode automatique (système)',
    };
    toggleBtn.title = titles[currentTheme] || '';

    // Update aria-label
    toggleBtn.setAttribute('aria-label', `Thème actuel: ${effectiveTheme}`);
}

/**
 * Create theme toggle button
 *
 * @param {Object} options - Options
 * @param {string} options.targetSelector - Selector where to insert button
 * @param {string} options.position - Position: 'before', 'after', 'prepend', 'append'
 * @param {string} options.className - Additional CSS classes
 * @param {boolean} options.showLabel - Show label text
 * @param {Function} options.onClick - Custom click handler
 *
 * @example
 * createThemeToggle({
 *   targetSelector: '.navbar-nav',
 *   position: 'append',
 *   className: 'nav-link'
 * });
 */
export function createThemeToggle(options = {}) {
    const {
        targetSelector = 'body',
        position = 'append',
        className = 'btn btn-ghost-secondary',
        showLabel = false,
        onClick = null,
    } = options;

    const target = document.querySelector(targetSelector);
    if (!target) {
        console.warn(`Theme toggle target not found: ${targetSelector}`);
        return null;
    }

    // Create button
    const button = document.createElement('button');
    button.id = 'theme-toggle';
    button.type = 'button';
    button.className = className;

    const effectiveTheme = getEffectiveTheme();
    const iconClass = effectiveTheme === THEMES.DARK ? 'ti ti-sun' : 'ti ti-moon';

    button.innerHTML = `
        <i class="${iconClass}"></i>
        ${showLabel ? '<span class="ms-2">Thème</span>' : ''}
    `;

    // Add click handler
    button.addEventListener('click', () => {
        if (onClick) {
            onClick();
        } else {
            toggleThemeSimple();
        }
    });

    // Insert button
    switch (position) {
        case 'before':
            target.parentNode.insertBefore(button, target);
            break;
        case 'after':
            target.parentNode.insertBefore(button, target.nextSibling);
            break;
        case 'prepend':
            target.insertBefore(button, target.firstChild);
            break;
        case 'append':
        default:
            target.appendChild(button);
    }

    updateToggleButton();
    return button;
}

/**
 * Initialize theme system
 *
 * @param {Object} options - Options
 * @param {string} options.defaultTheme - Default theme if no preference saved
 * @param {boolean} options.respectSystem - Respect system preference when in auto mode
 * @param {boolean} options.createToggle - Auto-create toggle button
 * @param {Object} options.toggleOptions - Options for createThemeToggle
 *
 * @example
 * initializeTheme({
 *   defaultTheme: 'auto',
 *   respectSystem: true,
 *   createToggle: true,
 *   toggleOptions: {
 *     targetSelector: '.navbar-nav',
 *     position: 'append'
 *   }
 * });
 */
export function initializeTheme(options = {}) {
    const {
        defaultTheme = THEMES.AUTO,
        respectSystem = true,
        createToggle = false,
        toggleOptions = {},
    } = options;

    // Get system preference
    systemPrefersDark = getSystemPreference();

    // Set up system preference change listener
    if (respectSystem && window.matchMedia) {
        mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');

        // Modern browsers
        if (mediaQuery.addEventListener) {
            mediaQuery.addEventListener('change', (e) => {
                systemPrefersDark = e.matches;
                if (currentTheme === THEMES.AUTO) {
                    applyTheme(THEMES.AUTO);
                }
            });
        }
        // Older browsers
        else if (mediaQuery.addListener) {
            mediaQuery.addListener((e) => {
                systemPrefersDark = e.matches;
                if (currentTheme === THEMES.AUTO) {
                    applyTheme(THEMES.AUTO);
                }
            });
        }
    }

    // Load saved preference or use default
    let savedTheme = defaultTheme;
    try {
        const stored = localStorage.getItem(STORAGE_KEY);
        if (stored && Object.values(THEMES).includes(stored)) {
            savedTheme = stored;
        }
    } catch (e) {
        console.warn('Failed to load theme preference:', e);
    }

    // Apply theme
    setTheme(savedTheme, false);

    // Create toggle button if requested
    if (createToggle) {
        // Wait for DOM to be ready
        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', () => {
                createThemeToggle(toggleOptions);
            });
        } else {
            createThemeToggle(toggleOptions);
        }
    }

    // Add CSS variables for theme
    injectThemeStyles();
}

/**
 * Inject CSS variables and styles for theme support
 */
function injectThemeStyles() {
    if (document.getElementById('theme-styles')) {
        return; // Already injected
    }

    const style = document.createElement('style');
    style.id = 'theme-styles';
    style.textContent = `
        /* Theme variables */
        :root {
            --theme-transition: background-color 0.3s ease, color 0.3s ease;
        }

        /* Apply transition to common elements */
        body,
        .card,
        .navbar,
        .sidebar,
        .modal-content,
        .table,
        .form-control,
        .form-select,
        .btn {
            transition: var(--theme-transition);
        }

        /* Dark theme overrides (if Tabler doesn't provide them) */
        [data-theme="dark"] {
            color-scheme: dark;
        }

        [data-theme="dark"] body {
            --tblr-body-bg: #1a1d1e;
            --tblr-body-color: #e8e8e8;
        }

        [data-theme="dark"] .card {
            --tblr-card-bg: #2d3133;
            --tblr-card-border-color: #3d4246;
        }

        [data-theme="dark"] .table {
            --tblr-table-bg: #2d3133;
            --tblr-table-border-color: #3d4246;
            --tblr-table-striped-bg: #363a3c;
        }

        [data-theme="dark"] .form-control,
        [data-theme="dark"] .form-select {
            background-color: #363a3c;
            border-color: #3d4246;
            color: #e8e8e8;
        }

        [data-theme="dark"] .form-control:focus,
        [data-theme="dark"] .form-select:focus {
            background-color: #3d4246;
            border-color: #5c6266;
            color: #e8e8e8;
        }

        [data-theme="dark"] .navbar {
            background-color: #2d3133 !important;
            border-color: #3d4246;
        }

        [data-theme="dark"] .border {
            border-color: #3d4246 !important;
        }

        [data-theme="dark"] .text-muted {
            color: #a8aaad !important;
        }

        /* Theme toggle button */
        #theme-toggle {
            cursor: pointer;
        }

        #theme-toggle i {
            font-size: 1.25rem;
        }
    `;

    document.head.appendChild(style);
}

/**
 * Listen to theme changes
 *
 * @param {Function} callback - Callback function(effectiveTheme, preference)
 * @returns {Function} Cleanup function
 *
 * @example
 * const cleanup = onThemeChange((theme, preference) => {
 *   console.log(`Theme changed to ${theme} (preference: ${preference})`);
 * });
 *
 * // Later, to stop listening:
 * cleanup();
 */
export function onThemeChange(callback) {
    const handler = (event) => {
        callback(event.detail.theme, event.detail.preference);
    };

    window.addEventListener('themechange', handler);

    // Return cleanup function
    return () => {
        window.removeEventListener('themechange', handler);
    };
}

// Export constants
export { THEMES };

// Export for global use
if (typeof window !== 'undefined') {
    window.themeManager = {
        setTheme,
        getTheme,
        getEffectiveTheme: getEffectiveThemeValue,
        toggleTheme,
        toggleThemeSimple,
        initializeTheme,
        createThemeToggle,
        onThemeChange,
        THEMES,
    };
}
