/**
 * @file tinybms-config.js
 * @brief TinyBMS Battery Insider Configuration UI Module
 * @description Handles the complete TinyBMS configuration interface inspired by Battery Insider
 */

export class TinyBMSConfigManager {
    constructor() {
        this.config = {
            cellSettings: {},
            safetySettings: {},
            peripheralsSettings: {},
        };
        this.originalConfig = null;
    }

    /**
     * Initialize the TinyBMS configuration manager
     */
    async init() {
        console.log('Initializing TinyBMS Configuration Manager');

        // Wait for DOM to be ready
        if (document.readyState === 'loading') {
            await new Promise(resolve => {
                document.addEventListener('DOMContentLoaded', resolve);
            });
        }

        // Wait a bit for the partial to load
        await new Promise(resolve => setTimeout(resolve, 500));

        this.attachEventListeners();
        await this.loadConfiguration();
    }

    /**
     * Attach event listeners to form elements
     */
    attachEventListeners() {
        // Cell Settings Form
        const cellForm = document.getElementById('tinybms-cell-settings-form');
        if (cellForm) {
            cellForm.addEventListener('submit', (e) => this.handleCellSettingsSubmit(e));
        }

        const cellResetBtn = document.getElementById('cell-settings-reset');
        if (cellResetBtn) {
            cellResetBtn.addEventListener('click', () => this.resetForm('cell'));
        }

        // Safety Form
        const safetyForm = document.getElementById('tinybms-safety-form');
        if (safetyForm) {
            safetyForm.addEventListener('submit', (e) => this.handleSafetySubmit(e));
        }

        const safetyResetBtn = document.getElementById('safety-reset');
        if (safetyResetBtn) {
            safetyResetBtn.addEventListener('click', () => this.resetForm('safety'));
        }

        // Peripherals Form
        const peripheralsForm = document.getElementById('tinybms-peripherals-form');
        if (peripheralsForm) {
            peripheralsForm.addEventListener('submit', (e) => this.handlePeripheralsSubmit(e));
        }

        const peripheralsResetBtn = document.getElementById('peripherals-reset');
        if (peripheralsResetBtn) {
            peripheralsResetBtn.addEventListener('click', () => this.resetForm('peripherals'));
        }

        // BMS Mode change handler
        const bmsModeSelect = document.getElementById('bms-mode');
        if (bmsModeSelect) {
            bmsModeSelect.addEventListener('change', (e) => this.handleBMSModeChange(e));
        }

        // Maintenance buttons
        const loadConfigBtn = document.getElementById('load-config-from-file');
        if (loadConfigBtn) {
            loadConfigBtn.addEventListener('click', () => this.loadConfigFromFile());
        }

        const saveConfigBtn = document.getElementById('save-config-to-file');
        if (saveConfigBtn) {
            saveConfigBtn.addEventListener('click', () => this.saveConfigToFile());
        }

        const uploadConfigBtn = document.getElementById('upload-config-to-bms');
        if (uploadConfigBtn) {
            uploadConfigBtn.addEventListener('click', () => this.uploadConfigToBMS());
        }

        const updateFirmwareBtn = document.getElementById('update-bms-firmware');
        if (updateFirmwareBtn) {
            updateFirmwareBtn.addEventListener('click', () => this.updateFirmware());
        }

        const restartBtn = document.getElementById('restart-bms');
        if (restartBtn) {
            restartBtn.addEventListener('click', () => this.restartBMS());
        }

        console.log('Event listeners attached');
    }

    /**
     * Load configuration from BMS
     */
    async loadConfiguration() {
        try {
            const response = await fetch('/api/tinybms/config');
            if (!response.ok) {
                throw new Error(`Failed to load configuration: ${response.statusText}`);
            }

            const config = await response.json();
            this.originalConfig = JSON.parse(JSON.stringify(config)); // Deep copy
            this.populateFormFields(config);

            console.log('Configuration loaded successfully');
        } catch (error) {
            console.error('Error loading configuration:', error);
            this.showNotification('Erreur lors du chargement de la configuration', 'danger');

            // Load default values
            this.loadDefaultValues();
        }
    }

    /**
     * Load default values into forms
     */
    loadDefaultValues() {
        // Cell Settings defaults
        this.setFieldValue('fully-charged-voltage', 3.70);
        this.setFieldValue('charge-finished-current', 1.0);
        this.setFieldValue('fully-discharged-voltage', 3.00);
        this.setFieldValue('early-balancing-threshold', 3.20);
        this.setFieldValue('allowed-disbalance', 15);
        this.setFieldValue('number-of-series-cells', 13);
        this.setFieldValue('battery-capacity', 10.0);
        this.setFieldValue('set-soc-manually', 50);
        this.setFieldValue('battery-maximum-cycles', 1000);
        this.setFieldValue('set-soh-manually', 100);

        // Safety Settings defaults
        this.setFieldValue('over-voltage-cutoff', 4.20);
        this.setFieldValue('under-voltage-cutoff', 2.90);
        this.setFieldValue('discharge-over-current-cutoff', 60);
        this.setFieldValue('discharge-over-current-timeout', 0);
        this.setFieldValue('discharge-peak-current-cutoff', 100);
        this.setFieldValue('charge-over-current-cutoff', 20);
        this.setFieldValue('over-heat-cutoff', 60);
        this.setFieldValue('low-temp-charger-cutoff', 1);
        this.setFieldValue('automatic-recovery', 5);

        // Peripherals defaults
        this.setFieldValue('bms-mode', 'dual-port');
        this.setFieldValue('load-switch-type', 'discharge-fet');
        this.setFieldValue('ignition', 'disabled');
        this.setFieldValue('precharge', 'disabled');
        this.setFieldValue('precharge-duration', '1.0');
        this.setFieldValue('charger-type', 'generic-cc-cv');
        this.setFieldValue('charger-detection', 'internal');
        this.setFieldValue('charger-startup-delay', 20);
        this.setFieldValue('charger-disable-delay', 5);
        this.setFieldValue('charger-switch-type', 'charge-fet');
        this.setFieldValue('charge-restart-level', 90);
        this.setFieldValue('speed-sensor-input', 'disabled');
        this.setFieldValue('distance-unit', 'kilometers');
        this.setFieldValue('pulses-per-unit', 1000);
        this.setFieldValue('protocol', 'cav3');
        this.setFieldValue('broadcast', 'disabled');
        this.setFieldValue('temperature-sensor-type', 'dual-10k-ntc');
        this.setFieldValue('external-current-sensor', 'none');

        console.log('Default values loaded');
    }

    /**
     * Populate form fields with configuration data
     */
    populateFormFields(config) {
        if (config.cellSettings) {
            Object.entries(config.cellSettings).forEach(([key, value]) => {
                this.setFieldValue(key, value);
            });
        }

        if (config.safetySettings) {
            Object.entries(config.safetySettings).forEach(([key, value]) => {
                this.setFieldValue(key, value);
            });
        }

        if (config.peripheralsSettings) {
            Object.entries(config.peripheralsSettings).forEach(([key, value]) => {
                this.setFieldValue(key, value);
            });
        }

        // Update UI based on mode
        this.handleBMSModeChange({ target: { value: config.peripheralsSettings?.['bms-mode'] || 'dual-port' } });
    }

    /**
     * Set field value by ID
     */
    setFieldValue(fieldId, value) {
        const field = document.getElementById(fieldId);
        if (!field) return;

        if (field.type === 'checkbox') {
            field.checked = value;
        } else {
            field.value = value;
        }
    }

    /**
     * Get field value by ID
     */
    getFieldValue(fieldId) {
        const field = document.getElementById(fieldId);
        if (!field) return null;

        if (field.type === 'checkbox') {
            return field.checked;
        } else if (field.type === 'number') {
            return parseFloat(field.value);
        } else {
            return field.value;
        }
    }

    /**
     * Handle BMS mode change
     */
    handleBMSModeChange(event) {
        const mode = event.target.value;
        const singlePortGroup = document.getElementById('single-port-switch-type-group');
        const loadSettingsGroup = document.getElementById('load-settings-group');
        const chargerSwitchTypeGroup = document.getElementById('charger-switch-type-group');

        if (mode === 'single-port') {
            if (singlePortGroup) singlePortGroup.style.display = 'block';
            if (loadSettingsGroup) loadSettingsGroup.style.display = 'none';
            if (chargerSwitchTypeGroup) chargerSwitchTypeGroup.style.display = 'none';
        } else {
            if (singlePortGroup) singlePortGroup.style.display = 'none';
            if (loadSettingsGroup) loadSettingsGroup.style.display = 'block';
            if (chargerSwitchTypeGroup) chargerSwitchTypeGroup.style.display = 'block';
        }
    }

    /**
     * Handle Cell Settings form submission
     */
    async handleCellSettingsSubmit(event) {
        event.preventDefault();

        const data = {
            fullyChargedVoltage: this.getFieldValue('fully-charged-voltage'),
            chargeFinishedCurrent: this.getFieldValue('charge-finished-current'),
            fullyDischargedVoltage: this.getFieldValue('fully-discharged-voltage'),
            earlyBalancingThreshold: this.getFieldValue('early-balancing-threshold'),
            allowedDisbalance: this.getFieldValue('allowed-disbalance'),
            numberOfSeriesCells: this.getFieldValue('number-of-series-cells'),
            batteryCapacity: this.getFieldValue('battery-capacity'),
            setSocManually: this.getFieldValue('set-soc-manually'),
            batteryMaximumCycles: this.getFieldValue('battery-maximum-cycles'),
            setSohManually: this.getFieldValue('set-soh-manually'),
        };

        await this.uploadSettings('cell-settings', data);
    }

    /**
     * Handle Safety form submission
     */
    async handleSafetySubmit(event) {
        event.preventDefault();

        const data = {
            overVoltageCutoff: this.getFieldValue('over-voltage-cutoff'),
            underVoltageCutoff: this.getFieldValue('under-voltage-cutoff'),
            dischargeOverCurrentCutoff: this.getFieldValue('discharge-over-current-cutoff'),
            dischargeOverCurrentTimeout: this.getFieldValue('discharge-over-current-timeout'),
            dischargePeakCurrentCutoff: this.getFieldValue('discharge-peak-current-cutoff'),
            chargeOverCurrentCutoff: this.getFieldValue('charge-over-current-cutoff'),
            overHeatCutoff: this.getFieldValue('over-heat-cutoff'),
            lowTempChargerCutoff: this.getFieldValue('low-temp-charger-cutoff'),
            automaticRecovery: this.getFieldValue('automatic-recovery'),
            invertCurrentSensor: this.getFieldValue('invert-current-sensor'),
            disableSwitchDiagnostics: this.getFieldValue('disable-switch-diagnostics'),
        };

        await this.uploadSettings('safety', data);
    }

    /**
     * Handle Peripherals form submission
     */
    async handlePeripheralsSubmit(event) {
        event.preventDefault();

        const data = {
            bmsMode: this.getFieldValue('bms-mode'),
            singlePortSwitchType: this.getFieldValue('single-port-switch-type'),
            loadSwitchType: this.getFieldValue('load-switch-type'),
            ignition: this.getFieldValue('ignition'),
            precharge: this.getFieldValue('precharge'),
            prechargeDuration: this.getFieldValue('precharge-duration'),
            chargerType: this.getFieldValue('charger-type'),
            chargerDetection: this.getFieldValue('charger-detection'),
            chargerStartupDelay: this.getFieldValue('charger-startup-delay'),
            chargerDisableDelay: this.getFieldValue('charger-disable-delay'),
            chargerSwitchType: this.getFieldValue('charger-switch-type'),
            enableChargerRestartLevel: this.getFieldValue('enable-charger-restart-level'),
            chargeRestartLevel: this.getFieldValue('charge-restart-level'),
            speedSensorInput: this.getFieldValue('speed-sensor-input'),
            distanceUnit: this.getFieldValue('distance-unit'),
            pulsesPerUnit: this.getFieldValue('pulses-per-unit'),
            protocol: this.getFieldValue('protocol'),
            broadcast: this.getFieldValue('broadcast'),
            temperatureSensorType: this.getFieldValue('temperature-sensor-type'),
            externalCurrentSensor: this.getFieldValue('external-current-sensor'),
        };

        await this.uploadSettings('peripherals', data);
    }

    /**
     * Upload settings to BMS
     */
    async uploadSettings(category, data) {
        try {
            this.showNotification(`Envoi des paramètres ${category} au BMS...`, 'info');

            const response = await fetch(`/api/tinybms/config/${category}`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(data),
            });

            if (!response.ok) {
                throw new Error(`Erreur ${response.status}: ${response.statusText}`);
            }

            const result = await response.json();
            this.showNotification(`✓ Configuration ${category} enregistrée avec succès`, 'success');

            // Reload configuration from BMS
            setTimeout(() => this.loadConfiguration(), 1000);

        } catch (error) {
            console.error(`Error uploading ${category} settings:`, error);
            this.showNotification(`✗ Erreur lors de l'enregistrement: ${error.message}`, 'danger');
        }
    }

    /**
     * Reset form to original values
     */
    resetForm(category) {
        if (!this.originalConfig) {
            this.showNotification('Aucune configuration originale disponible', 'warning');
            return;
        }

        if (category === 'cell' && this.originalConfig.cellSettings) {
            this.populateFormFields({ cellSettings: this.originalConfig.cellSettings });
        } else if (category === 'safety' && this.originalConfig.safetySettings) {
            this.populateFormFields({ safetySettings: this.originalConfig.safetySettings });
        } else if (category === 'peripherals' && this.originalConfig.peripheralsSettings) {
            this.populateFormFields({ peripheralsSettings: this.originalConfig.peripheralsSettings });
            this.handleBMSModeChange({ target: { value: this.originalConfig.peripheralsSettings['bms-mode'] } });
        }

        this.showNotification('Formulaire réinitialisé', 'info');
    }

    /**
     * Load configuration from file
     */
    async loadConfigFromFile() {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.json,.bms';

        input.onchange = async (e) => {
            const file = e.target.files[0];
            if (!file) return;

            try {
                const text = await file.text();
                const config = JSON.parse(text);

                this.populateFormFields(config);
                this.showNotification('✓ Configuration chargée depuis le fichier', 'success');

                const statusEl = document.getElementById('config-status-text');
                if (statusEl) {
                    statusEl.textContent = `Configuration loaded from ${file.name}`;
                }
            } catch (error) {
                console.error('Error loading config file:', error);
                this.showNotification(`✗ Erreur de lecture: ${error.message}`, 'danger');
            }
        };

        input.click();
    }

    /**
     * Save configuration to file
     */
    saveConfigToFile() {
        const config = {
            cellSettings: {},
            safetySettings: {},
            peripheralsSettings: {},
        };

        // Collect all form values
        const cellFields = ['fully-charged-voltage', 'charge-finished-current', 'fully-discharged-voltage',
            'early-balancing-threshold', 'allowed-disbalance', 'number-of-series-cells',
            'battery-capacity', 'set-soc-manually', 'battery-maximum-cycles', 'set-soh-manually'];

        cellFields.forEach(field => {
            config.cellSettings[field] = this.getFieldValue(field);
        });

        const safetyFields = ['over-voltage-cutoff', 'under-voltage-cutoff', 'discharge-over-current-cutoff',
            'discharge-over-current-timeout', 'discharge-peak-current-cutoff', 'charge-over-current-cutoff',
            'over-heat-cutoff', 'low-temp-charger-cutoff', 'automatic-recovery',
            'invert-current-sensor', 'disable-switch-diagnostics'];

        safetyFields.forEach(field => {
            config.safetySettings[field] = this.getFieldValue(field);
        });

        const peripheralsFields = ['bms-mode', 'single-port-switch-type', 'load-switch-type', 'ignition',
            'precharge', 'precharge-duration', 'charger-type', 'charger-detection',
            'charger-startup-delay', 'charger-disable-delay', 'charger-switch-type',
            'enable-charger-restart-level', 'charge-restart-level', 'speed-sensor-input',
            'distance-unit', 'pulses-per-unit', 'protocol', 'broadcast',
            'temperature-sensor-type', 'external-current-sensor'];

        peripheralsFields.forEach(field => {
            config.peripheralsSettings[field] = this.getFieldValue(field);
        });

        // Download as JSON
        const blob = new Blob([JSON.stringify(config, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `tinybms-config-${new Date().toISOString().split('T')[0]}.json`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);

        this.showNotification('✓ Configuration sauvegardée', 'success');

        const statusEl = document.getElementById('config-status-text');
        if (statusEl) {
            statusEl.textContent = `Configuration saved to ${a.download}`;
        }
    }

    /**
     * Upload complete configuration to BMS
     */
    async uploadConfigToBMS() {
        if (!confirm('Voulez-vous vraiment envoyer toute la configuration au BMS ? Le BMS va redémarrer.')) {
            return;
        }

        try {
            this.showNotification('Envoi de la configuration complète au BMS...', 'info');

            const response = await fetch('/api/tinybms/config/upload', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
            });

            if (!response.ok) {
                throw new Error(`Erreur ${response.status}: ${response.statusText}`);
            }

            this.showNotification('✓ Configuration complète envoyée. Le BMS va redémarrer.', 'success');

            const statusEl = document.getElementById('config-status-text');
            if (statusEl) {
                statusEl.textContent = 'Configuration uploaded to BMS';
            }

        } catch (error) {
            console.error('Error uploading configuration:', error);
            this.showNotification(`✗ Erreur: ${error.message}`, 'danger');
        }
    }

    /**
     * Update BMS firmware
     */
    async updateFirmware() {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.bms,.bin';

        input.onchange = async (e) => {
            const file = e.target.files[0];
            if (!file) return;

            if (!confirm(`Voulez-vous vraiment mettre à jour le firmware avec ${file.name} ? Cette opération peut prendre plusieurs minutes.`)) {
                return;
            }

            try {
                const formData = new FormData();
                formData.append('firmware', file);

                const progressContainer = document.getElementById('firmware-progress-container');
                const progressBar = document.getElementById('firmware-progress');
                const statusText = document.getElementById('firmware-status-text');

                if (progressContainer) progressContainer.style.display = 'block';
                if (statusText) statusText.textContent = 'Uploading firmware...';

                const response = await fetch('/api/tinybms/firmware/update', {
                    method: 'POST',
                    body: formData,
                });

                if (!response.ok) {
                    throw new Error(`Erreur ${response.status}: ${response.statusText}`);
                }

                // Simulate progress (in real implementation, use server-sent events or polling)
                let progress = 0;
                const interval = setInterval(() => {
                    progress += 5;
                    if (progressBar) {
                        progressBar.style.width = `${progress}%`;
                        progressBar.textContent = `${progress}%`;
                    }

                    if (progress >= 100) {
                        clearInterval(interval);
                        if (statusText) statusText.textContent = 'Firmware updated successfully!';
                        this.showNotification('✓ Firmware mis à jour avec succès', 'success');

                        setTimeout(() => {
                            if (progressContainer) progressContainer.style.display = 'none';
                            if (progressBar) {
                                progressBar.style.width = '0%';
                                progressBar.textContent = '0%';
                            }
                        }, 3000);
                    }
                }, 200);

            } catch (error) {
                console.error('Error updating firmware:', error);
                this.showNotification(`✗ Erreur de mise à jour: ${error.message}`, 'danger');

                const statusText = document.getElementById('firmware-status-text');
                if (statusText) statusText.textContent = `Error: ${error.message}`;
            }
        };

        input.click();
    }

    /**
     * Restart BMS
     */
    async restartBMS() {
        if (!confirm('Voulez-vous vraiment redémarrer le BMS ? Cette opération peut prendre quelques secondes.')) {
            return;
        }

        try {
            this.showNotification('Redémarrage du BMS en cours...', 'info');

            const response = await fetch('/api/tinybms/restart', {
                method: 'POST',
            });

            if (!response.ok) {
                throw new Error(`Erreur ${response.status}: ${response.statusText}`);
            }

            this.showNotification('✓ BMS redémarré avec succès', 'success');

            // Reload configuration after restart
            setTimeout(() => this.loadConfiguration(), 5000);

        } catch (error) {
            console.error('Error restarting BMS:', error);
            this.showNotification(`✗ Erreur de redémarrage: ${error.message}`, 'danger');
        }
    }

    /**
     * Show notification toast
     */
    showNotification(message, type = 'info') {
        // Create toast notification
        const toast = document.createElement('div');
        toast.className = `alert alert-${type} alert-dismissible fade show position-fixed top-0 end-0 m-3`;
        toast.style.zIndex = '9999';
        toast.innerHTML = `
            <div class="d-flex align-items-center">
                <i class="ti ti-${type === 'success' ? 'check' : type === 'danger' ? 'x' : 'info-circle'} me-2"></i>
                <span>${message}</span>
                <button type="button" class="btn-close ms-auto" data-bs-dismiss="alert"></button>
            </div>
        `;

        document.body.appendChild(toast);

        // Auto-dismiss after 5 seconds
        setTimeout(() => {
            toast.classList.remove('show');
            setTimeout(() => toast.remove(), 150);
        }, 5000);
    }
}

// Auto-initialize when loaded
const tinyBMSConfig = new TinyBMSConfigManager();

// Export for use in other modules
export default tinyBMSConfig;
