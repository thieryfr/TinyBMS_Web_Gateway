/**
 * @file tinybms-config.js
 * @brief TinyBMS Battery Configuration UI Module
 * @description Handles the complete TinyBMS configuration interface
 */

export class TinyBMSConfigManager {
    constructor() {
        this.config = {
            cellSettings: {},
            safetySettings: {},
            peripheralsSettings: {},
        };
        this.originalConfig = null;
        this.registers = new Map(); // Map of register number -> register descriptor/value

        // Register mapping: field-id -> register number
        this.registerMap = {
            'fully-charged-voltage': 300,
            'fully-discharged-voltage': 301,
            'early-balancing-threshold': 303,
            'charge-finished-current': 304,
            'discharge-peak-current-cutoff': 305,
            'battery-capacity': 306,
            'number-of-series-cells': 307,
            'allowed-disbalance': 308,
            'charger-startup-delay': 310,
            'charger-disable-delay': 311,
            'over-voltage-cutoff': 315,
            'under-voltage-cutoff': 316,
            'discharge-over-current-cutoff': 317,
            'charge-over-current-cutoff': 318,
            'over-heat-cutoff': 319,
            'low-temp-charger-cutoff': 320,
            'charge-restart-level': 321,
            'battery-maximum-cycles': 322,
            'set-soh-manually': 323,
            'set-soc-manually': 328,
            'invert-current-sensor': 329,
            'charger-type': 330,
            'load-switch-type': 331,
            'automatic-recovery': 332,
            'charger-switch-type': 333,
            'ignition': 334,
            'charger-detection': 335,
            'precharge': 337,
            'precharge-duration': 338,
            'temperature-sensor-type': 339,
            'bms-mode': 340,
            'single-port-switch-type': 341,
            'broadcast': 342,
            'protocol': 343,
        };

        this.registerKeyMap = {
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
        await this.loadRegisters();
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

        // Configuration tracking buttons
        const refreshCurrentValuesBtn = document.getElementById('refresh-current-values');
        if (refreshCurrentValuesBtn) {
            refreshCurrentValuesBtn.addEventListener('click', () => this.refreshCurrentValues());
        }

        const compareConfigBtn = document.getElementById('compare-config');
        if (compareConfigBtn) {
            compareConfigBtn.addEventListener('click', () => this.compareConfiguration());
        }

        console.log('Event listeners attached');
    }

    /**
     * Load registers from the backend API
     */
    async loadRegisters() {
        try {
            const response = await fetch('/api/registers');
            if (!response.ok) {
                console.warn('Failed to load registers from /api/registers, will try legacy method');
                return;
            }

            const data = await response.json();
            const registers = data.registers || [];

            this.registers.clear();

            registers.forEach(reg => {
                const key = reg.key || this.registerKeyMap[reg.address];
                const currentValue =
                    typeof reg.current_user_value !== 'undefined'
                        ? reg.current_user_value
                        : reg.value;
                this.registers.set(reg.address, {
                    ...reg,
                    key,
                    current_user_value: currentValue,
                });
            });

            // Display current values in spans
            this.displayCurrentValues();

            // Update tracking info
            this.updateConfigTrackingInfo();

            console.log(`Loaded ${registers.length} TinyBMS registers`);
        } catch (error) {
            console.error('Error loading registers:', error);
        }
    }

    /**
     * Display current register values in the UI
     */
    displayCurrentValues() {
        Object.entries(this.registerMap).forEach(([fieldId, registerNum]) => {
            const register = this.registers.get(registerNum);
            const value = this.getRegisterUserValue(register);
            if (value !== undefined) {
                const currentSpan = document.getElementById(`current-${fieldId}`);
                if (currentSpan) {
                    // Format value based on field type
                    const field = document.getElementById(fieldId);
                    if (field) {
                        if (field.tagName === 'SELECT') {
                            // For select fields, show the selected option text
                            const option = Array.from(field.options).find(opt => opt.value == value);
                            currentSpan.textContent = option ? option.text : value;
                        } else if (field.type === 'checkbox') {
                            currentSpan.textContent = value ? 'Oui' : 'Non';
                        } else if (field.type === 'number') {
                            // Format numeric values
                            const step = parseFloat(field.step) || 1;
                            const decimals = step < 1 ? 3 : step < 0.1 ? 2 : 0;
                            currentSpan.textContent = parseFloat(value).toFixed(decimals);
                        } else {
                            currentSpan.textContent = value;
                        }
                    } else {
                        currentSpan.textContent = value;
                    }
                }
            }
        });
    }

    /**
     * Load configuration from BMS
     */
    async loadConfiguration() {
        try {
            // Use register values to populate form
            Object.entries(this.registerMap).forEach(([fieldId, registerNum]) => {
                const register = this.registers.get(registerNum);
                const value = this.getRegisterUserValue(register);
                if (value !== undefined) {
                    this.setFieldValue(fieldId, value);
                }
            });

            console.log('Configuration loaded successfully from registers');
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
        }

        if (field.type === 'number') {
            const value = field.value.trim();
            return value === '' ? null : parseFloat(value);
        }

        if (field.tagName === 'SELECT') {
            const selectedValue = field.value;
            if (selectedValue === '') {
                return null;
            }
            const numeric = Number(selectedValue);
            return Number.isNaN(numeric) ? selectedValue : numeric;
        }

        const value = field.value.trim();
        if (value === '') {
            return null;
        }

        const numeric = Number(value);
        return Number.isNaN(numeric) ? value : numeric;
    }

    getRegisterUserValue(register) {
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

    normalizeValueForRegister(register, rawValue) {
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
            if (register && (register.is_enum || Array.isArray(register.enum) || Array.isArray(register.enum_options))) {
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

    buildRegisterUpdates(fieldIds) {
        const updates = [];

        fieldIds.forEach(fieldId => {
            const registerNum = this.registerMap[fieldId];
            if (registerNum === undefined) {
                return;
            }

            const register = this.registers.get(registerNum);
            const key = register?.key || this.registerKeyMap[registerNum];
            if (!key) {
                console.warn(`No key found for register ${registerNum} (${fieldId})`);
                return;
            }

            const rawValue = this.getFieldValue(fieldId);
            const value = this.normalizeValueForRegister(register, rawValue);
            if (value === null || typeof value === 'undefined') {
                return;
            }
            if (typeof value === 'number' && Number.isNaN(value)) {
                return;
            }

            const currentValue = this.getRegisterUserValue(register);
            let hasChanged = true;
            if (currentValue !== undefined && currentValue !== null) {
                if (typeof value === 'number' && typeof currentValue === 'number') {
                    hasChanged = Math.abs(value - currentValue) > 1e-6;
                } else {
                    hasChanged = value !== currentValue;
                }
            }

            if (!hasChanged) {
                return;
            }

            updates.push({
                address: registerNum,
                key,
                value,
                fieldId,
            });
        });

        return updates;
    }

    async sendRegisterUpdates(updates) {
        const responses = [];
        for (const update of updates) {
            const body = JSON.stringify({
                key: update.key,
                value: update.value,
            });

            const response = await fetch('/api/registers', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body,
            });

            if (!response.ok) {
                const errorText = await response.text();
                throw new Error(`Registre ${update.key}: ${errorText || response.statusText}`);
            }

            let payload = null;
            try {
                payload = await response.json();
            } catch (jsonError) {
                // Response without JSON body; ignore
            }

            responses.push({ ...update, payload });
        }

        return responses;
    }

    applyLocalRegisterUpdate(address, value) {
        const register = this.registers.get(address);
        if (register) {
            register.current_user_value = value;
            register.value = value;
            this.registers.set(address, register);
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

        const fieldIds = [
            'fully-charged-voltage',
            'charge-finished-current',
            'fully-discharged-voltage',
            'early-balancing-threshold',
            'allowed-disbalance',
            'number-of-series-cells',
            'battery-capacity',
            'set-soc-manually',
            'battery-maximum-cycles',
            'set-soh-manually',
        ];

        await this.uploadRegisters('Cell Settings', fieldIds);
    }

    /**
     * Handle Safety form submission
     */
    async handleSafetySubmit(event) {
        event.preventDefault();

        const fieldIds = [
            'over-voltage-cutoff',
            'under-voltage-cutoff',
            'discharge-over-current-cutoff',
            'discharge-peak-current-cutoff',
            'charge-over-current-cutoff',
            'over-heat-cutoff',
            'low-temp-charger-cutoff',
            'automatic-recovery',
            'invert-current-sensor',
        ];

        await this.uploadRegisters('Safety', fieldIds);
    }

    /**
     * Handle Peripherals form submission
     */
    async handlePeripheralsSubmit(event) {
        event.preventDefault();

        const fieldIds = [
            'bms-mode',
            'single-port-switch-type',
            'load-switch-type',
            'ignition',
            'precharge',
            'precharge-duration',
            'charger-type',
            'charger-detection',
            'charger-startup-delay',
            'charger-disable-delay',
            'charger-switch-type',
            'charge-restart-level',
            'protocol',
            'broadcast',
            'temperature-sensor-type',
        ];

        await this.uploadRegisters('Peripherals', fieldIds);
    }

    /**
     * Upload registers to BMS via /api/registers
     */
    async uploadRegisters(category, fieldIds) {
        const updates = this.buildRegisterUpdates(fieldIds);

        if (updates.length === 0) {
            this.showNotification(`Aucune modification détectée pour ${category}`, 'info');
            return;
        }

        try {
            this.showNotification(`Envoi des paramètres ${category} au BMS...`, 'info');

            await this.sendRegisterUpdates(updates);

            updates.forEach(update => {
                this.applyLocalRegisterUpdate(update.address, update.value);
            });

            this.displayCurrentValues();
            this.updateConfigTrackingInfo();

            this.showNotification(`✓ Configuration ${category} enregistrée avec succès (${updates.length} registre${updates.length > 1 ? 's' : ''})`, 'success');
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
            'discharge-peak-current-cutoff', 'charge-over-current-cutoff',
            'over-heat-cutoff', 'low-temp-charger-cutoff', 'automatic-recovery',
            'invert-current-sensor'];

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
        if (!confirm('Voulez-vous vraiment envoyer toute la configuration au BMS ?')) {
            return;
        }

        try {
            this.showNotification('Envoi de la configuration complète au BMS...', 'info');

            // Collect all field IDs from all sections
            const allFieldIds = [
                // Cell Settings
                'fully-charged-voltage',
                'fully-discharged-voltage',
                'early-balancing-threshold',
                'charge-finished-current',
                'battery-capacity',
                'number-of-series-cells',
                'allowed-disbalance',
                'battery-maximum-cycles',
                'set-soh-manually',
                'set-soc-manually',
                // Safety Settings
                'discharge-peak-current-cutoff',
                'over-voltage-cutoff',
                'under-voltage-cutoff',
                'discharge-over-current-cutoff',
                'charge-over-current-cutoff',
                'over-heat-cutoff',
                'low-temp-charger-cutoff',
                'automatic-recovery',
                'invert-current-sensor',
                // Peripherals Settings
                'charger-startup-delay',
                'charger-disable-delay',
                'charge-restart-level',
                'charger-type',
                'load-switch-type',
                'charger-switch-type',
                'ignition',
                'charger-detection',
                'precharge',
                'precharge-duration',
                'temperature-sensor-type',
                'bms-mode',
                'single-port-switch-type',
                'broadcast',
                'protocol',
            ];

            const updates = this.buildRegisterUpdates(allFieldIds);
            if (updates.length === 0) {
                this.showNotification('Aucune modification à envoyer', 'info');
                return;
            }

            await this.sendRegisterUpdates(updates);

            updates.forEach(update => {
                this.applyLocalRegisterUpdate(update.address, update.value);
            });

            this.displayCurrentValues();
            this.updateConfigTrackingInfo();

            this.showNotification(`✓ Configuration complète envoyée avec succès (${updates.length} registre${updates.length > 1 ? 's' : ''})`, 'success');

            const statusEl = document.getElementById('config-status-text');
            if (statusEl) {
                statusEl.textContent = `Configuration uploaded to BMS (${updates.length} registers)`;
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

                const response = await fetch('/api/ota', {
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

            const response = await fetch('/api/system/restart', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ target: 'bms' }),
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

    /**
     * Refresh current values from BMS
     */
    async refreshCurrentValues() {
        try {
            this.showNotification('Rafraîchissement des valeurs...', 'info');

            const statusBadge = document.getElementById('config-sync-status');
            if (statusBadge) {
                statusBadge.textContent = 'Synchronisation...';
                statusBadge.className = 'badge bg-warning';
            }

            await this.loadRegisters();
            await this.loadConfiguration();

            // Update tracking info
            this.updateConfigTrackingInfo();

            this.showNotification('✓ Valeurs rafraîchies avec succès', 'success');

            if (statusBadge) {
                statusBadge.textContent = 'Synchronisé';
                statusBadge.className = 'badge bg-success';
            }
        } catch (error) {
            console.error('Error refreshing values:', error);
            this.showNotification(`✗ Erreur: ${error.message}`, 'danger');

            const statusBadge = document.getElementById('config-sync-status');
            if (statusBadge) {
                statusBadge.textContent = 'Erreur';
                statusBadge.className = 'badge bg-danger';
            }
        }
    }

    /**
     * Compare current form values with BMS values
     */
    compareConfiguration() {
        const differences = [];

        Object.entries(this.registerMap).forEach(([fieldId, registerNum]) => {
            const register = this.registers.get(registerNum);
            const currentValue = this.getRegisterUserValue(register);
            const formValue = this.getFieldValue(fieldId);

            if (currentValue !== undefined && formValue !== null && formValue !== undefined) {
                // Compare values (with tolerance for floating point)
                if (typeof formValue === 'number' && typeof currentValue === 'number') {
                    if (Math.abs(formValue - currentValue) > 0.001) {
                        differences.push({
                            field: fieldId,
                            register: registerNum,
                            bmsValue: currentValue,
                            formValue: formValue
                        });
                    }
                } else if (formValue != currentValue) {
                    differences.push({
                        field: fieldId,
                        register: registerNum,
                        bmsValue: currentValue,
                        formValue: formValue
                    });
                }
            }
        });

        if (differences.length === 0) {
            this.showNotification('✓ Configuration identique au BMS', 'success');
        } else {
            const message = `${differences.length} différence(s) détectée(s) entre le formulaire et le BMS`;
            this.showNotification(message, 'warning');
            console.table(differences);
        }
    }

    /**
     * Update configuration tracking information
     */
    updateConfigTrackingInfo() {
        const lastUpdateEl = document.getElementById('config-last-update');
        if (lastUpdateEl) {
            const now = new Date();
            lastUpdateEl.textContent = now.toLocaleTimeString('fr-FR');
        }

        const registersCountEl = document.getElementById('config-registers-count');
        if (registersCountEl) {
            registersCountEl.textContent = this.registers.size;
        }

        const statusBadge = document.getElementById('config-sync-status');
        if (statusBadge) {
            statusBadge.textContent = 'Synchronisé';
            statusBadge.className = 'badge bg-success';
        }
    }
}

// Auto-initialize when loaded
const tinyBMSConfig = new TinyBMSConfigManager();

// Export for use in other modules
export default tinyBMSConfig;
