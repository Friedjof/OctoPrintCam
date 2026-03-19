// API Configuration
const API_BASE = '/api/v1';

// State
let connectionStatus = 'disconnected';

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    initializeUI();
    checkConnection();
    startPeriodicUpdates();
});

// UI Initialization
function initializeUI() {
    const actionBtn = document.getElementById('action-btn');
    const refreshBtn = document.getElementById('refresh-btn');

    actionBtn.addEventListener('click', handleAction);
    refreshBtn.addEventListener('click', refreshData);
}

// Connection check
async function checkConnection() {
    try {
        const response = await fetch(`${API_BASE}/status`);
        if (response.ok) {
            const data = await response.json();
            updateConnectionStatus('connected');
            updateStatus(data);
        } else {
            updateConnectionStatus('error');
        }
    } catch (error) {
        console.error('Connection error:', error);
        updateConnectionStatus('disconnected');
    }
}

// Update connection status
function updateConnectionStatus(status) {
    connectionStatus = status;
    const statusElement = document.getElementById('connection-status');

    const statusMap = {
        'connected': { text: 'Connected', color: '#4CAF50' },
        'disconnected': { text: 'Disconnected', color: '#F44336' },
        'error': { text: 'Error', color: '#FF9800' }
    };

    const statusInfo = statusMap[status] || statusMap.disconnected;
    statusElement.textContent = statusInfo.text;
    statusElement.style.color = statusInfo.color;
}

// Update status display
function updateStatus(data) {
    if (data.uptime !== undefined) {
        document.getElementById('uptime').textContent = formatUptime(data.uptime);
    }
}

// Format uptime (seconds to human readable)
function formatUptime(seconds) {
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);

    if (days > 0) {
        return `${days}d ${hours}h ${minutes}m`;
    } else if (hours > 0) {
        return `${hours}h ${minutes}m`;
    } else {
        return `${minutes}m`;
    }
}

// Handle action button
async function handleAction() {
    try {
        const response = await fetch(`${API_BASE}/action`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ action: 'test' })
        });

        if (response.ok) {
            console.log('Action executed successfully');
        } else {
            console.error('Action failed');
        }
    } catch (error) {
        console.error('Action error:', error);
    }
}

// Refresh data
async function refreshData() {
    await checkConnection();
}

// Periodic updates
function startPeriodicUpdates() {
    // Update status every 5 seconds
    setInterval(() => {
        if (connectionStatus === 'connected') {
            checkConnection();
        }
    }, 5000);
}
