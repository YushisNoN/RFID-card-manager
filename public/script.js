
const API_BASE = '';

let currentStats = null;
let logEntries = [];

function formatDate(timestamp) {
    const date = new Date(timestamp);

    if (Number.isNaN(date.getTime())) {
        return '—';
    }

    return new Intl.DateTimeFormat('ru-RU', {
        day: '2-digit',
        month: '2-digit',
        year: 'numeric',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit'
    }).format(date);
}

async function apiGet(endpoint) {
    const response = await fetch(`${API_BASE}${endpoint}`, {
        method: 'GET',
        cache: 'no-store'
    });

    const data = await response.json();

    if (!response.ok) {
        throw new Error(data.error || `HTTP ${response.status}`);
    }

    return data;
}

function getEventLabel(type) {
    switch (type) {
        case 'board_taken':
            return 'взял плату';

        case 'board_returned':
            return 'сдал плату';

        default:
            return type;
    }
}
function getResultLabel(type) {
    switch (type) {
        case 'board_taken':
            return 'плата на руках';

        case 'board_returned':
            return 'плата возвращена';

        default:
            return 'неизвестно';
    }
}

function addCell(row, value, className = '') {
    const cell = document.createElement('td');

    // Используем textContent, чтобы данные из БД
    // не интерпретировались браузером как HTML.
    cell.textContent = value;

    if (className) {
        cell.className = className;
    }

    row.appendChild(cell);
}

function renderLogs() {
    const tbody = document.getElementById('log-body');
    tbody.innerHTML = '';

    if (logEntries.length === 0) {
        const tr = document.createElement('tr');
        const td = document.createElement('td');

        td.colSpan = 5;
        td.textContent = 'событий пока нет';
        td.style.textAlign = 'center';

        tr.appendChild(td);
        tbody.appendChild(tr);

        return;
    }

    logEntries.forEach(entry => {
        const tr = document.createElement('tr');

        const isIssued = entry.event_type === 'board_taken';

        addCell(tr, String(entry.id));
        addCell(tr, entry.uid);

        addCell(
            tr,
            getEventLabel(entry.event_type),
            isIssued ? 'action-issued' : 'action-repeat'
        );

        addCell(tr, formatDate(entry.timestamp));

        addCell(
            tr,
            getResultLabel(entry.event_type),
            isIssued ? 'action-issued' : 'action-repeat'
        );

        tbody.appendChild(tr);
    });
}

function renderStock() {
    if (!currentStats) {
        return;
    }

    const available = Number(currentStats.available_cards);
	const total = Number(currentStats.total_cards);

	const pct = total > 0
		? (available / total) * 100
		: 0;

	document.getElementById('stock-fill').style.width = pct + '%';
	document.getElementById('stock-text').textContent =
		`${available}/${total}`;
}

function renderStats() {
    if (!currentStats) {
        return;
    }

	document.getElementById('stat-takes').textContent =
		currentStats.total_takes;

	document.getElementById('stat-returns').textContent =
		currentStats.total_returns;

	const avg = Number(currentStats.avg_duration_ms);

	document.getElementById('stat-avg').textContent =
		avg > 0 ? formatDuration(avg) : '—';

    const list = document.getElementById('stat-detail-list');
    list.innerHTML = '';

    if (logEntries.length === 0) {
        const li = document.createElement('li');
        li.textContent = 'нет событий';
        list.appendChild(li);
        return;
    }

    logEntries.forEach(entry => {
        const li = document.createElement('li');

        const spanUid = document.createElement('span');
        spanUid.textContent = entry.uid;

        const spanTime = document.createElement('span');
        spanTime.className = 'sl-time';
        spanTime.textContent = formatDate(entry.timestamp);

        const spanEvent = document.createElement('span');
        spanEvent.className = 'sl-duration';
        spanEvent.textContent = getEventLabel(entry.event_type);

        li.appendChild(spanUid);
        li.appendChild(spanTime);
        li.appendChild(spanEvent);

        list.appendChild(li);
    });
}

function showError(message) {
    const tbody = document.getElementById('log-body');
    tbody.innerHTML = '';

    const tr = document.createElement('tr');
    const td = document.createElement('td');

    td.colSpan = 5;
    td.textContent = `Ошибка загрузки: ${message}`;
    td.style.textAlign = 'center';
    td.style.color = '#d08080';

    tr.appendChild(td);
    tbody.appendChild(tr);
}

async function refreshAll() {
    try {
        const [stats, history] = await Promise.all([
            apiGet('/api/stats'),
            apiGet('/api/history')
        ]);

        currentStats = stats;
        logEntries = history.items || [];

        renderLogs();
        renderStats();
        renderStock();

        console.log('Statistics updated:', currentStats);
        console.log('History updated:', logEntries);
    } catch (error) {
        console.error('API error:', error);
        showError(error.message);
    }
}

function refreshStats() {
    refreshAll();
}

function switchTab(tabName) {
    document.querySelectorAll('.tab').forEach(tab => {
        tab.classList.remove('active');
    });

    document.querySelector(
        `.tab[data-tab="${tabName}"]`
    ).classList.add('active');

    document.querySelectorAll('.panel').forEach(panel => {
        panel.classList.add('hidden');
    });

    if (tabName === 'logs') {
        document.getElementById('logs-panel')
            .classList.remove('hidden');
    } else if (tabName === 'stats') {
        document.getElementById('stats-panel')
            .classList.remove('hidden');

        renderStats();
    }
}

document.addEventListener('DOMContentLoaded', () => {
    refreshAll();

    document.querySelectorAll('.tab').forEach(tab => {
        tab.addEventListener('click', () => {
            switchTab(tab.dataset.tab);
        });
    });

    document.getElementById('refresh-btn')
        .addEventListener('click', refreshAll);

    document.getElementById('refresh-side-btn')
        .addEventListener('click', refreshAll);

    document.getElementById('refresh-footer-btn')
        .addEventListener('click', refreshAll);

    document.getElementById('refresh-stats-btn')
        .addEventListener('click', refreshStats);

    // Автоматическое обновление раз в 10 секунд
    window.setInterval(refreshAll, 10000);
});

function formatDuration(ms) {
    const totalMinutes = Math.floor(ms / 60000);
    const hours = Math.floor(totalMinutes / 60);
    const minutes = totalMinutes % 60;

    if (hours > 0) {
        return `${hours}ч ${minutes}м`;
    }

    return `${minutes}м`;
}