/**
 * @file config-registers.js
 * @brief TinyBMS Configuration Registers UI Module
 * @description Handles loading, displaying, and updating TinyBMS UART configuration registers
 */

export class ConfigRegistersManager {
    constructor() {
        this.registers = [];
        this.originalValues = new Map();
        this.dirtyRegisters = new Set();
        this.containerElement = null;
        this.formTabsInitialized = false;
        this.registerKeyFallback = {
            300: 'fully_charged_voltage_mv',
            301: 'fully_discharged_voltage_mv',
            303: 'early_balancing_threshold_mv',
            304: 'charge_finished_current_ma',
            305: 'peak_discharge_current_a',
            306: 'battery_capacity_ah',
            307: 'cell_count',
            308: 'allowed_disbalance_mv',
            310: 'charger_startup_delay_s',
            311: 'charger_disable_delay_s',
            315: 'overvoltage_cutoff_mv',
            316: 'undervoltage_cutoff_mv',
            317: 'discharge_overcurrent_a',
            318: 'charge_overcurrent_a',
            319: 'overheat_cutoff_c',
            320: 'low_temp_charge_cutoff_c',
            321: 'charge_restart_level_percent',
            322: 'battery_max_cycles',
            323: 'state_of_health_permille',
            328: 'state_of_charge_permille',
            329: 'invert_ext_current_sensor',
            330: 'charger_type',
            331: 'load_switch_type',
            332: 'automatic_recovery_count',
            333: 'charger_switch_type',
            334: 'ignition_source',
            335: 'charger_detection_source',
            337: 'precharge_pin',
            338: 'precharge_duration',
            339: 'temperature_sensor_type',
            340: 'operation_mode',
            341: 'single_port_switch_type',
            342: 'broadcast_interval',
            343: 'communication_protocol',
        };
    }

    /**
     * Initialize the configuration registers manager
     * @param {string} containerId - ID of the container element
     */
    async init(containerId) {
        this.containerElement = document.getElementById(containerId);
        if (!this.containerElement) {
            console.error(`Container element '${containerId}' not found`);
            return;
        }

        this.setupFormTabs();
        await this.loadRegisters();
        this.render();
        this.attachEventListeners();
    }

    /**
     * Ensure the configuration form tabs are interactive
     */
    setupFormTabs() {
        if (this.formTabsInitialized) return;

        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', () => this.setupFormTabs(), { once: true });
            return;
        }

        const tabList = document.getElementById('configuration-tablist');
        if (!tabList) return;

        const tabButtons = Array.from(tabList.querySelectorAll('[role="tab"]'));
        if (!tabButtons.length) return;

        const getPanel = (button) => {
            const selector = button.getAttribute('data-bs-target') || button.getAttribute('data-target');
            if (!selector) return null;
            try {
                return document.querySelector(selector);
            } catch (error) {
                console.warn('Configuration tabs: unable to resolve selector', selector, error);
                return null;
            }
        };

        const activate = (button) => {
            const panel = getPanel(button);
            if (!panel) return;

            tabButtons.forEach((tab) => {
                tab.classList.remove('active');
                tab.setAttribute('aria-selected', 'false');
                const tabPanel = getPanel(tab);
                if (tabPanel) {
                    tabPanel.classList.remove('active', 'show');
                }
            });

            button.classList.add('active');
            button.setAttribute('aria-selected', 'true');
            panel.classList.add('active', 'show');
        };

        tabButtons.forEach((button) => {
            button.addEventListener('click', (event) => {
                event.preventDefault();
                activate(button);
            });

            button.addEventListener('keydown', (event) => {
                if (event.key !== 'Enter' && event.key !== ' ') return;
                event.preventDefault();
                activate(button);
            });
        });

        const initiallyActive = tabButtons.find((tab) => tab.classList.contains('active')) ?? tabButtons[0];
        if (initiallyActive) {
            activate(initiallyActive);
        }

        this.formTabsInitialized = true;
    }

    /**
     * Load registers from the backend API
     */
    async loadRegisters() {
        try {
            const response = await fetch('/api/registers');
            if (!response.ok) {
                throw new Error(`Failed to load registers: ${response.statusText}`);
            }

            const data = await response.json();
            const registers = data.registers || [];

            this.registers = registers.map(reg => {
                const key = reg.key || this.registerKeyFallback[reg.address];
                const currentValue =
                    typeof reg.current_user_value !== 'undefined'
                        ? reg.current_user_value
                        : reg.value;
                return {
                    ...reg,
                    key,
                    current_user_value: currentValue,
                };
            });

            // Store original values
            this.originalValues.clear();
            this.dirtyRegisters.clear();
            this.registers.forEach(reg => {
                this.originalValues.set(reg.address, reg.current_user_value);
            });

            console.log(`Loaded ${this.registers.length} TinyBMS configuration registers`);
        } catch (error) {
            console.error('Error loading registers:', error);
            this.showError('Impossible de charger les registres TinyBMS');
        }
    }

    getRegisterValue(register) {
        if (!register) {
            return undefined;
        }

        if (register.current_user_value !== undefined && register.current_user_value !== null) {
            return register.current_user_value;
        }

        if (register.value !== undefined && register.value !== null) {
            return register.value;
        }

        return undefined;
    }

    normalizeValue(register, rawValue) {
        if (rawValue === null || typeof rawValue === 'undefined') {
            return null;
        }

        if (typeof rawValue === 'boolean') {
            return rawValue ? 1 : 0;
        }

        if (typeof rawValue === 'string') {
            const trimmed = rawValue.trim();
            if (trimmed === '') {
                return null;
            }
            const numeric = Number(trimmed);
            return Number.isNaN(numeric) ? trimmed : numeric;
        }

        if (typeof rawValue === 'number') {
            if (register && register.is_enum) {
                return Number.isNaN(rawValue) ? rawValue : Math.trunc(rawValue);
            }

            if (register && typeof register.precision === 'number' && Number.isFinite(register.precision)) {
                const factor = Math.pow(10, register.precision);
                if (factor > 0 && Number.isFinite(factor)) {
                    return Math.round(rawValue * factor) / factor;
                }
            }

            return rawValue;
        }

        return rawValue;
    }

    parseInputValue(input, register) {
        if (!input) {
            return null;
        }

        if (input.type === 'checkbox') {
            return input.checked ? 1 : 0;
        }

        if (input.tagName === 'SELECT') {
            const numeric = Number(input.value);
            if (!Number.isNaN(numeric)) {
                return register && register.is_enum ? Math.trunc(numeric) : numeric;
            }
            return input.value;
        }

        const rawValue = input.value != null ? input.value.toString().trim() : '';
        if (rawValue === '') {
            return null;
        }

        const numeric = Number(rawValue);
        if (!Number.isNaN(numeric)) {
            return this.normalizeValue(register, numeric);
        }

        return rawValue;
    }

    /**
     * Group registers by their group property
     */
    groupRegisters() {
        const grouped = new Map();

        this.registers.forEach(reg => {
            const group = reg.group || 'Autres';
            if (!grouped.has(group)) {
                grouped.set(group, []);
            }
            grouped.get(group).push(reg);
        });

        // Sort registers within each group by address
        grouped.forEach((regs, group) => {
            regs.sort((a, b) => a.address - b.address);
        });

        return grouped;
    }

    /**
     * Render the configuration interface
     */
    render() {
        if (!this.containerElement) return;

        const grouped = this.groupRegisters();

        if (grouped.size === 0) {
            this.containerElement.innerHTML = `
                <div class="alert alert-info">
                    <i class="ti ti-info-circle me-2"></i>
                    Aucun registre de configuration disponible.
                </div>
            `;
            return;
        }

        let html = `
            <div class="config-registers-toolbar mb-3 d-flex gap-2 justify-content-end">
                <button id="config-registers-refresh" class="btn btn-outline-secondary btn-sm" title="Recharger les valeurs">
                    <i class="ti ti-refresh"></i> Actualiser
                </button>
                <button id="config-registers-reset" class="btn btn-outline-warning btn-sm" title="Annuler les modifications" disabled>
                    <i class="ti ti-reload"></i> Réinitialiser
                </button>
                <button id="config-registers-save" class="btn btn-primary btn-sm" title="Enregistrer les modifications" disabled>
                    <i class="ti ti-device-floppy"></i> Enregistrer
                </button>
            </div>
            <div class="config-registers-status mb-3" id="config-registers-status"></div>
            <div class="accordion" id="config-registers-accordion">
        `;

        let groupIndex = 0;
        grouped.forEach((registers, groupName) => {
            const groupId = `group-${groupIndex}`;
            const isFirst = groupIndex === 0;

            html += `
                <div class="accordion-item">
                    <h2 class="accordion-header" id="heading-${groupId}">
                        <button class="accordion-button ${isFirst ? '' : 'collapsed'}"
                                type="button"
                                data-bs-toggle="collapse"
                                data-bs-target="#collapse-${groupId}"
                                aria-expanded="${isFirst}"
                                aria-controls="collapse-${groupId}">
                            <i class="ti ti-settings me-2"></i>
                            ${this.escapeHtml(groupName)}
                            <span class="badge bg-secondary-lt ms-2">${registers.length}</span>
                        </button>
                    </h2>
                    <div id="collapse-${groupId}"
                         class="accordion-collapse collapse ${isFirst ? 'show' : ''}"
                         aria-labelledby="heading-${groupId}">
                        <div class="accordion-body">
                            <div class="row g-3">
                                ${registers.map(reg => this.renderRegisterField(reg)).join('')}
                            </div>
                        </div>
                    </div>
                </div>
            `;
            groupIndex++;
        });

        html += `
            </div>
        `;

        this.containerElement.innerHTML = html;
    }

    /**
     * Render a single register field
     */
    renderRegisterField(register) {
        const fieldId = `reg-${register.address}`;
        const isReadOnly = register.access === 'ro';
        const isDirty = this.dirtyRegisters.has(register.address);
        const currentValue = this.getRegisterValue(register);
        const enumOptions = register.enum_options || register.enum || [];
        const isEnum = register.is_enum || (Array.isArray(enumOptions) && enumOptions.length > 0);

        let fieldHtml = '';

        if (isEnum) {
            const optionsHtml = enumOptions.map(opt => {
                const optionValue = opt.value;
                const isSelected = optionValue == currentValue;
                return `
                        <option value="${optionValue}" ${isSelected ? 'selected' : ''}>
                            ${this.escapeHtml(opt.label || optionValue)}
                        </option>`;
            }).join('');

            fieldHtml = `
                <select id="${fieldId}"
                        class="form-control ${isDirty ? 'is-dirty' : ''}"
                        data-address="${register.address}"
                        ${isReadOnly ? 'disabled' : ''}>
                    ${optionsHtml}
                </select>
            `;
        } else {
            const inputType = 'number';
            const step = register.step || register.step_user || 0.01;
            const precision = register.precision || 2;
            const numericValue = typeof currentValue === 'number' ? currentValue : parseFloat(currentValue);
            const displayValue = Number.isFinite(numericValue)
                ? numericValue.toFixed(precision)
                : (currentValue !== undefined ? currentValue : '');
            const minCandidate = register.min_value !== undefined ? register.min_value : register.min;
            const maxCandidate = register.max_value !== undefined ? register.max_value : register.max;
            const minAttr = register.has_min && typeof minCandidate !== 'undefined' ? `min="${minCandidate}"` : '';
            const maxAttr = register.has_max && typeof maxCandidate !== 'undefined' ? `max="${maxCandidate}"` : '';

            fieldHtml = `
                <input type="${inputType}"
                       id="${fieldId}"
                       class="form-control ${isDirty ? 'is-dirty' : ''}"
                       data-address="${register.address}"
                       value="${displayValue}"
                       ${minAttr}
                       ${maxAttr}
                       step="${step}"
                       ${isReadOnly ? 'readonly' : ''}>
            `;
        }

        const hint = [];
        if (register.unit) hint.push(`Unité: ${register.unit}`);
        if (register.has_min && register.has_max && !register.is_enum) {
            hint.push(`Plage: ${register.min_value} - ${register.max_value}`);
        }
        if (register.default_user_value !== undefined) {
            hint.push(`Défaut: ${register.default_user_value}`);
        }

        return `
            <div class="col-md-6 col-xl-4">
                <div class="mb-2">
                    <label for="${fieldId}" class="form-label d-flex align-items-center gap-2">
                        <span>${this.escapeHtml(register.key || `Reg 0x${register.address.toString(16)}`)}</span>
                        ${isReadOnly ? '<span class="badge bg-secondary-lt" title="Lecture seule">RO</span>' : ''}
                        ${isDirty ? '<i class="ti ti-pencil text-warning" title="Modifié"></i>' : ''}
                    </label>
                    ${fieldHtml}
                    ${register.comment ? `<div class="form-hint text-secondary small mt-1">${this.escapeHtml(register.comment)}</div>` : ''}
                    ${hint.length > 0 ? `<div class="form-hint text-muted small">${hint.join(' • ')}</div>` : ''}
                </div>
            </div>
        `;
    }

    /**
     * Attach event listeners
     */
    attachEventListeners() {
        // Refresh button
        const refreshBtn = document.getElementById('config-registers-refresh');
        if (refreshBtn) {
            refreshBtn.addEventListener('click', () => this.handleRefresh());
        }

        // Reset button
        const resetBtn = document.getElementById('config-registers-reset');
        if (resetBtn) {
            resetBtn.addEventListener('click', () => this.handleReset());
        }

        // Save button
        const saveBtn = document.getElementById('config-registers-save');
        if (saveBtn) {
            saveBtn.addEventListener('click', () => this.handleSave());
        }

        // Input change listeners
        this.containerElement.querySelectorAll('input[data-address], select[data-address]').forEach(input => {
            input.addEventListener('change', (e) => this.handleInputChange(e));
        });
    }

    /**
     * Handle input value change
     */
    handleInputChange(event) {
        const input = event.target;
        const address = parseInt(input.dataset.address);
        const register = this.registers.find(r => r.address === address);
        const originalValue = this.originalValues.get(address);
        const currentValue = this.parseInputValue(input, register);

        let isDirty = false;
        if (originalValue === undefined || originalValue === null) {
            isDirty = currentValue !== null && currentValue !== undefined;
        } else if (typeof originalValue === 'number' && typeof currentValue === 'number') {
            const tolerance = register && typeof register.precision === 'number'
                ? Math.pow(10, -(register.precision + 1))
                : 0.0001;
            isDirty = Math.abs(currentValue - originalValue) > tolerance;
        } else {
            isDirty = currentValue !== originalValue;
        }

        if (isDirty) {
            this.dirtyRegisters.add(address);
            input.classList.add('is-dirty');
        } else {
            this.dirtyRegisters.delete(address);
            input.classList.remove('is-dirty');
        }

        this.updateToolbarState();
    }

    /**
     * Update toolbar buttons state
     */
    updateToolbarState() {
        const hasDirty = this.dirtyRegisters.size > 0;

        const resetBtn = document.getElementById('config-registers-reset');
        const saveBtn = document.getElementById('config-registers-save');

        if (resetBtn) resetBtn.disabled = !hasDirty;
        if (saveBtn) saveBtn.disabled = !hasDirty;
    }

    /**
     * Handle refresh action
     */
    async handleRefresh() {
        if (this.dirtyRegisters.size > 0) {
            if (!confirm('Des modifications non enregistrées seront perdues. Continuer ?')) {
                return;
            }
        }

        this.showStatus('Chargement des registres...', 'info');
        await this.loadRegisters();
        this.render();
        this.attachEventListeners();
        this.showStatus('Registres rechargés avec succès', 'success');
    }

    /**
     * Handle reset action
     */
    handleReset() {
        this.dirtyRegisters.forEach(address => {
            const input = document.querySelector(`[data-address="${address}"]`);
            if (input) {
                const originalValue = this.originalValues.get(address);
                const register = this.registers.find(r => r.address === address);

                if (register) {
                    const enumOptions = register.enum_options || register.enum || [];
                    const isEnum = register.is_enum || (Array.isArray(enumOptions) && enumOptions.length > 0);
                    if (isEnum) {
                        input.value = originalValue;
                    } else {
                        const numericValue = typeof originalValue === 'number' ? originalValue : parseFloat(originalValue);
                        if (Number.isFinite(numericValue)) {
                            input.value = numericValue.toFixed(register.precision || 2);
                        } else {
                            input.value = originalValue !== undefined ? originalValue : '';
                        }
                    }
                }

                input.classList.remove('is-dirty');
            }
        });

        this.dirtyRegisters.clear();
        this.updateToolbarState();
        this.showStatus('Modifications annulées', 'info');
    }

    /**
     * Handle save action
     */
    async handleSave() {
        if (this.dirtyRegisters.size === 0) {
            return;
        }

        const updates = [];
        this.dirtyRegisters.forEach(address => {
            const input = document.querySelector(`[data-address="${address}"]`);
            if (!input) {
                return;
            }

            const register = this.registers.find(r => r.address === address);
            if (!register) {
                return;
            }

            const key = register.key || this.registerKeyFallback[address] || `0x${address.toString(16)}`;
            const value = this.parseInputValue(input, register);
            if (value === null || typeof value === 'undefined') {
                return;
            }
            if (typeof value === 'number' && Number.isNaN(value)) {
                return;
            }

            updates.push({ address, key, value, input, register });
        });

        if (updates.length === 0) {
            this.showStatus('Aucune modification valide à enregistrer', 'warning');
            return;
        }

        try {
            this.showStatus('Enregistrement en cours...', 'info');

            await this.sendRegisterUpdates(updates);

            updates.forEach(({ address, value, input, register }) => {
                const normalized = this.normalizeValue(register, value);
                this.originalValues.set(address, normalized);
                this.updateRegisterValue(register, normalized);

                const enumOptions = register.enum_options || register.enum || [];
                const isEnum = register.is_enum || (Array.isArray(enumOptions) && enumOptions.length > 0);

                if (isEnum) {
                    input.value = normalized;
                } else if (typeof normalized === 'number') {
                    input.value = normalized.toFixed(register.precision || 2);
                } else if (normalized !== null && normalized !== undefined) {
                    input.value = normalized;
                }

                input.classList.remove('is-dirty');
            });

            this.dirtyRegisters.clear();
            this.updateToolbarState();
            this.showStatus(`Configuration enregistrée avec succès (${updates.length} registre${updates.length > 1 ? 's' : ''})`, 'success');

        } catch (error) {
            console.error('Error saving registers:', error);
            this.showStatus(`Erreur lors de l'enregistrement: ${error.message}`, 'danger');
        }
    }

    async sendRegisterUpdates(updates) {
        for (const update of updates) {
            const response = await fetch('/api/registers', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    key: update.key,
                    value: update.value,
                }),
            });

            if (!response.ok) {
                const errorText = await response.text();
                throw new Error(`Registre ${update.key}: ${errorText || response.statusText}`);
            }

            try {
                await response.json();
            } catch (jsonError) {
                // Some firmware versions return an empty body; ignore parsing errors
            }
        }
    }

    updateRegisterValue(register, value) {
        if (!register) {
            return;
        }

        register.current_user_value = value;
        if (register.hasOwnProperty('value')) {
            register.value = value;
        }
    }

    /**
     * Show status message
     */
    showStatus(message, type = 'info') {
        const statusEl = document.getElementById('config-registers-status');
        if (!statusEl) return;

        const iconMap = {
            info: 'ti-info-circle',
            success: 'ti-check',
            warning: 'ti-alert-triangle',
            danger: 'ti-alert-circle',
        };

        statusEl.innerHTML = `
            <div class="alert alert-${type} alert-dismissible fade show" role="alert">
                <i class="ti ${iconMap[type] || 'ti-info-circle'} me-2"></i>
                ${this.escapeHtml(message)}
                <button type="button" class="btn-close" data-bs-dismiss="alert" aria-label="Close"></button>
            </div>
        `;

        // Auto-dismiss after 5 seconds for success messages
        if (type === 'success') {
            setTimeout(() => {
                const alert = statusEl.querySelector('.alert');
                if (alert) {
                    alert.classList.remove('show');
                    setTimeout(() => { statusEl.innerHTML = ''; }, 150);
                }
            }, 5000);
        }
    }

    /**
     * Show error message
     */
    showError(message) {
        this.showStatus(message, 'danger');
    }

    /**
     * Escape HTML to prevent XSS
     */
    escapeHtml(text) {
        if (!text) return '';
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }
}
