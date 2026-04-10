const API = '/api';
const startTime = Date.now();
let itemCount = 0;

// ── Fetch & Render ──────────────────────────────────────────────

async function fetchItems() {
    const t0 = performance.now();
    try {
        const res = await fetch(`${API}/items`);
        const items = await res.json();
        recordLatency(t0);
        logRequest('GET', '/api/items', res.status);
        renderItems(items);
    } catch (e) {
        logRequest('GET', '/api/items', 0);
    }
}

function renderItems(items) {
    const tbody = document.getElementById('items-body');
    const empty = document.getElementById('empty-state');
    const countEl = document.getElementById('tel-count');

    itemCount = items.length;
    countEl.textContent = itemCount;

    if (items.length === 0) {
        tbody.innerHTML = '';
        empty.classList.add('visible');
        return;
    }

    empty.classList.remove('visible');
    tbody.innerHTML = items.map(item => `
        <tr class="row-enter">
            <td class="cell-id">${item.id}</td>
            <td class="cell-name">${esc(item.name)}</td>
            <td class="cell-price">${formatPrice(item.price)}</td>
            <td class="col-action">
                <button class="btn-delete" onclick="deleteItem(${item.id}, this)" title="Delete">DEL</button>
            </td>
        </tr>
    `).join('');
}

// ── CRUD Operations ─────────────────────────────────────────────

async function addItem(e) {
    e.preventDefault();
    const nameInput = document.getElementById('item-name');
    const priceInput = document.getElementById('item-price');
    const name = nameInput.value.trim();
    const price = parseInt(priceInput.value) || 0;
    if (!name) return;

    const t0 = performance.now();
    try {
        const res = await fetch(`${API}/items`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name, price })
        });
        recordLatency(t0);
        logRequest('POST', '/api/items', res.status);

        if (res.ok) {
            nameInput.value = '';
            priceInput.value = '0';
            nameInput.focus();
            toast(`Inserted "${name}"`, 'success');
            fetchItems();
        } else {
            toast('Insert failed', 'error');
        }
    } catch (e) {
        logRequest('POST', '/api/items', 0);
        toast('Network error', 'error');
    }
}

async function deleteItem(id, btn) {
    const row = btn.closest('tr');
    row.classList.add('row-exit');

    const t0 = performance.now();
    try {
        const res = await fetch(`${API}/items/${id}`, { method: 'DELETE' });
        recordLatency(t0);
        logRequest('DELETE', `/api/items/${id}`, res.status);
        toast(`Deleted #${id}`, 'success');

        setTimeout(() => fetchItems(), 200);
    } catch (e) {
        logRequest('DELETE', `/api/items/${id}`, 0);
        row.classList.remove('row-exit');
        toast('Delete failed', 'error');
    }
}

// ── Health Check ────────────────────────────────────────────────

async function checkHealth() {
    const dot = document.querySelector('.beacon-dot');
    const label = document.getElementById('beacon-label');
    const modeEl = document.getElementById('tel-mode');

    try {
        const res = await fetch(`${API}/health`);
        const data = await res.json();
        dot.className = 'beacon-dot live';
        label.textContent = 'ONLINE';
        label.style.color = 'var(--green)';
        modeEl.textContent = data.mode || 'Unknown';
    } catch {
        dot.className = 'beacon-dot dead';
        label.textContent = 'OFFLINE';
        label.style.color = 'var(--red)';
        modeEl.textContent = '--';
    }
}

// ── Request Log ─────────────────────────────────────────────────

const MAX_LOG = 50;

function logRequest(method, path, status) {
    const container = document.getElementById('log-entries');
    const empty = container.querySelector('.log-empty');
    if (empty) empty.remove();

    const time = new Date().toLocaleTimeString('en', { hour12: false });
    const isErr = !status || status >= 400;

    const entry = document.createElement('div');
    entry.className = 'log-entry';
    entry.innerHTML = `
        <span class="log-method ${method.toLowerCase()}">${method}</span>
        <span class="log-path">${esc(path)}</span>
        <span class="log-status${isErr ? ' err' : ''}">${status || 'ERR'}</span>
        <span class="log-time">${time}</span>
    `;

    container.prepend(entry);

    // Trim old entries
    while (container.children.length > MAX_LOG) {
        container.lastChild.remove();
    }
}

// ── Telemetry ───────────────────────────────────────────────────

function updateUptime() {
    const totalSecs = Math.floor((Date.now() - startTime) / 1000);
    const m = String(Math.floor(totalSecs / 60)).padStart(2, '0');
    const s = String(totalSecs % 60).padStart(2, '0');
    document.getElementById('tel-uptime').textContent = `${m}:${s}`;
}

function recordLatency(t0) {
    const ms = (performance.now() - t0).toFixed(0);
    document.getElementById('tel-latency').textContent = `${ms}ms`;
}

// ── Toast ───────────────────────────────────────────────────────

function toast(message, type) {
    // Remove existing toast
    document.querySelectorAll('.toast').forEach(t => t.remove());

    const el = document.createElement('div');
    el.className = `toast ${type}`;
    el.textContent = message;
    document.body.appendChild(el);

    setTimeout(() => el.remove(), 2600);
}

// ── Utilities ───────────────────────────────────────────────────

function formatPrice(v) {
    return v.toLocaleString('en-US');
}

function esc(str) {
    const d = document.createElement('div');
    d.textContent = str;
    return d.innerHTML;
}

// ── Init ────────────────────────────────────────────────────────

document.getElementById('add-form').addEventListener('submit', addItem);
fetchItems();
checkHealth();
setInterval(updateUptime, 1000);
setInterval(checkHealth, 10000);
