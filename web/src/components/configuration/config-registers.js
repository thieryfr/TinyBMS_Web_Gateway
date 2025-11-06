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

        await this.loadRegisters();
        this.render();
        this.attachEventListeners();
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
            this.registers = data.registers || [];

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

        let fieldHtml = '';

        if (register.is_enum) {
            // Enum field (select dropdown)
            fieldHtml = `
                <select id="${fieldId}"
                        class="form-control ${isDirty ? 'is-dirty' : ''}"
                        data-address="${register.address}"
                        ${isReadOnly ? 'disabled' : ''}>
                    ${register.enum_options.map(opt => `
                        <option value="${opt.value}" ${opt.value == register.current_user_value ? 'selected' : ''}>
                            ${this.escapeHtml(opt.label)}
                        </option>
                    `).join('')}
                </select>
            `;
        } else {
            // Numeric field
            const inputType = 'number';
            const step = register.step || 0.01;
            const precision = register.precision || 2;

            fieldHtml = `
                <input type="${inputType}"
                       id="${fieldId}"
                       class="form-control ${isDirty ? 'is-dirty' : ''}"
                       data-address="${register.address}"
                       value="${register.current_user_value.toFixed(precision)}"
                       ${register.has_min ? `min="${register.min_value}"` : ''}
                       ${register.has_max ? `max="${register.max_value}"` : ''}
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
        const originalValue = this.originalValues.get(address);
        const currentValue = parseFloat(input.value);

        if (Math.abs(currentValue - originalValue) > 0.0001) {
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
                    if (register.is_enum) {
                        input.value = originalValue;
                    } else {
                        input.value = originalValue.toFixed(register.precision || 2);
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

        // Collect changes
        const changes = {};
        this.dirtyRegisters.forEach(address => {
            const input = document.querySelector(`[data-address="${address}"]`);
            if (input) {
                const register = this.registers.find(r => r.address === address);
                if (register) {
                    changes[register.key || `0x${address.toString(16)}`] = parseFloat(input.value);
                }
            }
        });

        try {
            this.showStatus('Enregistrement en cours...', 'info');

            const response = await fetch('/api/registers', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(changes),
            });

            if (!response.ok) {
                throw new Error(`Erreur ${response.status}: ${response.statusText}`);
            }

            // Update original values
            this.dirtyRegisters.forEach(address => {
                const input = document.querySelector(`[data-address="${address}"]`);
                if (input) {
                    const newValue = parseFloat(input.value);
                    this.originalValues.set(address, newValue);
                    input.classList.remove('is-dirty');
                }
            });

            this.dirtyRegisters.clear();
            this.updateToolbarState();
            this.showStatus(`Configuration enregistrée avec succès (${Object.keys(changes).length} registres modifiés)`, 'success');

        } catch (error) {
            console.error('Error saving registers:', error);
            this.showStatus(`Erreur lors de l'enregistrement: ${error.message}`, 'danger');
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
