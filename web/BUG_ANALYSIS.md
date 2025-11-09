# TinyBMS-GW Web Interface JavaScript Code Analysis

## Summary
Comprehensive analysis of JavaScript code across the web interface for bugs and issues. Analysis includes error handling, memory leaks, race conditions, null/undefined issues, and logic errors.

---

## 1. ERROR HANDLING ISSUES

### 1.1 Unhandled Promise Rejections

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Lines:** 538, 542, 549

```javascript
// Line 538 - Missing error handler
refresh.addEventListener('click', () => fetchMqttStatus().catch((err) => updateMqttStatus(null, err)));
// Issue: Catch is present but error is only logged/shown, fetch errors might occur before handler

// Line 542 - Empty catch handler
else { fetchMqttStatus().catch(() => {}); startMqttStatusPolling(); }
// Issue: Silently ignoring errors can mask problems

// Line 549 - Similar pattern
fetchMqttStatus().catch((err) => updateMqttStatus(null, err));
```

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Line:** 1687-1702

```javascript
async function fetchStatus() {
    try {
        const res = await fetch('/api/status');
        const data = await res.json();  // No error check on res.ok
        if (data.battery) { ... }
        return data;
    } catch (error) {
        console.error('[API] fetchStatus error:', error);
        throw error;
    }
}
```
**Issue:** Missing `if (!res.ok)` check before calling `res.json()`. Will crash if HTTP status is error.

**Severity:** HIGH

---

### 1.2 Missing Error Checks on Fetch Responses

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Lines:** 1707, 1722, 1739, 1751

```javascript
// Lines 1707-1708
async function fetchLiveHistory(limit = 120) {
    try {
        const res = await fetch(`/api/history?limit=${limit}`);
        const data = await res.json();  // No res.ok check
        state.liveHistory = data.entries || [];

// Lines 1722-1723
async function fetchRegisters() {
    try {
        const res = await fetch('/api/registers');
        const data = await res.json();  // No res.ok check

// Lines 1739-1740
async function fetchConfig() {
    try {
        const res = await fetch('/api/config');
        const data = await res.json();  // No res.ok check

// Lines 1751-1752
async function fetchHistoryArchives() {
    try {
        const res = await fetch('/api/history/files');
        const data = await res.json();  // No res.ok check
```

**Issue:** All fetch calls lack `if (!res.ok)` validation before calling `res.json()`. Will silently fail if server returns error status.

**Severity:** HIGH

---

### 1.3 Missing Error Handler in Promise Chain

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Line:** 1611

```javascript
fetchHistoryArchives().finally(updateArchiveControls);
```

**Issue:** No catch handler. If `fetchHistoryArchives()` rejects, the promise rejection is unhandled. `finally` is called regardless, but error is not caught.

**Severity:** MEDIUM

---

### 1.4 WebSocket Error Handler Insufficient

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Lines:** 1818-1825

```javascript
ws.onerror = (error) => {
    console.error(`[WebSocket ${path}] Error:`, error);
};

ws.onclose = () => {
    console.log(`[WebSocket ${path}] Disconnected. Reconnecting in 3s...`);
    setTimeout(() => connectWebSocket(path, onMessage), 3000);  // No exponential backoff
};
```

**Issues:**
- No maximum reconnection attempts - will retry indefinitely causing resource exhaustion
- No exponential backoff - hammers the server
- `ws` reference is not returned from outer scope in all cases, reconnection may fail silently

**Severity:** MEDIUM

---

## 2. MEMORY LEAK ISSUES

### 2.1 WebSocket Connections Not Properly Cleaned Up

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Lines:** 1797-1828

```javascript
function connectWebSocket(path, onMessage) {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const url = `${protocol}//${window.location.host}${path}`;
    
    const ws = new WebSocket(url);
    
    ws.onopen = () => { ... };
    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            onMessage(data);
        } catch (error) { ... }
    };
    
    ws.onerror = (error) => { ... };
    ws.onclose = () => {
        console.log(`[WebSocket ${path}] Disconnected. Reconnecting in 3s...`);
        setTimeout(() => connectWebSocket(path, onMessage), 3000);  // Creates new ws instance
    };
    
    return ws;
}
```

**Issues:**
- WebSocket instances are created at lines 1616-1619 but never stored for cleanup
- On reconnection (line 1824), a new WebSocket is created but the old one is never closed
- No tracking of WebSocket instances - impossible to clean up on page unload
- Called 4 times (lines 1616-1619), each recursive reconnection creates more instances

**Severity:** HIGH - Can cause many zombie connections

---

### 2.2 Event Listeners Not Removed

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Line:** 1200-1207

```javascript
document.addEventListener('visibilitychange', () => {
    if (document.hidden) {
        stopCanStatusPolling();
    } else {
        fetchCanStatus().catch(() => {});
        startCanStatusPolling();
    }
});
```

**Issue:** Event listener attached in `setupCanDashboard()` is never removed. Similar listeners are added for MQTT and UART tabs. Multiple listeners will accumulate if tabs are revisited.

**Severity:** MEDIUM

---

### 2.3 setInterval Not Tracked for Cleanup

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Lines:** 1146-1159

```javascript
function startUartStatusPolling() {
    if (state.uartDashboard.statusInterval) return;
    state.uartDashboard.statusInterval = setInterval(() => {
        fetchUartStatus().catch((err) => {
            console.warn('UART status polling error', err);
        });
    }, UART_STATUS_POLL_INTERVAL_MS);
}

function stopUartStatusPolling() {
    if (state.uartDashboard.statusInterval) {
        clearInterval(state.uartDashboard.statusInterval);
        state.uartDashboard.statusInterval = null;
    }
}
```

**Issue:** While `stopUartStatusPolling()` exists, there's no cleanup on page unload. Same issue for MQTT polling (lines 546-558) and CAN polling (lines 1520-1534). If user navigates away, polling continues.

**Severity:** MEDIUM

---

### 2.4 Chart Instances Not Disposed

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Lines:** 1566-1594

```javascript
state.historyChart = new HistoryChart(document.getElementById('history-chart'));
state.mqtt.messageChart = new MqttMessageChart(document.getElementById('mqtt-messages-chart'));
state.batteryCharts = new BatteryRealtimeCharts({ ... });
state.energyCharts = new EnergyCharts({ ... });
state.uartRealtime.charts = new UartCharts({ ... });
state.canRealtime.charts = new CanCharts({ ... });
```

**Issues:**
- Chart instances stored in `state` global object
- No disposal or cleanup on page unload
- ECharts instances can hold significant memory
- `initChart()` function in base.js (lines 80-111) creates ResizeObserver and event listeners that are never cleaned up globally

**Severity:** MEDIUM

---

## 3. RACE CONDITION ISSUES

### 3.1 Concurrent Polling Requests

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Lines:** 1546-1551 (MQTT), 1148-1152 (UART), 1520-1526 (CAN)

```javascript
function startMqttStatusPolling() {
    if (state.mqtt.statusInterval) return;
    state.mqtt.statusInterval = setInterval(() => {
        fetchMqttStatus().catch((err) => updateMqttStatus(null, err));
    }, MQTT_STATUS_POLL_INTERVAL_MS);  // 5000ms
}
```

**Issue:** Multiple concurrent fetch requests can be in flight:
- A fetch request takes > 5 seconds
- The interval fires again before first response arrives
- Second request is sent while first is still pending
- Responses may arrive out of order, causing state inconsistency

**Example scenario:**
```
T=0ms    : First fetch() starts
T=5000ms : Second fetch() starts (first still pending)
T=7000ms : Second response arrives → state updated with newer data
T=8000ms : First response arrives → state updated with older data
Result   : UI shows stale data
```

**Severity:** MEDIUM

---

### 3.2 State Mutation Without Synchronization

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Lines:** 618-697 (updateMqttDashboardKPIs)

```javascript
function updateMqttDashboardKPIs(status) {
    if (!status) return;

    const now = Date.now();
    const timeDelta = state.mqttDashboard.lastUpdate ? (now - state.mqttDashboard.lastUpdate) / 1000 : 1;
    state.mqttDashboard.lastUpdate = now;  // Line 623

    // Extract current stats
    const currentPub = status.published_messages || status.message_counts?.published || 0;
    const currentSub = status.received_messages || status.message_counts?.received || 0;
    // ...
    
    // Calculate rates
    const pubRate = Math.max(0, (currentPub - state.mqttDashboard.previousStats.published) / timeDelta);
    // ...
    
    // Update previous stats
    state.mqttDashboard.previousStats = {  // Line 636
        published: currentPub,
        received: currentSub,
        bytesIn: currentBytesIn,
        bytesOut: currentBytesOut,
    };
```

**Issue:** If two calls to `updateMqttDashboardKPIs()` happen quickly:
1. First call: `lastUpdate` = T0, calculates rate based on `previousStats`
2. Second call: `lastUpdate` = T1, but if they're too close, `timeDelta` is very small → huge rate spikes

No debouncing or request deduplication.

**Severity:** LOW - Visual glitch, not critical

---

### 3.3 Multiple WebSocket Message Handlers

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Lines:** 1616-1619

```javascript
connectWebSocket('/ws/telemetry', handleTelemetryMessage);
connectWebSocket('/ws/events', handleEventMessage);
connectWebSocket('/ws/uart', handleUartMessage);
connectWebSocket('/ws/can', handleCanMessage);
```

**Issue:** If user navigates or page reloads:
- `onclose` handler (line 1824) will call `connectWebSocket()` again
- This creates duplicate listeners for the same events
- Each reconnection adds another listener without removing the old one
- Eventually thousands of handlers calling the same function

**Severity:** HIGH - Exponential memory growth and duplicate processing

---

## 4. NULL/UNDEFINED ISSUES

### 4.1 Missing Null Checks Before DOM Access

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Line:** 1029

```javascript
const lastSeen = Number(data.lastSeen) ? new Date(data.lastSeen).toLocaleTimeString() : '--';
```

**Issue:** `data.lastSeen` could be 0 (falsy but valid), causing wrong formatting. Should use `Number.isFinite()` or check explicitly.

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Line:** 1077

```javascript
const time = new Date(event.timestamp).toLocaleTimeString();
```

**Issue:** `event.timestamp` could be invalid. No check if `new Date()` is valid (check `isNaN(date.getTime())`).

---

### 4.2 Optional Chaining Not Used Consistently

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Lines:** 1235-1236

```javascript
const prevBusState = previous?.bus?.state ?? previous?.bus?.state_label;
const currentBusState = status.bus?.state ?? status.bus?.state_label;
```

**Good:** Uses optional chaining

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Line:** 794

```javascript
const broker = status.host ? `${status.scheme || 'mqtt'}://${status.host}:${status.port || 1883}` : '--';
```

**Issue:** Should use optional chaining: `status.scheme ?? 'mqtt'` instead of `status.scheme || 'mqtt'` because 0 and '' are falsy.

---

### 4.3 Missing Validation of Array Access

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Lines:** 2145-2146

```javascript
const minIndex = voltages.indexOf(min);
const maxIndex = voltages.indexOf(max);
```

**Issue:** `indexOf()` returns -1 if not found, but this is used directly in:

```javascript
// Line 2161-2162
set('cell-stat-max', minIndex >= 0 ? `C${maxIndex + 1} (${max} mV)` : '-- (-- mV)');
set('cell-stat-min', maxIndex >= 0 ? `C${minIndex + 1} (${min} mV)` : '-- (-- mV)');
```

**BUG:** Variables are swapped! Should be:
```javascript
set('cell-stat-max', maxIndex >= 0 ? `C${maxIndex + 1} (${max} mV)` : '-- (-- mV)');  // maxIndex for max
set('cell-stat-min', minIndex >= 0 ? `C${minIndex + 1} (${min} mV)` : '-- (-- mV)');  // minIndex for min
```

**Severity:** MEDIUM - Shows wrong cell indices

---

## 5. LOGIC ERRORS

### 5.1 Off-by-One Error in Cell Voltage Display

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Lines:** 2160-2162

```javascript
// Find indices of min and max
const minIndex = voltages.indexOf(min);
const maxIndex = voltages.indexOf(max);

// ... later ...

set('cell-stat-max', minIndex >= 0 ? `C${maxIndex + 1} (${max} mV)` : '-- (-- mV)');
set('cell-stat-min', maxIndex >= 0 ? `C${minIndex + 1} (${min} mV)` : '-- (-- mV)');
```

**Issues:**
1. Variable names are swapped (minIndex used for max, maxIndex used for min)
2. The actual indices are correct (+1 is right), but variable names are backwards

**Severity:** MEDIUM

---

### 5.2 Incorrect State Update in Dashboard

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Lines:** 1257-1268

```javascript
const prev = state.canDashboard.previousStats || { txFrames: 0, rxFrames: 0, txBytes: 0, rxBytes: 0, initialized: false };
const firstUpdate = !prev.initialized;

const txRate = firstUpdate ? 0 : Math.max(0, (txCount - prev.txFrames) / timeDeltaSeconds);
const rxRate = firstUpdate ? 0 : Math.max(0, (rxCount - prev.rxFrames) / timeDeltaSeconds);

state.canDashboard.previousStats = {
    txFrames: txCount,
    rxFrames: rxCount,
    txBytes,
    rxBytes,
    initialized: true,
};
```

**Issue:** `previousStats` is always updated, even if fetch failed. If fetch fails on update 2, update 3 will calculate incorrect deltas from failed update 2.

**Severity:** LOW

---

### 5.3 Timestamp Coercion Issue

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Line:** 1483

```javascript
const date = new Date(event.timestamp);
const time = Number.isNaN(date.getTime()) ? '--' : date.toLocaleTimeString();
```

**Good:** Has validation

**But Line:** 1077 (UART)

```javascript
const time = new Date(event.timestamp).toLocaleTimeString();
```

**Issue:** No validation. Should check `isNaN(date.getTime())` first.

**Severity:** LOW

---

### 5.4 Array Spread Without Size Check

**File:** `/home/user/TinyBMS-GW/web/dashboard.js`
**Line:** 2149

```javascript
const inBalances = voltages.map(v => v > 0 ? Math.abs(v - avg) : 0);
const maxInBalance = Math.max(...inBalances);
```

**Issue:** If `inBalances` is empty array, `Math.max()` returns `-Infinity`. Should check array length first.

**Severity:** LOW

---

## 6. ADDITIONAL ISSUES IN OTHER FILES

### 6.1 CodeMetricsDashboard.js - Missing Error Handling

**File:** `/home/user/TinyBMS-GW/web/src/js/codeMetricsDashboard.js`
**Lines:** 267-272

```javascript
const [runtime, eventBus, tasks, modules] = await Promise.all([
    fetchFromCandidates(ENDPOINTS.runtime),
    fetchFromCandidates(ENDPOINTS.eventBus),
    fetchFromCandidates(ENDPOINTS.tasks),
    fetchFromCandidates(ENDPOINTS.modules),
]);
```

**Issue:** `Promise.all()` will reject if any promise rejects. No error handling. If one endpoint fails, entire dashboard update fails.

**Better approach:** Use `Promise.allSettled()` to continue even if some fail.

**Severity:** MEDIUM

---

### 6.2 mqtt-config.js - Uncaught Promise

**File:** `/home/user/TinyBMS-GW/web/src/js/mqtt-config.js`
**Line:** 910

```javascript
Promise.all([configPromise, fetchMqttStatus(), fetchRuntimeSummary()]).catch(() => {});
```

**Issue:** Silently swallows all errors. Issues won't be logged.

**Severity:** MEDIUM

---

### 6.3 base.js - ResizeObserver Leak Potential

**File:** `/home/user/TinyBMS-GW/web/src/js/charts/base.js`
**Lines:** 80-111

```javascript
export function initChart(domElement, options = {}, { theme = DEFAULT_THEME_NAME, renderer = 'canvas' } = {}) {
    // ...
    const resizeObserver = typeof ResizeObserver !== 'undefined' ? new ResizeObserver(() => chart.resize()) : null;
    const handleWindowResize = () => chart.resize();

    window.addEventListener('resize', handleWindowResize);
    if (resizeObserver) {
        resizeObserver.observe(domElement);
    }

    const dispose = () => {
        window.removeEventListener('resize', handleWindowResize);
        if (resizeObserver) {
            resizeObserver.disconnect();
        }
        if (!chart.isDisposed()) {
            chart.dispose();
        }
    };

    return { chart, dispose };
}
```

**Issues:**
1. `dispose()` function returned but never called in dashboard.js
2. Window resize listener added for EVERY chart but only removed if dispose() is called
3. Multiple charts create multiple resize listeners on same window

**Severity:** MEDIUM

---

### 6.4 config-registers.js - Missing Error Handling

**File:** `/home/user/TinyBMS-GW/web/src/components/configuration/config-registers.js`
**Lines:** 34-55

```javascript
async loadRegisters() {
    try {
        const response = await fetch('/api/registers');
        if (!response.ok) {
            throw new Error(`Failed to load registers: ${response.statusText}`);
        }

        const data = await response.json();
        this.registers = data.registers || [];
        // ...
        console.log(`Loaded ${this.registers.length} TinyBMS configuration registers`);
    } catch (error) {
        console.error('Error loading registers:', error);
        this.showError('Impossible de charger les registres TinyBMS');
    }
}
```

**Issue:** `showError()` method is called but not defined in class. Will throw `TypeError`.

**Severity:** HIGH

---

## 7. SUMMARY OF CRITICAL ISSUES

| Priority | Issue | File | Lines | Impact |
|----------|-------|------|-------|--------|
| CRITICAL | Unhandled fetch rejections | dashboard.js | 1687-1702, 1707, 1722, 1739, 1751 | App crashes on network errors |
| CRITICAL | WebSocket zombie connections | dashboard.js | 1616-1619, 1824 | Memory leak, duplicate processing |
| CRITICAL | Undefined method `showError()` | config-registers.js | 54 | App crashes |
| HIGH | No maximum WebSocket retries | dashboard.js | 1824 | Resource exhaustion |
| HIGH | Race condition in concurrent polls | dashboard.js | 1546-1551 | Stale data in UI |
| HIGH | Event listeners not removed | dashboard.js | 1200-1207 | Memory accumulation |
| MEDIUM | Promise.all() without error handling | codeMetricsDashboard.js | 267-272 | Dashboard fails entirely |
| MEDIUM | Cell voltage index bug | dashboard.js | 2161-2162 | Wrong cell numbers displayed |
| MEDIUM | No ResizeObserver cleanup | charts/base.js | 80-111 | Memory leak |

---

## 8. RECOMMENDED FIXES

### 8.1 Add Response Status Checks

```javascript
async function fetchStatus() {
    try {
        const res = await fetch('/api/status');
        if (!res.ok) {
            throw new Error(`HTTP ${res.status}: ${res.statusText}`);
        }
        const data = await res.json();
        // ...
    } catch (error) {
        console.error('[API] fetchStatus error:', error);
        throw error;
    }
}
```

### 8.2 Implement WebSocket Pool Management

```javascript
const websockets = new Map();

function connectWebSocket(path, onMessage) {
    if (websockets.has(path)) {
        return websockets.get(path);
    }
    
    const ws = new WebSocket(url);
    ws.retryCount = 0;
    ws.maxRetries = 5;
    
    // ... setup handlers ...
    
    websockets.set(path, ws);
    return ws;
}

window.addEventListener('beforeunload', () => {
    websockets.forEach(ws => ws.close());
    websockets.clear();
});
```

### 8.3 Add Debouncing to Polling

```javascript
function startMqttStatusPolling() {
    if (state.mqtt.statusInterval) return;
    let isFetching = false;
    
    state.mqtt.statusInterval = setInterval(() => {
        if (isFetching) return;
        isFetching = true;
        
        fetchMqttStatus()
            .catch((err) => updateMqttStatus(null, err))
            .finally(() => { isFetching = false; });
    }, MQTT_STATUS_POLL_INTERVAL_MS);
}
```

### 8.4 Add Chart Cleanup

```javascript
window.addEventListener('beforeunload', () => {
    // Dispose all charts
    if (state.historyChart?.chart?.dispose) state.historyChart.chart.dispose();
    if (state.mqtt.messageChart?.chart?.dispose) state.mqtt.messageChart.chart.dispose();
    // ... etc
    
    // Clear polling
    stopMqttStatusPolling();
    stopUartStatusPolling();
    stopCanStatusPolling();
});
```

