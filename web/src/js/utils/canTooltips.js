/**
 * CAN Protocol Tooltip System
 * Displays Victron CAN PGN descriptions on hover
 *
 * @file canTooltips.js
 * @description Provides tooltip information for Victron CAN protocol fields
 *              displayed in the web interface. Each tooltip shows the PGN name,
 *              description, and transmission interval.
 */

/**
 * Victron CAN Protocol Descriptions
 * Mapping of CAN IDs to their protocol information
 */
const CAN_DESCRIPTIONS = {
    '0x307': {
        name: 'Inverter Identifier',
        desc: 'Handshake avec Victron GX',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x351': {
        name: 'Charge Parameters',
        desc: 'CVL, CCL, DCL (limites charge/décharge)',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x355': {
        name: 'SOC & SOH',
        desc: 'État de charge et santé de la batterie',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x356': {
        name: 'Voltage / Current / Temperature',
        desc: 'Tension, courant, température du pack',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x35A': {
        name: 'Alarms & Warnings',
        desc: 'Alarmes et avertissements BMS',
        interval: '1s',
        pgn: '59904'
    },
    '0x35E': {
        name: 'Manufacturer Name',
        desc: 'Nom du fabricant (TinyBMS)',
        interval: 'Init',
        pgn: 'N/A'
    },
    '0x35F': {
        name: 'Battery Model & Capacity',
        desc: 'Modèle batterie, version firmware, capacité',
        interval: 'Init',
        pgn: 'N/A'
    },
    '0x370': {
        name: 'Battery/BMS Name Part 1',
        desc: 'Nom de la batterie/BMS (partie 1)',
        interval: 'Init',
        pgn: 'N/A'
    },
    '0x371': {
        name: 'Battery/BMS Name Part 2',
        desc: 'Nom de la batterie/BMS (partie 2)',
        interval: 'Init',
        pgn: 'N/A'
    },
    '0x372': {
        name: 'Module Status Counts',
        desc: 'Nombre de modules OK/Blocking/Offline',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x373': {
        name: 'Cell Voltage & Temperature Extremes',
        desc: 'Min/Max tension et température des cellules',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x374': {
        name: 'Min Cell Voltage ID',
        desc: 'Identifiant de la cellule à tension minimale',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x375': {
        name: 'Max Cell Voltage ID',
        desc: 'Identifiant de la cellule à tension maximale',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x376': {
        name: 'Min Cell Temperature ID',
        desc: 'Identifiant de la cellule à température minimale',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x377': {
        name: 'Max Cell Temperature ID',
        desc: 'Identifiant de la cellule à température maximale',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x378': {
        name: 'Cumulative Energy',
        desc: 'Énergie cumulée IN/OUT',
        interval: '1s',
        pgn: 'N/A'
    },
    '0x379': {
        name: 'Installed Capacity',
        desc: 'Capacité installée de la batterie',
        interval: 'Init',
        pgn: 'N/A'
    },
    '0x380': {
        name: 'Serial Number Part 1',
        desc: 'Numéro de série (partie 1)',
        interval: 'Init',
        pgn: 'N/A'
    },
    '0x381': {
        name: 'Serial Number Part 2',
        desc: 'Numéro de série (partie 2)',
        interval: 'Init',
        pgn: 'N/A'
    },
    '0x382': {
        name: 'Battery Family Name',
        desc: 'Nom de la famille de batterie',
        interval: 'Init',
        pgn: 'N/A'
    },
};

/**
 * Initialize CAN tooltips for all elements with data-tooltip attribute
 * Searches the DOM for elements with data-tooltip attributes containing CAN IDs
 * and adds tooltip information and visual indicators.
 *
 * @returns {number} Number of tooltips initialized
 *
 * @example
 * // Call after DOM is loaded
 * document.addEventListener('DOMContentLoaded', () => {
 *   initCanTooltips();
 * });
 */
export function initCanTooltips() {
    const elements = document.querySelectorAll('[data-tooltip]');
    let count = 0;

    elements.forEach(element => {
        const canId = element.getAttribute('data-tooltip');
        const info = CAN_DESCRIPTIONS[canId];

        if (info) {
            // Set native HTML title attribute for simple tooltip
            const tooltipText = `${info.name} (${canId})\n${info.desc}\nInterval: ${info.interval}`;
            element.setAttribute('title', tooltipText);

            // Add visual indicator
            element.style.cursor = 'help';
            element.classList.add('has-can-tooltip');

            // Add a small icon indicator (optional)
            if (!element.querySelector('.can-tooltip-icon')) {
                const icon = document.createElement('i');
                icon.className = 'ti ti-info-circle can-tooltip-icon ms-1 text-muted';
                icon.style.fontSize = '0.875rem';
                icon.setAttribute('title', tooltipText);
                element.appendChild(icon);
            }

            count++;
        }
    });

    console.log(`[CAN Tooltips] Initialized ${count} tooltips`);
    return count;
}

/**
 * Get CAN description by ID
 * Retrieves the protocol information for a specific CAN ID.
 *
 * @param {string} canId - CAN ID (e.g., "0x356")
 * @returns {Object|null} CAN description object or null if not found
 *
 * @example
 * const info = getCanDescription('0x356');
 * if (info) {
 *   console.log(`${info.name}: ${info.desc}`);
 * }
 */
export function getCanDescription(canId) {
    return CAN_DESCRIPTIONS[canId] || null;
}

/**
 * Get all CAN descriptions
 * Returns the complete mapping of all CAN protocol descriptions.
 *
 * @returns {Object} Object containing all CAN ID descriptions
 *
 * @example
 * const allDescriptions = getAllCanDescriptions();
 * Object.keys(allDescriptions).forEach(canId => {
 *   console.log(`${canId}: ${allDescriptions[canId].name}`);
 * });
 */
export function getAllCanDescriptions() {
    return { ...CAN_DESCRIPTIONS };
}
