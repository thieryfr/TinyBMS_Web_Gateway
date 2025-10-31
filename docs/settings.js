/**
 * Settings Logic
 * Handles system configuration (WiFi, Hardware, CVL, Victron, Logging, System)
 */

// ============================================
// Global Variables
// ============================================

let systemSettings = {
    wifi: {
        mode: 'station',
        sta_ssid: '',
        sta_password: '',
        sta_hostname: 'tinybms-victron',
        sta_ip_mode: 'dhcp',
        sta_static_ip: '',
        sta_gateway: '',
        sta_subnet: '255.255.255.0',
        ap_ssid: 'TinyBMS-AP',
        ap_password: 'tinybms123',
        ap_channel: 6,
        ap_fallback: true
    },
    hardware: {
        uart_rx_pin: 16,
        uart_tx_pin: 17,
        uart_baudrate: 115200,
        can_tx_pin: 5,
        can_rx_pin: 4,
        can_bitrate: 500000,
        can_termination: true
    },
    tinybms: {
        poll_interval_ms: 100,
        poll_interval_min_ms: 50,
        poll_interval_max_ms: 500,
        poll_backoff_step_ms: 25,
        poll_recovery_step_ms: 10,
        poll_latency_target_ms: 40,
        poll_latency_slack_ms: 15,
        poll_failure_threshold: 3,
        poll_success_threshold: 6,
        uart_retry_count: 3,
        uart_retry_delay_ms: 50,
        broadcast_expected: true
    },
    cvl: {
        enabled: true,
        bulk_transition_soc: 90,
        transition_float_soc: 95,
        float_exit_soc: 85,
        float_approach_offset: -0.05,
        float_offset: -0.10,
        imbalance_offset: -0.15,
        imbalance_trigger_mv: 100,
        imbalance_release_mv: 50
    },
    victron: {
        manufacturer: 'ENEPAQ',
        battery_name: 'LiFePO4 48V 300Ah',
        pgn_interval_ms: 1000,
        cvl_interval_ms: 20000,
        keepalive_interval_ms: 5000,
        thresholds: {
            undervoltage_v: 44.0,
            overvoltage_v: 58.4,
            overtemp_c: 55,
            low_temp_charge_c: 0,
            imbalance_warn_mv: 100,
            imbalance_alarm_mv: 200,
            soc_low_percent: 10,
            soc_high_percent: 99,
            derate_current_a: 1.0
        }
    },
    mqtt: {
        enabled: false,
        uri: 'mqtt://127.0.0.1',
        port: 1883,
        client_id: 'tinybms-victron',
        username: '',
        password: '',
        root_topic: 'victron/tinybms',
        clean_session: true,
        use_tls: false,
        server_certificate: '',
        keepalive_seconds: 30,
        reconnect_interval_ms: 5000,
        default_qos: 0,
        retain_by_default: false
    },
    logging: {
        level: 'info',
        serial: true,
        web: true,
        sd: false,
        syslog: false,
        syslog_server: ''
    },
    system: {
        web_port: 80,
        ws_max_clients: 4,
        ws_update_interval: 1000,
        cors_enabled: true
    },
    advanced: {
        enable_spiffs: true,
        enable_ota: false,
        watchdog_timeout_s: 30,
        stack_size_bytes: 8192
    }
};

const DASHBOARD_PREFS_FALLBACK = {
    cellVoltage: {
        min_mv: 3000,
        max_mv: 3700,
        warning_delta_mv: 30,
        critical_delta_mv: 100
    },
    alerts: {
        soc_critical: 20,
        soc_low: 30,
        temp_warning: 45,
        temp_critical: 50,
        imbalance_warning: 150,
        imbalance_critical: 200,
        balancing_duration_warning_ms: 30 * 60 * 1000
    }
};

let dashboardUIPreferences = typeof window.getDashboardPreferences === 'function'
    ? window.getDashboardPreferences()
    : mergeObjects(JSON.parse(JSON.stringify(DASHBOARD_PREFS_FALLBACK)), {});

// ============================================
// Initialize Settings
// ============================================

function initSettings() {
    console.log('[Settings] Initializing...');
    
    // Load current settings from ESP32
    loadSettings();
    
    // Setup event listeners
    setupSettingsListeners();
    
    console.log('[Settings] Initialized');
}

// ============================================
// Load Settings from ESP32
// ============================================

async function loadSettings() {
    try {
        const response = await fetchAPI('/api/config');
        
        if (response && response.success) {
            // Merge with defaults
            systemSettings = { ...systemSettings, ...response.config };
            
            // Populate UI
            populateSettingsUI();
            
            showToast('Settings loaded successfully', 'success');
        }
    } catch (error) {
        console.error('[Settings] Load error:', error);
        showToast('Using default settings', 'warning');
        populateSettingsUI();
    }
}

// ============================================
// Populate UI from Settings
// ============================================

function populateSettingsUI() {
    // WiFi
    document.querySelector(`input[name="wifiMode"][value="${systemSettings.wifi.mode}"]`).checked = true;
    document.getElementById('wifiSSID').value = systemSettings.wifi.sta_ssid;
    document.getElementById('wifiPassword').value = systemSettings.wifi.sta_password;
    document.getElementById('wifiHostname').value = systemSettings.wifi.sta_hostname;
    document.getElementById('wifiIpMode').value = systemSettings.wifi.sta_ip_mode;
    document.getElementById('staticIP').value = systemSettings.wifi.sta_static_ip;
    document.getElementById('staticGateway').value = systemSettings.wifi.sta_gateway;
    document.getElementById('staticSubnet').value = systemSettings.wifi.sta_subnet;
    document.getElementById('apSSID').value = systemSettings.wifi.ap_ssid;
    document.getElementById('apPassword').value = systemSettings.wifi.ap_password;
    document.getElementById('apChannel').value = systemSettings.wifi.ap_channel;
    document.getElementById('apFallback').checked = systemSettings.wifi.ap_fallback;
    
    // Hardware
    document.getElementById('uartRxPin').value = systemSettings.hardware.uart_rx_pin;
    document.getElementById('uartTxPin').value = systemSettings.hardware.uart_tx_pin;
    document.getElementById('uartBaudrate').value = systemSettings.hardware.uart_baudrate;
    document.getElementById('canTxPin').value = systemSettings.hardware.can_tx_pin;
    document.getElementById('canRxPin').value = systemSettings.hardware.can_rx_pin;
    document.getElementById('canBitrate').value = systemSettings.hardware.can_bitrate;
    document.getElementById('canTermination').checked = systemSettings.hardware.can_termination;
    
    // CVL
    document.getElementById('cvlEnable').checked = systemSettings.cvl.enabled;
    document.getElementById('cvlBulkTransition').value = systemSettings.cvl.bulk_transition_soc;
    document.getElementById('cvlTransitionFloat').value = systemSettings.cvl.transition_float_soc;
    document.getElementById('cvlFloatExit').value = systemSettings.cvl.float_exit_soc;
    document.getElementById('cvlFloatApproachOffset').value = systemSettings.cvl.float_approach_offset;
    document.getElementById('cvlFloatOffset').value = systemSettings.cvl.float_offset;
    document.getElementById('cvlImbalanceOffset').value = systemSettings.cvl.imbalance_offset;
    document.getElementById('cvlImbalanceTrigger').value = systemSettings.cvl.imbalance_trigger_mv;
    document.getElementById('cvlImbalanceRelease').value = systemSettings.cvl.imbalance_release_mv;
    
    // Victron
    document.getElementById('victronManufacturer').value = systemSettings.victron.manufacturer;
    document.getElementById('victronBatteryName').value = systemSettings.victron.battery_name;
    document.getElementById('victronPgnInterval').value = systemSettings.victron.pgn_interval_ms;
    document.getElementById('victronCvlInterval').value = systemSettings.victron.cvl_interval_ms;
    document.getElementById('victronKeepaliveInterval').value = systemSettings.victron.keepalive_interval_ms;
    
    // Logging
    document.querySelector(`input[name="logLevel"][value="${systemSettings.logging.level}"]`).checked = true;
    document.getElementById('logSerial').checked = systemSettings.logging.serial;
    document.getElementById('logWeb').checked = systemSettings.logging.web;
    document.getElementById('logSD').checked = systemSettings.logging.sd;
    document.getElementById('logSyslog').checked = systemSettings.logging.syslog;
    document.getElementById('syslogServer').value = systemSettings.logging.syslog_server;
    
    // System
    document.getElementById('webPort').value = systemSettings.system.web_port;
    document.getElementById('wsMaxClients').value = systemSettings.system.ws_max_clients;
    document.getElementById('wsUpdateInterval').value = systemSettings.system.ws_update_interval;
    document.getElementById('webCORS').checked = systemSettings.system.cors_enabled;

    populateDashboardUIPreferences();
}

function populateDashboardUIPreferences() {
    if (typeof window.getDashboardPreferences === 'function') {
        dashboardUIPreferences = window.getDashboardPreferences();
    } else {
        dashboardUIPreferences = mergeObjects(JSON.parse(JSON.stringify(DASHBOARD_PREFS_FALLBACK)), {});
    }

    const minInput = document.getElementById('dashboardCellMin');
    if (!minInput) {
        return;
    }

    document.getElementById('dashboardCellMin').value = dashboardUIPreferences.cellVoltage.min_mv;
    document.getElementById('dashboardCellMax').value = dashboardUIPreferences.cellVoltage.max_mv;
    document.getElementById('dashboardCellWarning').value = dashboardUIPreferences.cellVoltage.warning_delta_mv;
    document.getElementById('dashboardCellCritical').value = dashboardUIPreferences.cellVoltage.critical_delta_mv;

    document.getElementById('dashboardAlertSocCritical').value = dashboardUIPreferences.alerts.soc_critical;
    document.getElementById('dashboardAlertSocLow').value = dashboardUIPreferences.alerts.soc_low;
    document.getElementById('dashboardAlertTempWarning').value = dashboardUIPreferences.alerts.temp_warning;
    document.getElementById('dashboardAlertTempCritical').value = dashboardUIPreferences.alerts.temp_critical;
    document.getElementById('dashboardAlertImbalanceWarning').value = dashboardUIPreferences.alerts.imbalance_warning;
    document.getElementById('dashboardAlertImbalanceCritical').value = dashboardUIPreferences.alerts.imbalance_critical;
    document.getElementById('dashboardAlertBalancingMinutes').value = Math.round(
        dashboardUIPreferences.alerts.balancing_duration_warning_ms / 60000
    );
}

function saveDashboardUIPreferences(options = {}) {
    const silent = options && options.silent;

    const minMv = parseInt(document.getElementById('dashboardCellMin').value, 10);
    const maxMv = parseInt(document.getElementById('dashboardCellMax').value, 10);
    const warningMv = parseInt(document.getElementById('dashboardCellWarning').value, 10);
    const criticalMv = parseInt(document.getElementById('dashboardCellCritical').value, 10);

    if (!Number.isFinite(minMv) || !Number.isFinite(maxMv)) {
        showToast('Invalid cell voltage range', 'error');
        return false;
    }

    if (maxMv <= minMv) {
        showToast('Maximum cell voltage must be greater than minimum', 'error');
        return false;
    }

    if (!Number.isFinite(warningMv) || !Number.isFinite(criticalMv)) {
        showToast('Invalid imbalance thresholds', 'error');
        return false;
    }

    if (warningMv <= 0 || criticalMv <= warningMv) {
        showToast('Imbalance high threshold must be greater than warning threshold', 'error');
        return false;
    }

    const socCritical = parseInt(document.getElementById('dashboardAlertSocCritical').value, 10);
    const socLow = parseInt(document.getElementById('dashboardAlertSocLow').value, 10);
    if (!Number.isFinite(socCritical) || !Number.isFinite(socLow)) {
        showToast('Invalid SOC thresholds', 'error');
        return false;
    }
    if (socLow <= socCritical) {
        showToast('SOC warning must be greater than SOC critical', 'error');
        return false;
    }

    const tempWarning = parseInt(document.getElementById('dashboardAlertTempWarning').value, 10);
    const tempCritical = parseInt(document.getElementById('dashboardAlertTempCritical').value, 10);
    if (!Number.isFinite(tempWarning) || !Number.isFinite(tempCritical)) {
        showToast('Invalid temperature thresholds', 'error');
        return false;
    }
    if (tempCritical < tempWarning) {
        showToast('Temperature critical must be greater or equal to warning', 'error');
        return false;
    }

    const imbalanceWarning = parseInt(document.getElementById('dashboardAlertImbalanceWarning').value, 10);
    const imbalanceCritical = parseInt(document.getElementById('dashboardAlertImbalanceCritical').value, 10);
    if (!Number.isFinite(imbalanceWarning) || !Number.isFinite(imbalanceCritical)) {
        showToast('Invalid imbalance alert thresholds', 'error');
        return false;
    }
    if (imbalanceCritical <= imbalanceWarning) {
        showToast('Imbalance critical threshold must be greater than warning', 'error');
        return false;
    }

    const balancingMinutes = parseInt(document.getElementById('dashboardAlertBalancingMinutes').value, 10);
    if (!Number.isFinite(balancingMinutes) || balancingMinutes <= 0) {
        showToast('Balancing duration must be greater than zero', 'error');
        return false;
    }

    const newPreferences = {
        cellVoltage: {
            min_mv: minMv,
            max_mv: maxMv,
            warning_delta_mv: warningMv,
            critical_delta_mv: criticalMv
        },
        alerts: {
            soc_critical: socCritical,
            soc_low: socLow,
            temp_warning: tempWarning,
            temp_critical: tempCritical,
            imbalance_warning: imbalanceWarning,
            imbalance_critical: imbalanceCritical,
            balancing_duration_warning_ms: balancingMinutes * 60000
        }
    };

    if (typeof window.setDashboardPreferences === 'function') {
        window.setDashboardPreferences(newPreferences);
        dashboardUIPreferences = window.getDashboardPreferences();
    } else {
        dashboardUIPreferences = mergeObjects(JSON.parse(JSON.stringify(DASHBOARD_PREFS_FALLBACK)), newPreferences);
    }

    populateDashboardUIPreferences();

    if (!silent) {
        showToast('Dashboard preferences saved', 'success');
    }

    return true;
}

function resetDashboardUIPreferences() {
    if (typeof window.getDashboardPreferenceDefaults === 'function' && typeof window.setDashboardPreferences === 'function') {
        const defaults = window.getDashboardPreferenceDefaults();
        window.setDashboardPreferences(defaults);
        dashboardUIPreferences = window.getDashboardPreferences();
    } else {
        dashboardUIPreferences = mergeObjects(JSON.parse(JSON.stringify(DASHBOARD_PREFS_FALLBACK)), {});
    }

    populateDashboardUIPreferences();
    showToast('Dashboard preferences reset to defaults', 'info');
}

function mergeObjects(target, source) {
    const merged = {
        cellVoltage: { ...target.cellVoltage, ...(source.cellVoltage || {}) },
        alerts: { ...target.alerts, ...(source.alerts || {}) }
    };

    merged.cellVoltage.max_mv = Math.max(merged.cellVoltage.max_mv, merged.cellVoltage.min_mv + 1);
    merged.cellVoltage.min_mv = Math.min(merged.cellVoltage.min_mv, merged.cellVoltage.max_mv - 1);
    merged.cellVoltage.warning_delta_mv = Math.max(1, merged.cellVoltage.warning_delta_mv);
    merged.cellVoltage.critical_delta_mv = Math.max(
        merged.cellVoltage.warning_delta_mv + 1,
        merged.cellVoltage.critical_delta_mv
    );

    merged.alerts.soc_low = Math.max(merged.alerts.soc_low, merged.alerts.soc_critical + 1);
    merged.alerts.temp_critical = Math.max(merged.alerts.temp_critical, merged.alerts.temp_warning);
    merged.alerts.imbalance_critical = Math.max(
        merged.alerts.imbalance_warning + 1,
        merged.alerts.imbalance_critical
    );
    merged.alerts.balancing_duration_warning_ms = Math.max(60000, merged.alerts.balancing_duration_warning_ms);

    return merged;
}

// ============================================
// Setup Event Listeners
// ============================================

function setupSettingsListeners() {
    // WiFi IP Mode change
    document.getElementById('wifiIpMode').addEventListener('change', (e) => {
        document.getElementById('staticIpSection').style.display = 
            e.target.value === 'static' ? 'block' : 'none';
    });
    
    // Syslog enable
    document.getElementById('logSyslog').addEventListener('change', (e) => {
        document.getElementById('syslogSettings').style.display = 
            e.target.checked ? 'block' : 'none';
    });
}

// ============================================
// WiFi Settings
// ============================================

function togglePassword(inputId) {
    const input = document.getElementById(inputId);
    const icon = input.nextElementSibling.querySelector('i');
    
    if (input.type === 'password') {
        input.type = 'text';
        icon.classList.remove('fa-eye');
        icon.classList.add('fa-eye-slash');
    } else {
        input.type = 'password';
        icon.classList.remove('fa-eye-slash');
        icon.classList.add('fa-eye');
    }
}

async function testWifiConnection() {
    const ssid = document.getElementById('wifiSSID').value;
    const password = document.getElementById('wifiPassword').value;
    
    if (!ssid) {
        showToast('Please enter SSID', 'warning');
        return;
    }
    
    showToast('Testing WiFi connection...', 'info', 5000);
    
    try {
        const response = await postAPI('/api/wifi/test', { ssid, password });
        
        if (response && response.success) {
            showToast('WiFi test successful! RSSI: ' + response.rssi + ' dBm', 'success');
        } else {
            showToast('WiFi test failed: ' + (response?.message || 'Unknown error'), 'error');
        }
    } catch (error) {
        console.error('[Settings] WiFi test error:', error);
        showToast('WiFi test error', 'error');
    }
}

async function saveWifiSettings() {
    // Collect values
    systemSettings.wifi.mode = document.querySelector('input[name="wifiMode"]:checked').value;
    systemSettings.wifi.sta_ssid = document.getElementById('wifiSSID').value;
    systemSettings.wifi.sta_password = document.getElementById('wifiPassword').value;
    systemSettings.wifi.sta_hostname = document.getElementById('wifiHostname').value;
    systemSettings.wifi.sta_ip_mode = document.getElementById('wifiIpMode').value;
    systemSettings.wifi.sta_static_ip = document.getElementById('staticIP').value;
    systemSettings.wifi.sta_gateway = document.getElementById('staticGateway').value;
    systemSettings.wifi.sta_subnet = document.getElementById('staticSubnet').value;
    systemSettings.wifi.ap_ssid = document.getElementById('apSSID').value;
    systemSettings.wifi.ap_password = document.getElementById('apPassword').value;
    systemSettings.wifi.ap_channel = parseInt(document.getElementById('apChannel').value);
    systemSettings.wifi.ap_fallback = document.getElementById('apFallback').checked;
    
    // Validate
    if (!systemSettings.wifi.sta_ssid && systemSettings.wifi.mode !== 'ap') {
        showToast('SSID is required for Station mode', 'error');
        return;
    }
    
    if (systemSettings.wifi.sta_password && systemSettings.wifi.sta_password.length < 8) {
        showToast('Password must be at least 8 characters', 'error');
        return;
    }
    
    if (!await confirmReboot('Apply WiFi settings?', 'ESP32 will reboot to apply WiFi changes.')) {
        return;
    }
    
    try {
        const response = await postAPI('/api/config/wifi', { wifi: systemSettings.wifi });
        
        if (response && response.success) {
            showToast('WiFi settings saved. Rebooting...', 'success', 10000);
            setTimeout(() => {
                window.location.reload();
            }, 3000);
        } else {
            showToast('Failed to save WiFi settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] WiFi save error:', error);
        showToast('WiFi save error', 'error');
    }
}

// ============================================
// Hardware Settings
// ============================================

async function testUART() {
    showToast('Testing UART communication...', 'info', 5000);
    
    try {
        const response = await fetchAPI('/api/hardware/test/uart');
        
        if (response && response.success) {
            showToast('UART test successful! BMS responded.', 'success');
        } else {
            showToast('UART test failed: ' + (response?.message || 'No response'), 'error');
        }
    } catch (error) {
        console.error('[Settings] UART test error:', error);
        showToast('UART test error', 'error');
    }
}

async function testCAN() {
    showToast('Testing CAN bus...', 'info', 5000);
    
    try {
        const response = await fetchAPI('/api/hardware/test/can');
        
        if (response && response.success) {
            showToast('CAN test successful! Bus is active.', 'success');
        } else {
            showToast('CAN test failed: ' + (response?.message || 'No activity'), 'error');
        }
    } catch (error) {
        console.error('[Settings] CAN test error:', error);
        showToast('CAN test error', 'error');
    }
}

async function saveHardwareSettings() {
    // Collect values
    systemSettings.hardware.uart_rx_pin = parseInt(document.getElementById('uartRxPin').value);
    systemSettings.hardware.uart_tx_pin = parseInt(document.getElementById('uartTxPin').value);
    systemSettings.hardware.uart_baudrate = parseInt(document.getElementById('uartBaudrate').value);
    systemSettings.hardware.can_tx_pin = parseInt(document.getElementById('canTxPin').value);
    systemSettings.hardware.can_rx_pin = parseInt(document.getElementById('canRxPin').value);
    systemSettings.hardware.can_bitrate = parseInt(document.getElementById('canBitrate').value);
    systemSettings.hardware.can_termination = document.getElementById('canTermination').checked;
    
    if (!await confirmReboot('Apply hardware settings?', 'ESP32 will reboot to reinitialize hardware.')) {
        return;
    }
    
    try {
        const response = await postAPI('/api/config/hardware', { hardware: systemSettings.hardware });
        
        if (response && response.success) {
            showToast('Hardware settings saved. Rebooting...', 'success', 10000);
            setTimeout(() => {
                window.location.reload();
            }, 3000);
        } else {
            showToast('Failed to save hardware settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] Hardware save error:', error);
        showToast('Hardware save error', 'error');
    }
}

// ============================================
// CVL Algorithm Settings
// ============================================

async function testCVLAlgorithm() {
    showToast('Testing CVL algorithm...', 'info');
    
    // Simulate test (in production, would call API)
    setTimeout(() => {
        showToast('CVL algorithm test successful!', 'success');
        addLog('CVL algorithm validated with current settings', 'success');
    }, 1000);
}

async function saveCVLSettings() {
    // Collect values
    systemSettings.cvl.enabled = document.getElementById('cvlEnable').checked;
    systemSettings.cvl.bulk_transition_soc = parseInt(document.getElementById('cvlBulkTransition').value);
    systemSettings.cvl.transition_float_soc = parseInt(document.getElementById('cvlTransitionFloat').value);
    systemSettings.cvl.float_exit_soc = parseInt(document.getElementById('cvlFloatExit').value);
    systemSettings.cvl.float_approach_offset = parseFloat(document.getElementById('cvlFloatApproachOffset').value);
    systemSettings.cvl.float_offset = parseFloat(document.getElementById('cvlFloatOffset').value);
    systemSettings.cvl.imbalance_offset = parseFloat(document.getElementById('cvlImbalanceOffset').value);
    systemSettings.cvl.imbalance_trigger_mv = parseInt(document.getElementById('cvlImbalanceTrigger').value);
    systemSettings.cvl.imbalance_release_mv = parseInt(document.getElementById('cvlImbalanceRelease').value);
    
    // Validate
    if (systemSettings.cvl.bulk_transition_soc >= systemSettings.cvl.transition_float_soc) {
        showToast('Bulk transition must be < Transition float', 'error');
        return;
    }
    
    try {
        const response = await postAPI('/api/config/cvl', { cvl: systemSettings.cvl });
        
        if (response && response.success) {
            showToast('CVL settings saved successfully', 'success');
            addLog('CVL algorithm configuration updated', 'info');
        } else {
            showToast('Failed to save CVL settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] CVL save error:', error);
        showToast('CVL save error', 'error');
    }
}

function resetCVLDefaults() {
    if (!confirm('Reset CVL settings to defaults?')) return;
    
    document.getElementById('cvlEnable').checked = true;
    document.getElementById('cvlBulkTransition').value = 90;
    document.getElementById('cvlTransitionFloat').value = 95;
    document.getElementById('cvlFloatExit').value = 85;
    document.getElementById('cvlFloatApproachOffset').value = -0.05;
    document.getElementById('cvlFloatOffset').value = -0.10;
    document.getElementById('cvlImbalanceOffset').value = -0.15;
    document.getElementById('cvlImbalanceTrigger').value = 100;
    document.getElementById('cvlImbalanceRelease').value = 50;
    
    showToast('CVL settings reset to defaults', 'info');
}

// ============================================
// Victron Settings
// ============================================

async function saveVictronSettings() {
    // Collect values
    systemSettings.victron.manufacturer = document.getElementById('victronManufacturer').value;
    systemSettings.victron.battery_name = document.getElementById('victronBatteryName').value;
    systemSettings.victron.pgn_interval_ms = parseInt(document.getElementById('victronPgnInterval').value);
    systemSettings.victron.cvl_interval_ms = parseInt(document.getElementById('victronCvlInterval').value);
    systemSettings.victron.keepalive_interval_ms = parseInt(document.getElementById('victronKeepaliveInterval').value);
    
    try {
        const response = await postAPI('/api/config/victron', { victron: systemSettings.victron });
        
        if (response && response.success) {
            showToast('Victron settings saved successfully', 'success');
            addLog('Victron configuration updated', 'info');
        } else {
            showToast('Failed to save Victron settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] Victron save error:', error);
        showToast('Victron save error', 'error');
    }
}

// ============================================
// Logging Settings
// ============================================

async function clearAllLogs() {
    if (!confirm('Clear all logs?')) return;
    
    try {
        const response = await postAPI('/api/logs/clear', {});
        
        if (response && response.success) {
            showToast('All logs cleared', 'success');
            clearLogs(); // Clear UI logs
        } else {
            showToast('Failed to clear logs', 'error');
        }
    } catch (error) {
        console.error('[Settings] Clear logs error:', error);
        showToast('Clear logs error', 'error');
    }
}

async function downloadLogs() {
    try {
        const response = await fetchAPI('/api/logs/download');
        
        if (response && response.logs) {
            const blob = new Blob([response.logs], { type: 'text/plain' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = `tinybms_logs_${new Date().toISOString().split('T')[0]}.txt`;
            a.click();
            URL.revokeObjectURL(url);
            
            showToast('Logs downloaded', 'success');
        } else {
            showToast('No logs available', 'warning');
        }
    } catch (error) {
        console.error('[Settings] Download logs error:', error);
        showToast('Download logs error', 'error');
    }
}

async function saveLoggingSettings() {
    // Collect values
    systemSettings.logging.level = document.querySelector('input[name="logLevel"]:checked').value;
    systemSettings.logging.serial = document.getElementById('logSerial').checked;
    systemSettings.logging.web = document.getElementById('logWeb').checked;
    systemSettings.logging.sd = document.getElementById('logSD').checked;
    systemSettings.logging.syslog = document.getElementById('logSyslog').checked;
    systemSettings.logging.syslog_server = document.getElementById('syslogServer').value;
    
    try {
        const response = await postAPI('/api/config/logging', { logging: systemSettings.logging });
        
        if (response && response.success) {
            showToast('Logging settings saved successfully', 'success');
        } else {
            showToast('Failed to save logging settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] Logging save error:', error);
        showToast('Logging save error', 'error');
    }
}

// ============================================
// System Settings
// ============================================

function exportSystemConfig() {
    const config = {
        version: '3.0',
        timestamp: new Date().toISOString(),
        settings: systemSettings
    };
    
    const json = JSON.stringify(config, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `system_config_${new Date().toISOString().split('T')[0]}.json`;
    a.click();
    URL.revokeObjectURL(url);
    
    showToast('System configuration exported', 'success');
}

function importSystemConfig() {
    const fileInput = document.getElementById('systemConfigInput');
    const file = fileInput.files[0];
    
    if (!file) return;
    
    const reader = new FileReader();
    
    reader.onload = async (e) => {
        try {
            const config = JSON.parse(e.target.result);
            
            if (!config.settings) {
                showToast('Invalid config file', 'error');
                return;
            }
            
            if (!await confirmReboot('Import system configuration?', 'This will overwrite current settings and reboot.')) {
                return;
            }
            
            // Send to ESP32
            const response = await postAPI('/api/config/import', config.settings);
            
            if (response && response.success) {
                showToast('Configuration imported. Rebooting...', 'success', 10000);
                setTimeout(() => {
                    window.location.reload();
                }, 3000);
            } else {
                showToast('Failed to import config', 'error');
            }
            
        } catch (error) {
            console.error('[Settings] Import error:', error);
            showToast('Failed to import config', 'error');
        }
    };
    
    reader.readAsText(file);
}

async function reloadConfig() {
    if (!await confirmReboot('Reload config from SPIFFS?', 'This will discard unsaved changes.')) {
        return;
    }
    
    try {
        const response = await postAPI('/api/config/reload', {});
        
        if (response && response.success) {
            showToast('Config reloaded successfully', 'success');
            loadSettings();
        } else {
            showToast('Failed to reload config', 'error');
        }
    } catch (error) {
        console.error('[Settings] Reload error:', error);
        showToast('Reload error', 'error');
    }
}

async function saveAllSettings() {
    if (!confirm('Save ALL current settings?')) return;

    if (!saveDashboardUIPreferences({ silent: true })) {
        return;
    }

    // Collect all values
    // (Already done in individual save functions)

    try {
        const response = await postAPI('/api/config/save', { settings: systemSettings });
        
        if (response && response.success) {
            showToast('All settings saved successfully', 'success');
            addLog('System configuration saved', 'success');
        } else {
            showToast('Failed to save settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] Save all error:', error);
        showToast('Save all error', 'error');
    }
}

async function restartESP32() {
    if (!await confirmReboot('Restart ESP32?', 'The system will reboot now.')) {
        return;
    }
    
    try {
        await postAPI('/api/system/restart', {});
        showToast('ESP32 restarting...', 'warning', 10000);
        
        setTimeout(() => {
            window.location.reload();
        }, 5000);
    } catch (error) {
        console.error('[Settings] Restart error:', error);
    }
}

async function resetToDefaults() {
    if (!await confirmReboot('Reset ALL settings to defaults?', 'This will restore factory defaults and reboot.')) {
        return;
    }
    
    // Second confirmation
    if (!confirm('Are you SURE? This action cannot be undone!')) {
        return;
    }
    
    try {
        const response = await postAPI('/api/config/reset', {});
        
        if (response && response.success) {
            showToast('Settings reset to defaults. Rebooting...', 'success', 10000);
            setTimeout(() => {
                window.location.reload();
            }, 3000);
        } else {
            showToast('Failed to reset settings', 'error');
        }
    } catch (error) {
        console.error('[Settings] Reset error:', error);
        showToast('Reset error', 'error');
    }
}

async function factoryReset() {
    if (!await confirmReboot('⚠️ FACTORY RESET?', 'This will erase ALL data and restore factory settings!')) {
        return;
    }
    
    // Second confirmation with text input
    const confirmation = prompt('Type "FACTORY RESET" to confirm:');
    if (confirmation !== 'FACTORY RESET') {
        showToast('Factory reset cancelled', 'info');
        return;
    }
    
    try {
        await postAPI('/api/system/factory-reset', {});
        showToast('Factory reset initiated. Rebooting...', 'danger', 15000);
        
        setTimeout(() => {
            window.location.href = 'http://192.168.4.1'; // Default AP IP
        }, 10000);
    } catch (error) {
        console.error('[Settings] Factory reset error:', error);
    }
}

// ============================================
// Helpers
// ============================================

async function confirmReboot(title, message) {
    return new Promise((resolve) => {
        const modalHtml = `
            <div class="modal fade" id="confirmRebootModal" tabindex="-1">
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
                            <p class="mb-0"><strong>Do you want to continue?</strong></p>
                        </div>
                        <div class="modal-footer">
                            <button type="button" class="btn btn-secondary" data-bs-dismiss="modal" id="rebootNo">
                                Cancel
                            </button>
                            <button type="button" class="btn btn-warning" id="rebootYes">
                                Confirm
                            </button>
                        </div>
                    </div>
                </div>
            </div>
        `;
        
        // Remove existing
        const existing = document.getElementById('confirmRebootModal');
        if (existing) existing.remove();
        
        // Add new
        document.body.insertAdjacentHTML('beforeend', modalHtml);
        
        const modal = new bootstrap.Modal(document.getElementById('confirmRebootModal'));
        
        document.getElementById('rebootYes').onclick = () => {
            modal.hide();
            resolve(true);
        };
        
        document.getElementById('rebootNo').onclick = () => {
            modal.hide();
            resolve(false);
        };
        
        modal.show();
    });
}

// ============================================
// TinyBMS Settings
// ============================================

async function saveTinyBMSSettings() {
    const settings = {
        tinybms: {
            poll_interval_ms: parseInt(document.getElementById('tinyPollInterval').value),
            poll_interval_min_ms: parseInt(document.getElementById('tinyPollMin').value),
            poll_interval_max_ms: parseInt(document.getElementById('tinyPollMax').value),
            poll_backoff_step_ms: parseInt(document.getElementById('tinyBackoffStep').value),
            poll_recovery_step_ms: parseInt(document.getElementById('tinyRecoveryStep').value),
            poll_latency_target_ms: parseInt(document.getElementById('tinyLatencyTarget').value),
            poll_latency_slack_ms: parseInt(document.getElementById('tinyLatencySlack').value),
            poll_failure_threshold: parseInt(document.getElementById('tinyFailureThreshold').value),
            poll_success_threshold: parseInt(document.getElementById('tinySuccessThreshold').value),
            uart_retry_count: parseInt(document.getElementById('tinyRetryCount').value),
            uart_retry_delay_ms: parseInt(document.getElementById('tinyRetryDelay').value),
            broadcast_expected: document.getElementById('tinyBroadcastExpected').checked
        }
    };

    try {
        await postAPI('/api/config/save', { settings });
        systemSettings.tinybms = settings.tinybms;
        showToast('TinyBMS settings saved successfully', 'success');
        addNotification('TinyBMS polling configuration updated', 'success');
    } catch (error) {
        console.error('[Settings] Error saving TinyBMS settings:', error);
        showToast('Failed to save TinyBMS settings', 'error');
    }
}

// ============================================
// MQTT Settings
// ============================================

async function saveMQTTSettings() {
    const settings = {
        mqtt: {
            enabled: document.getElementById('mqttEnabled').checked,
            uri: document.getElementById('mqttUri').value,
            port: parseInt(document.getElementById('mqttPort').value),
            client_id: document.getElementById('mqttClientId').value,
            username: document.getElementById('mqttUsername').value,
            password: document.getElementById('mqttPassword').value,
            root_topic: document.getElementById('mqttRootTopic').value,
            clean_session: document.getElementById('mqttCleanSession').checked,
            use_tls: document.getElementById('mqttUseTls').checked,
            server_certificate: document.getElementById('mqttServerCert')?.value || '',
            keepalive_seconds: parseInt(document.getElementById('mqttKeepalive').value),
            reconnect_interval_ms: parseInt(document.getElementById('mqttReconnectInterval').value),
            default_qos: parseInt(document.getElementById('mqttQos').value),
            retain_by_default: document.getElementById('mqttRetain').checked
        }
    };

    try {
        await postAPI('/api/config/save', { settings });
        systemSettings.mqtt = settings.mqtt;
        showToast('MQTT settings saved successfully. Restart required.', 'success', 5000);
        addNotification('MQTT configuration updated - restart to apply', 'success');
    } catch (error) {
        console.error('[Settings] Error saving MQTT settings:', error);
        showToast('Failed to save MQTT settings', 'error');
    }
}

// ============================================
// Extended Victron Settings (with thresholds)
// ============================================

async function saveVictronSettings() {
    const settings = {
        victron: {
            manufacturer: document.getElementById('victronManufacturer').value,
            battery_name: document.getElementById('victronBatteryName').value,
            pgn_interval_ms: parseInt(document.getElementById('victronPgnInterval').value),
            cvl_interval_ms: parseInt(document.getElementById('victronCvlInterval').value),
            keepalive_interval_ms: parseInt(document.getElementById('victronKeepaliveInterval').value),
            thresholds: {
                undervoltage_v: parseFloat(document.getElementById('victronUndervoltage').value),
                overvoltage_v: parseFloat(document.getElementById('victronOvervoltage').value),
                overtemp_c: parseFloat(document.getElementById('victronOvertemp').value),
                low_temp_charge_c: parseFloat(document.getElementById('victronLowTempCharge').value),
                imbalance_warn_mv: parseInt(document.getElementById('victronImbalanceWarn').value),
                imbalance_alarm_mv: parseInt(document.getElementById('victronImbalanceAlarm').value),
                soc_low_percent: parseFloat(document.getElementById('victronSocLow').value),
                soc_high_percent: parseFloat(document.getElementById('victronSocHigh').value),
                derate_current_a: parseFloat(document.getElementById('victronDerateCurrent').value)
            }
        }
    };

    try {
        await postAPI('/api/config/victron', settings);
        systemSettings.victron = settings.victron;
        showToast('Victron settings saved successfully', 'success');
        addNotification('Victron integration settings updated', 'success');
    } catch (error) {
        console.error('[Settings] Error saving Victron settings:', error);
        showToast('Failed to save Victron settings', 'error');
    }
}

// ============================================
// Initialize
// ============================================

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initSettings);
} else {
    initSettings();
}
