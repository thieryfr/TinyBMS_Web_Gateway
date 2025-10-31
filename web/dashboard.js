(async function () {
    const statusEl = document.getElementById('status-content');
    statusEl.textContent = 'Loading data...';

    try {
        // Placeholder for WebSocket or REST polling
        const response = await fetch('/api/status');
        if (!response.ok) {
            throw new Error('Failed to fetch status');
        }
        const data = await response.json();
        statusEl.textContent = JSON.stringify(data, null, 2);
    } catch (error) {
        statusEl.textContent = error.message;
    }
})();
