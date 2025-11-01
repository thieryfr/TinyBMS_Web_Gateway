/**
 * TinyBMS Configuration Logic
 * Handles 30+ registers configuration with validation and UART communication
 */

// ============================================
// Register Definitions
// ============================================

const REGISTERS = {
    // Battery Pack (300-308)
    300: { name: 'Fully Charged Voltage', unit: 'mV', min: 3200, max: 3900, default: 3650, group: 'pack' },
    302: { name: 'Fully Discharged Voltage', unit: 'mV', min: 2500, max: 3500, default: 3250, group: 'pack' },
    304: { name: 'Charge Finished Current', unit: 'mA', min: 100, max: 5000, default: 1000, group: 'pack' },
    306: { name: 'Battery Capacity', unit: '0.01Ah', min: 5000, max: 50000, default: 31400, group: 'pack', display: (v) => `${(v/100).toFixed(2)} Ah` },
    307: { name: 'Cell Count', unit: 'cells', min: 4, max: 32, default: 16, group: 'pack' },

    // Protection (315-320)
    315: { name: 'Overvoltage Cutoff', unit: 'mV', min: 3600, max: 4200, default: 3800, group: 'protection' },
    316: { name: 'Undervoltage Cutoff', unit: 'mV', min: 2000, max: 3000, default: 2800, group: 'protection' },
    317: { name: 'Discharge Overcurrent', unit: 'A', min: 10, max: 200, default: 65, group: 'protection' },
    318: { name: 'Charge Overcurrent', unit: 'A', min: 10, max: 150, default: 90, group: 'protection' },
    319: { name: 'Overheat Temperature', unit: '0.1°C', min: 400, max: 800, default: 600, group: 'protection', display: (v) => `${(v/10).toFixed(1)} °C` },
    320: { name: 'Low Temp Charge', unit: '0.1°C', min: -200, max: 100, default: 0, group: 'protection', display: (v) => `${(v/10).toFixed(1)} °C` },

    // Communication (342-343)
    342: { name: 'Broadcast Enable', unit: 'bool', min: 0, max: 1, default: 1, group: 'comm' },
    343: { name: 'Protocol Selection', unit: 'enum', min: 0, max: 3, default: 2, group: 'comm' }
};

// ============================================
// Configuration Presets
// ============================================

const CONFIG_PRESETS = {
    'lifepo4_16s_default': {
        name: 'LiFePO4 16S (Défaut)',
        description: 'Configuration standard pour batterie LiFePO4 16S (51.2V nominal)',
        icon: 'battery-three-quarters',
        values: {
            300: 3650,  // Fully Charged: 3.65V per cell
            302: 3250,  // Fully Discharged: 3.25V per cell
            304: 1000,  // Charge Finished Current: 1A
            306: 31400, // Capacity: 314Ah
            307: 16,    // Cell Count: 16
            315: 3800,  // Overvoltage Cutoff: 3.8V
            316: 2800,  // Undervoltage Cutoff: 2.8V
            317: 65,    // Discharge Overcurrent: 65A
            318: 90,    // Charge Overcurrent: 90A
            319: 600,   // Overheat: 60°C
            320: 0,     // Low Temp Charge: 0°C
            342: 1,     // Broadcast Enable
            343: 2      // Protocol
        }
    },
    'lifepo4_12s': {
        name: 'LiFePO4 12S',
        description: 'Configuration pour batterie LiFePO4 12S (38.4V nominal)',
        icon: 'battery-half',
        values: {
            300: 3650,
            302: 3250,
            304: 1000,
            306: 31400,
            307: 12,    // 12 cells
            315: 3800,
            316: 2800,
            317: 65,
            318: 90,
            319: 600,
            320: 0,
            342: 1,
            343: 2
        }
    },
    'lifepo4_fast_charge': {
        name: 'Charge Rapide',
        description: 'Configuration optimisée pour charge rapide (augmente les limites de courant)',
        icon: 'bolt',
        values: {
            300: 3650,
            302: 3250,
            304: 2000,  // Charge Finished: 2A (plus élevé)
            306: 31400,
            307: 16,
            315: 3750,  // Overvoltage plus conservateur
            316: 2900,  // Undervoltage plus conservateur
            317: 90,    // Discharge: 90A (augmenté)
            318: 120,   // Charge: 120A (augmenté)
            319: 550,   // Overheat: 55°C (plus strict)
            320: 50,    // Low Temp: 5°C (protection température basse)
            342: 1,
            343: 2
        }
    },
    'lifepo4_longevity': {
        name: 'Longévité Maximale',
        description: 'Configuration conservatrice pour maximiser la durée de vie de la batterie',
        icon: 'heart',
        values: {
            300: 3550,  // Fully Charged: 3.55V (plus bas)
            302: 3300,  // Fully Discharged: 3.3V (plus haut)
            304: 500,   // Charge Finished: 0.5A (plus strict)
            306: 31400,
            307: 16,
            315: 3650,  // Overvoltage: 3.65V (plus bas)
            316: 3000,  // Undervoltage: 3.0V (plus haut)
            317: 50,    // Discharge: 50A (limité)
            318: 60,    // Charge: 60A (limité)
            319: 500,   // Overheat: 50°C (strict)
            320: 100,   // Low Temp: 10°C (strict)
            342: 1,
            343: 2
        }
    },
    'lifepo4_high_power': {
        name: 'Haute Puissance',
        description: 'Configuration pour applications haute puissance (inverseurs, moteurs)',
        icon: 'fire',
        values: {
            300: 3650,
            302: 3250,
            304: 1500,
            306: 31400,
            307: 16,
            315: 3800,
            316: 2800,
            317: 150,   // Discharge: 150A (très élevé)
            318: 120,   // Charge: 120A
            319: 600,
            320: 0,
            342: 1,
            343: 2
        }
    }
};

// Register state
let registerValues = {};
let registerBmsValues = {};
let registerStates = {}; // 'synced', 'modified', 'error', 'writing', 'reading'

// ============================================
// Initialize Config
// ============================================

function initConfig() {
    console.log('[Config] Initializing...');

    // Initialize register values with defaults
    Object.keys(REGISTERS).forEach(reg => {
        const regNum = parseInt(reg);
        registerValues[regNum] = REGISTERS[regNum].default;
        registerBmsValues[regNum] = REGISTERS[regNum].default;
        registerStates[regNum] = 'synced';
    });

    // Load protection registers dynamically
    loadProtectionRegisters();

    // Load presets UI
    loadPresetsUI();

    // Setup search
    setupConfigSearch();

    // Setup tooltips
    const tooltips = document.querySelectorAll('[data-bs-toggle="tooltip"]');
    tooltips.forEach(el => new bootstrap.Tooltip(el));

    console.log('[Config] Initialized');
}

// ============================================
// Presets UI and Functions
// ============================================

function loadPresetsUI() {
    const container = document.getElementById('presetsContainer');
    if (!container) return;

    let html = `
        <div class="card mb-4">
            <div class="card-header bg-info text-white">
                <h5 class="mb-0"><i class="fas fa-magic"></i> Presets de Configuration</h5>
            </div>
            <div class="card-body">
                <p class="text-muted">
                    Sélectionnez un preset pour charger une configuration optimisée.
                    <strong>Attention:</strong> Cela modifiera tous les registres sans écriture immédiate.
                </p>
                <div class="row g-3">
    `;

    Object.entries(CONFIG_PRESETS).forEach(([key, preset]) => {
        html += `
            <div class="col-md-6 col-lg-4">
                <div class="card preset-card h-100" onclick="applyPreset('${key}')">
                    <div class="card-body text-center">
                        <i class="fas fa-${preset.icon} fa-3x text-primary mb-3"></i>
                        <h6 class="card-title">${preset.name}</h6>
                        <p class="card-text small text-muted">${preset.description}</p>
                        <button class="btn btn-sm btn-primary" onclick="event.stopPropagation(); applyPreset('${key}')">
                            <i class="fas fa-download"></i> Charger
                        </button>
                    </div>
                </div>
            </div>
        `;
    });

    html += `
                </div>
            </div>
        </div>
    `;

    container.innerHTML = html;
}

async function applyPreset(presetKey) {
    const preset = CONFIG_PRESETS[presetKey];
    if (!preset) {
        showToast('Preset non trouvé', 'error');
        return;
    }

    if (!await confirmAction(
        `Charger le preset "${preset.name}"?`,
        `${preset.description}\n\nCeci modifiera ${Object.keys(preset.values).length} registres localement. Vous devrez cliquer sur "Write All" pour envoyer au BMS.`
    )) {
        return;
    }

    let modifiedCount = 0;

    Object.entries(preset.values).forEach(([reg, value]) => {
        const regNum = parseInt(reg);
        if (REGISTERS[regNum]) {
            updateRegisterValue(regNum, value);
            modifiedCount++;
        }
    });

    showToast(`Preset "${preset.name}" chargé: ${modifiedCount} registres modifiés`, 'success', 5000);
    addNotification(`Configuration preset "${preset.name}" appliquée localement`, 'success');

    // Scroll to protection registers section
    document.getElementById('protectionRegisters')?.scrollIntoView({ behavior: 'smooth', block: 'start' });
}

// ============================================
// Dynamic Protection Registers
// ============================================

function loadProtectionRegisters() {
    const container = document.getElementById('protectionRegisters');
    if (!container) return;
    
    const protectionRegs = Object.entries(REGISTERS).filter(([_, def]) => def.group === 'protection');
    
    container.innerHTML = protectionRegs.map(([reg, def], idx) => {
        const regNum = parseInt(reg);
        const value = registerValues[regNum] || def.default;
        
        return `
            <div class="register-group" data-register="${regNum}">
                <div class="row align-items-center mb-3">
                    <div class="col-md-4">
                        <label class="form-label fw-bold">
                            [${reg}] ${def.name}
                            <i class="fas fa-info-circle text-muted" data-bs-toggle="tooltip" title="${def.name} protection threshold"></i>
                        </label>
                        <small class="text-muted d-block">Range: ${def.min}-${def.max} ${def.unit}</small>
                    </div>
                    <div class="col-md-5">
                        <input type="range" class="form-range" id="reg${regNum}_slider" 
                               min="${def.min}" max="${def.max}" value="${value}" step="${getStep(def)}"
                               oninput="updateRegisterValue(${regNum}, this.value)">
                        <div class="d-flex justify-content-between small text-muted">
                            <span>${def.min} ${def.unit}</span>
                            <span id="reg${regNum}_display" class="fw-bold">${formatValue(value, def)}</span>
                            <span>${def.max} ${def.unit}</span>
                        </div>
                    </div>
                    <div class="col-md-3">
                        <div class="input-group input-group-sm">
                            <input type="number" class="form-control" id="reg${regNum}_input" 
                                   value="${value}" min="${def.min}" max="${def.max}"
                                   onchange="updateRegisterSlider(${regNum}, this.value)">
                            <span class="input-group-text">${def.unit}</span>
                        </div>
                        <div class="mt-1">
                            <button class="btn btn-sm btn-outline-primary" onclick="readRegister(${regNum})">
                                <i class="fas fa-download"></i>
                            </button>
                            <button class="btn btn-sm btn-outline-success" onclick="writeRegister(${regNum})">
                                <i class="fas fa-upload"></i>
                            </button>
                            <span id="reg${regNum}_status" class="badge bg-secondary ms-2">Synced</span>
                        </div>
                    </div>
                </div>
                ${idx < protectionRegs.length - 1 ? '<hr>' : ''}
            </div>
        `;
    }).join('');
    
    // Re-init tooltips
    const tooltips = container.querySelectorAll('[data-bs-toggle="tooltip"]');
    tooltips.forEach(el => new bootstrap.Tooltip(el));
}

// ============================================
// Value Updates
// ============================================

function updateRegisterValue(regNum, value) {
    const def = REGISTERS[regNum];
    if (!def) return;
    
    // Validate
    value = parseFloat(value);
    if (value < def.min) value = def.min;
    if (value > def.max) value = def.max;
    
    // Update state
    registerValues[regNum] = value;
    
    // Update UI
    const slider = document.getElementById(`reg${regNum}_slider`);
    const input = document.getElementById(`reg${regNum}_input`);
    const display = document.getElementById(`reg${regNum}_display`);
    
    if (slider) slider.value = value;
    if (input) input.value = value;
    if (display) display.textContent = formatValue(value, def);
    
    // Check if modified
    if (value !== registerBmsValues[regNum]) {
        setRegisterState(regNum, 'modified');
    } else {
        setRegisterState(regNum, 'synced');
    }
    
    // Special handlers
    if (regNum === 342) {
        // Broadcast Enable toggle
        const switchEl = document.getElementById('reg342_switch');
        const label = document.getElementById('reg342_label');
        if (switchEl) switchEl.checked = value > 0;
        if (label) label.textContent = value > 0 ? 'Enabled' : 'Disabled';
    }
}

function updateRegisterSlider(regNum, value) {
    updateRegisterValue(regNum, value);
}

// ============================================
// Register State Management
// ============================================

function setRegisterState(regNum, state) {
    registerStates[regNum] = state;
    
    const statusBadge = document.getElementById(`reg${regNum}_status`);
    if (!statusBadge) return;
    
    statusBadge.className = 'badge ms-2';
    
    switch(state) {
        case 'synced':
            statusBadge.classList.add('bg-secondary');
            statusBadge.textContent = 'Synced';
            break;
        case 'modified':
            statusBadge.classList.add('bg-warning');
            statusBadge.textContent = 'Modified';
            break;
        case 'error':
            statusBadge.classList.add('bg-danger');
            statusBadge.textContent = 'Error';
            break;
        case 'writing':
            statusBadge.classList.add('bg-info');
            statusBadge.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Writing';
            break;
        case 'reading':
            statusBadge.classList.add('bg-info');
            statusBadge.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Reading';
            break;
    }
}

// ============================================
// Read/Write Operations
// ============================================

async function readRegister(regNum) {
    console.log(`[Config] Reading register ${regNum}...`);
    setRegisterState(regNum, 'reading');
    
    try {
        const response = await fetchAPI(`/api/tinybms/register?address=${regNum}`);
        
        if (response && response.success) {
            const value = response.value;
            registerBmsValues[regNum] = value;
            updateRegisterValue(regNum, value);
            setRegisterState(regNum, 'synced');
            showToast(`Register ${regNum} read successfully`, 'success');
        } else {
            setRegisterState(regNum, 'error');
            showToast(`Failed to read register ${regNum}`, 'error');
        }
    } catch (error) {
        console.error(`[Config] Error reading register ${regNum}:`, error);
        setRegisterState(regNum, 'error');
        showToast(`Error reading register ${regNum}`, 'error');
    }
}

async function writeRegister(regNum) {
    const value = registerValues[regNum];
    const def = REGISTERS[regNum];
    
    // Validate
    if (value < def.min || value > def.max) {
        showToast(`Value out of range for register ${regNum}`, 'error');
        return;
    }
    
    // Confirmation for critical registers
    if (def.group === 'protection') {
        if (!await confirmAction(`Write ${def.name} = ${formatValue(value, def)}?`, 'This will change a protection threshold.')) {
            return;
        }
    }
    
    console.log(`[Config] Writing register ${regNum} = ${value}...`);
    setRegisterState(regNum, 'writing');
    
    try {
        const response = await postAPI('/api/tinybms/register', { address: regNum, value });
        
        if (response && response.success) {
            registerBmsValues[regNum] = value;
            setRegisterState(regNum, 'synced');
            showToast(`Register ${regNum} written successfully`, 'success');
            addLog(`Register ${regNum} (${def.name}) written: ${formatValue(value, def)}`, 'success');
        } else {
            setRegisterState(regNum, 'error');
            showToast(`Failed to write register ${regNum}`, 'error');
        }
    } catch (error) {
        console.error(`[Config] Error writing register ${regNum}:`, error);
        setRegisterState(regNum, 'error');
        showToast(`Error writing register ${regNum}`, 'error');
    }
}

// ============================================
// Bulk Operations
// ============================================

async function readAllRegisters() {
    if (!await confirmAction('Read all registers from BMS?', 'This will overwrite any unsaved changes.')) {
        return;
    }
    
    showToast('Reading all registers...', 'info', 5000);
    
    let successCount = 0;
    let errorCount = 0;
    
    for (const regNum of Object.keys(REGISTERS)) {
        await readRegister(parseInt(regNum));
        const state = registerStates[regNum];
        if (state === 'synced') successCount++;
        else errorCount++;
        
        // Small delay to not overwhelm BMS
        await sleep(100);
    }
    
    showToast(`Read complete: ${successCount} success, ${errorCount} errors`, 
              errorCount > 0 ? 'warning' : 'success');
}

async function writeAllRegisters() {
    // Count modified registers
    const modifiedRegs = Object.keys(registerStates).filter(r => registerStates[r] === 'modified');
    
    if (modifiedRegs.length === 0) {
        showToast('No modified registers to write', 'info');
        return;
    }
    
    if (!await confirmAction(`Write ${modifiedRegs.length} modified registers?`, 
                            'This will permanently change BMS configuration.')) {
        return;
    }
    
    showToast(`Writing ${modifiedRegs.length} registers...`, 'info', 5000);
    
    let successCount = 0;
    let errorCount = 0;
    
    for (const regNum of modifiedRegs) {
        await writeRegister(parseInt(regNum));
        const state = registerStates[regNum];
        if (state === 'synced') successCount++;
        else errorCount++;
        
        // Small delay
        await sleep(200);
    }
    
    showToast(`Write complete: ${successCount} success, ${errorCount} errors`, 
              errorCount > 0 ? 'warning' : 'success');
}

async function resetToDefaults() {
    if (!await confirmAction('Reset all values to BMS?', 'This will reload all registers from the BMS, discarding local changes.')) {
        return;
    }
    
    await readAllRegisters();
}

// ============================================
// Export/Import
// ============================================

function exportConfig() {
    const config = {
        version: '3.0',
        timestamp: new Date().toISOString(),
        registers: {}
    };
    
    Object.keys(REGISTERS).forEach(reg => {
        const regNum = parseInt(reg);
        config.registers[regNum] = {
            name: REGISTERS[regNum].name,
            value: registerValues[regNum],
            unit: REGISTERS[regNum].unit
        };
    });
    
    const json = JSON.stringify(config, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `tinybms_config_${new Date().toISOString().split('T')[0]}.json`;
    a.click();
    URL.revokeObjectURL(url);
    
    showToast('Configuration exported', 'success');
}

function importConfig() {
    const fileInput = document.getElementById('configFileInput');
    const file = fileInput.files[0];
    
    if (!file) {
        showToast('Please select a file', 'warning');
        return;
    }
    
    const reader = new FileReader();
    
    reader.onload = async (e) => {
        try {
            const config = JSON.parse(e.target.result);
            
            if (!config.registers) {
                showToast('Invalid config file', 'error');
                return;
            }
            
            if (!await confirmAction('Import configuration?', `This will load ${Object.keys(config.registers).length} register values.`)) {
                return;
            }
            
            // Load values
            Object.entries(config.registers).forEach(([reg, data]) => {
                const regNum = parseInt(reg);
                if (REGISTERS[regNum]) {
                    updateRegisterValue(regNum, data.value);
                }
            });
            
            showToast('Configuration imported', 'success');
            fileInput.value = ''; // Clear input
            
        } catch (error) {
            console.error('[Config] Import error:', error);
            showToast('Failed to import config', 'error');
        }
    };
    
    reader.readAsText(file);
}

// ============================================
// Factory Reset
// ============================================

async function factoryResetBMS() {
    if (!await confirmAction('⚠️ FACTORY RESET BMS?', 
                            'This will reset ALL BMS settings to factory defaults. This action is IRREVERSIBLE and requires physical confirmation on the BMS.')) {
        return;
    }
    
    // Second confirmation
    if (!await confirmAction('Are you ABSOLUTELY SURE?', 
                            'Last chance to cancel. The BMS will be reset to factory settings.')) {
        return;
    }
    
    showToast('Sending factory reset command...', 'warning', 5000);
    
    try {
        const response = await postAPI('/api/tinybms/factory-reset', {});
        
        if (response && response.success) {
            showToast('Factory reset command sent. Please confirm on BMS unit.', 'info', 10000);
            addLog('Factory reset initiated', 'warning');
            
            // Wait then reload
            setTimeout(() => {
                readAllRegisters();
            }, 5000);
        } else {
            showToast('Factory reset failed', 'error');
        }
    } catch (error) {
        console.error('[Config] Factory reset error:', error);
        showToast('Factory reset error', 'error');
    }
}

// ============================================
// Search & Filter
// ============================================

function setupConfigSearch() {
    const searchInput = document.getElementById('configSearch');
    if (searchInput) {
        searchInput.addEventListener('input', debounce(filterRegisters, 300));
    }
}

function filterRegisters() {
    const searchTerm = document.getElementById('configSearch')?.value.toLowerCase() || '';
    const filter = document.getElementById('configFilter')?.value || 'all';
    
    const allGroups = document.querySelectorAll('.register-group');
    
    allGroups.forEach(group => {
        const regNum = parseInt(group.dataset.register);
        const def = REGISTERS[regNum];
        const state = registerStates[regNum];
        
        let show = true;
        
        // Filter by search term
        if (searchTerm) {
            const searchable = `${regNum} ${def.name} ${def.unit}`.toLowerCase();
            if (!searchable.includes(searchTerm)) {
                show = false;
            }
        }
        
        // Filter by state
        if (filter === 'modified' && state !== 'modified') {
            show = false;
        } else if (filter === 'errors' && state !== 'error') {
            show = false;
        }
        
        group.style.display = show ? '' : 'none';
    });
}

// ============================================
// Helpers
// ============================================

function getStep(def) {
    const range = def.max - def.min;
    if (range > 10000) return 100;
    if (range > 1000) return 10;
    return 1;
}

function formatValue(value, def) {
    if (def.display) {
        return def.display(value);
    }
    return `${value} ${def.unit}`;
}

async function confirmAction(title, message) {
    return new Promise((resolve) => {
        const modalHtml = `
            <div class="modal fade" id="confirmModal" tabindex="-1">
                <div class="modal-dialog">
                    <div class="modal-content">
                        <div class="modal-header bg-warning">
                            <h5 class="modal-title">
                                <i class="fas fa-exclamation-triangle"></i> ${title}
                            </h5>
                            <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
                        </div>
                        <div class="modal-body">
                            <p>${message}</p>
                        </div>
                        <div class="modal-footer">
                            <button type="button" class="btn btn-secondary" data-bs-dismiss="modal" id="confirmNo">
                                Cancel
                            </button>
                            <button type="button" class="btn btn-warning" id="confirmYes">
                                Confirm
                            </button>
                        </div>
                    </div>
                </div>
            </div>
        `;
        
        // Remove existing modal
        const existing = document.getElementById('confirmModal');
        if (existing) existing.remove();
        
        // Add new modal
        document.body.insertAdjacentHTML('beforeend', modalHtml);
        
        const modal = new bootstrap.Modal(document.getElementById('confirmModal'));
        
        document.getElementById('confirmYes').onclick = () => {
            modal.hide();
            resolve(true);
        };
        
        document.getElementById('confirmNo').onclick = () => {
            modal.hide();
            resolve(false);
        };
        
        modal.show();
    });
}

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

function debounce(func, wait) {
    let timeout;
    return function(...args) {
        clearTimeout(timeout);
        timeout = setTimeout(() => func.apply(this, args), wait);
    };
}

// ============================================
// Initialize
// ============================================

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initConfig);
} else {
    initConfig();
}
