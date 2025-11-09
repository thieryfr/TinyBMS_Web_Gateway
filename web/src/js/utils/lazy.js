/**
 * @file lazy.js
 * @brief Lazy loading utilities for modules and components
 * @description Provides dynamic import and lazy loading functionality
 *              to improve initial page load performance
 */

/**
 * Module cache to avoid reloading
 */
const moduleCache = new Map();

/**
 * Loading states tracker
 */
const loadingModules = new Map();

/**
 * Lazy load a JavaScript module
 *
 * @param {string} modulePath - Path to the module
 * @param {Object} options - Options
 * @param {boolean} options.cache - Cache the loaded module (default: true)
 * @param {Function} options.onProgress - Progress callback
 * @param {number} options.timeout - Load timeout in ms (default: 30000)
 *
 * @returns {Promise<any>} Loaded module
 *
 * @example
 * const echarts = await lazyLoadModule('/src/js/lib/echarts.min.js');
 * const chart = echarts.init(element);
 *
 * @example
 * // With options
 * const module = await lazyLoadModule('/src/js/charts.js', {
 *   cache: true,
 *   onProgress: (progress) => console.log(`Loading: ${progress}%`),
 *   timeout: 10000
 * });
 */
export async function lazyLoadModule(modulePath, options = {}) {
    const {
        cache = true,
        onProgress = null,
        timeout = 30000,
    } = options;

    // Check cache first
    if (cache && moduleCache.has(modulePath)) {
        console.log(`[Lazy] Loading ${modulePath} from cache`);
        return moduleCache.get(modulePath);
    }

    // Check if already loading
    if (loadingModules.has(modulePath)) {
        console.log(`[Lazy] Waiting for ${modulePath} to finish loading`);
        return loadingModules.get(modulePath);
    }

    // Create loading promise
    const loadingPromise = new Promise(async (resolve, reject) => {
        const timeoutId = setTimeout(() => {
            reject(new Error(`Module load timeout: ${modulePath}`));
        }, timeout);

        try {
            console.log(`[Lazy] Loading module: ${modulePath}`);

            // Use dynamic import if available (ES6 modules)
            if (typeof import === 'function') {
                const module = await import(modulePath);

                clearTimeout(timeoutId);

                // Cache if requested
                if (cache) {
                    moduleCache.set(modulePath, module);
                }

                loadingModules.delete(modulePath);
                resolve(module);
            }
            // Fallback to script tag loading
            else {
                const script = document.createElement('script');
                script.src = modulePath;
                script.async = true;

                script.onload = () => {
                    clearTimeout(timeoutId);
                    const module = window[getModuleName(modulePath)];

                    if (cache) {
                        moduleCache.set(modulePath, module);
                    }

                    loadingModules.delete(modulePath);
                    resolve(module);
                };

                script.onerror = () => {
                    clearTimeout(timeoutId);
                    loadingModules.delete(modulePath);
                    reject(new Error(`Failed to load module: ${modulePath}`));
                };

                document.head.appendChild(script);
            }
        } catch (error) {
            clearTimeout(timeoutId);
            loadingModules.delete(modulePath);
            reject(error);
        }
    });

    // Track loading
    loadingModules.set(modulePath, loadingPromise);

    return loadingPromise;
}

/**
 * Get module name from path (for fallback loading)
 */
function getModuleName(path) {
    const fileName = path.split('/').pop().replace('.js', '').replace('.min', '');
    return fileName;
}

/**
 * Lazy load multiple modules in parallel
 *
 * @param {Array<string>} modulePaths - Array of module paths
 * @param {Object} options - Options (passed to lazyLoadModule)
 *
 * @returns {Promise<Array>} Array of loaded modules
 *
 * @example
 * const [echarts, moment] = await lazyLoadModules([
 *   '/src/js/lib/echarts.min.js',
 *   '/src/js/lib/moment.min.js'
 * ]);
 */
export async function lazyLoadModules(modulePaths, options = {}) {
    const promises = modulePaths.map(path => lazyLoadModule(path, options));
    return Promise.all(promises);
}

/**
 * Lazy load a CSS file
 *
 * @param {string} cssPath - Path to CSS file
 * @param {Object} options - Options
 * @param {boolean} options.cache - Prevent re-loading (default: true)
 * @param {string} options.media - Media query (default: 'all')
 *
 * @returns {Promise<HTMLLinkElement>} Link element
 *
 * @example
 * await lazyLoadCSS('/src/css/dark-theme.css');
 */
export async function lazyLoadCSS(cssPath, options = {}) {
    const {
        cache = true,
        media = 'all',
    } = options;

    // Check if already loaded
    if (cache && document.querySelector(`link[href="${cssPath}"]`)) {
        console.log(`[Lazy] CSS already loaded: ${cssPath}`);
        return document.querySelector(`link[href="${cssPath}"]`);
    }

    return new Promise((resolve, reject) => {
        const link = document.createElement('link');
        link.rel = 'stylesheet';
        link.href = cssPath;
        link.media = media;

        link.onload = () => {
            console.log(`[Lazy] CSS loaded: ${cssPath}`);
            resolve(link);
        };

        link.onerror = () => {
            reject(new Error(`Failed to load CSS: ${cssPath}`));
        };

        document.head.appendChild(link);
    });
}

/**
 * Lazy load an image
 *
 * @param {string} imageSrc - Image source URL
 * @param {Object} options - Options
 * @param {number} options.timeout - Load timeout in ms
 *
 * @returns {Promise<HTMLImageElement>} Image element
 *
 * @example
 * const img = await lazyLoadImage('/images/large-photo.jpg');
 * container.appendChild(img);
 */
export async function lazyLoadImage(imageSrc, options = {}) {
    const {
        timeout = 30000,
    } = options;

    return new Promise((resolve, reject) => {
        const timeoutId = setTimeout(() => {
            reject(new Error(`Image load timeout: ${imageSrc}`));
        }, timeout);

        const img = new Image();

        img.onload = () => {
            clearTimeout(timeoutId);
            console.log(`[Lazy] Image loaded: ${imageSrc}`);
            resolve(img);
        };

        img.onerror = () => {
            clearTimeout(timeoutId);
            reject(new Error(`Failed to load image: ${imageSrc}`));
        };

        img.src = imageSrc;
    });
}

/**
 * Lazy load component when element becomes visible (Intersection Observer)
 *
 * @param {HTMLElement|string} element - Element or selector
 * @param {Function} loader - Async function to load component
 * @param {Object} options - Intersection Observer options
 * @param {string} options.rootMargin - Root margin (default: '50px')
 * @param {number} options.threshold - Intersection threshold (default: 0.01)
 * @param {boolean} options.once - Load only once (default: true)
 *
 * @returns {IntersectionObserver} Observer instance
 *
 * @example
 * lazyLoadOnVisible('#chart-container', async () => {
 *   const echarts = await lazyLoadModule('/src/js/lib/echarts.min.js');
 *   const chart = echarts.init(document.getElementById('chart-container'));
 *   chart.setOption(options);
 * });
 */
export function lazyLoadOnVisible(element, loader, options = {}) {
    const {
        rootMargin = '50px',
        threshold = 0.01,
        once = true,
    } = options;

    const target = typeof element === 'string' ? document.querySelector(element) : element;

    if (!target) {
        console.warn(`[Lazy] Element not found for lazy loading`);
        return null;
    }

    // Check if Intersection Observer is supported
    if (!('IntersectionObserver' in window)) {
        console.warn('[Lazy] IntersectionObserver not supported, loading immediately');
        loader();
        return null;
    }

    const observer = new IntersectionObserver(
        (entries) => {
            entries.forEach((entry) => {
                if (entry.isIntersecting) {
                    console.log(`[Lazy] Element visible, loading component`);
                    loader();

                    if (once) {
                        observer.unobserve(target);
                    }
                }
            });
        },
        {
            rootMargin,
            threshold,
        }
    );

    observer.observe(target);
    return observer;
}

/**
 * Preload a module in the background (low priority)
 *
 * @param {string} modulePath - Path to module
 *
 * @example
 * // Preload module that might be needed soon
 * preloadModule('/src/js/advanced-charts.js');
 */
export function preloadModule(modulePath) {
    if (moduleCache.has(modulePath)) {
        return; // Already loaded
    }

    // Use <link rel="prefetch"> for low-priority preloading
    const link = document.createElement('link');
    link.rel = 'prefetch';
    link.as = 'script';
    link.href = modulePath;
    document.head.appendChild(link);

    console.log(`[Lazy] Prefetching module: ${modulePath}`);
}

/**
 * Clear module cache
 *
 * @param {string|null} modulePath - Specific module to clear, or null for all
 */
export function clearModuleCache(modulePath = null) {
    if (modulePath) {
        moduleCache.delete(modulePath);
        console.log(`[Lazy] Cleared cache for: ${modulePath}`);
    } else {
        moduleCache.clear();
        console.log('[Lazy] Cleared all module cache');
    }
}

/**
 * Get cache statistics
 */
export function getCacheStats() {
    return {
        cached: moduleCache.size,
        loading: loadingModules.size,
        modules: Array.from(moduleCache.keys()),
    };
}

/**
 * Create lazy-loadable component wrapper
 *
 * @param {string} modulePath - Path to component module
 * @param {string} placeholder - Placeholder HTML while loading
 *
 * @returns {Function} Component loader function
 *
 * @example
 * const LazyChart = createLazyComponent(
 *   '/src/components/chart.js',
 *   '<div class="skeleton skeleton-card"></div>'
 * );
 *
 * // Later, when needed:
 * await LazyChart(document.getElementById('chart-container'));
 */
export function createLazyComponent(modulePath, placeholder = '') {
    return async function(container) {
        const element = typeof container === 'string'
            ? document.querySelector(container)
            : container;

        if (!element) {
            throw new Error('Container element not found');
        }

        // Show placeholder
        if (placeholder) {
            element.innerHTML = placeholder;
        }

        // Load module
        const module = await lazyLoadModule(modulePath);

        // Initialize component (assumes module has a default export or init function)
        if (module.default) {
            return module.default(element);
        } else if (module.init) {
            return module.init(element);
        } else {
            throw new Error(`Module ${modulePath} doesn't export default or init function`);
        }
    };
}

// Export for global use
if (typeof window !== 'undefined') {
    window.lazyLoader = {
        lazyLoadModule,
        lazyLoadModules,
        lazyLoadCSS,
        lazyLoadImage,
        lazyLoadOnVisible,
        preloadModule,
        clearModuleCache,
        getCacheStats,
        createLazyComponent,
    };
}
