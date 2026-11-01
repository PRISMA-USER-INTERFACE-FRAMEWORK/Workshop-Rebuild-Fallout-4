let _mpCards = {};
let _mpStatusHooked = false;

function mpApi() {
    const a = window.__prismaUI_modelPreview;
    return (a && typeof a.show === 'function') ? a : null;
}
function meshPathsOf(r) {
    if (!r) return null;
    if (r.nifPaths && r.nifPaths.length) return r.nifPaths;
    return r.nifPath ? [r.nifPath] : null;
}

function cardVisible(el) {
    const strip = document.getElementById('strip');
    if (!strip || !el || !el.isConnected) return false;
    const r = el.getBoundingClientRect(), s = strip.getBoundingClientRect();
    if (!(r.width > 0 && r.height > 0)) return false;
    return r.bottom > s.top && r.top < s.bottom && r.right > s.left && r.left < s.right;
}

function clipTo(el, boundary) {
    if (!el) return {};
    const c = el.getBoundingClientRect();
    const b = boundary ? boundary.getBoundingClientRect() : null;
    const left = b ? Math.max(c.left, b.left) : c.left;
    const top = b ? Math.max(c.top, b.top) : c.top;
    const right = b ? Math.min(c.right, b.right) : c.right;
    const bottom = b ? Math.min(c.bottom, b.bottom) : c.bottom;
    if (!(right > left && bottom > top)) return {};
    return { clipX: Math.round(left), clipY: Math.round(top),
             clipW: Math.round(right - left), clipH: Math.round(bottom - top) };
}
function showCardPreview(entry) {
    const api = mpApi();
    if (!api || !entry) { if (entry && entry.el) entry.el.classList.remove('has3d'); return; }
    hookPreviewStatus();
    const box = entry.el;
    if (!cardVisible(box)) { api.hide({ id: entry.id }); box.classList.remove('has3d'); return; }
    const rc = box.getBoundingClientRect();
    const s = Math.floor(Math.min(rc.width, rc.height));

    if (s < 20) { api.hide({ id: entry.id }); box.classList.remove('has3d'); return; }
    const paths = meshPathsOf(entry.r);
    if (!paths) { api.hide({ id: entry.id }); box.classList.remove('has3d'); return; }
    const opts = {
        id: entry.id,

        formType: 'STAT', matSwap: entry.r.matSwap || '',
        x: Math.round(rc.left + (rc.width - s) / 2), y: Math.round(rc.top + (rc.height - s) / 2),
        w: s, h: s, spin: 0, brightness: 1.25, zoom: 0.92,
        ...clipTo(box, document.getElementById('strip'))
    };
    if (paths.length > 1) opts.nifPaths = paths; else opts.nifPath = paths[0];
    api.show(opts);
    box.classList.add('has3d');
}
function syncCardPreviews() {
    Object.keys(_mpCards).forEach(id => showCardPreview(_mpCards[id]));
}
function hideCardPreviews() {
    const api = mpApi();
    Object.keys(_mpCards).forEach(id => {
        if (api) api.hide({ id: id });
        if (_mpCards[id].el) _mpCards[id].el.classList.remove('has3d');
    });
    _mpCards = {};
}

function hideCardPreviewsKeepEntries() {
    const api = mpApi();
    Object.keys(_mpCards).forEach(id => {
        if (api) api.hide({ id: id });
        if (_mpCards[id].el) _mpCards[id].el.classList.remove('has3d');
    });
}

function hookPreviewStatus() {
    if (_mpStatusHooked) return;
    const api = mpApi();
    if (!api || typeof api.onStatus !== 'function') return;
    _mpStatusHooked = true;
    api.onStatus(s => {
        if (!s || s.phase !== 'error') return;
        const e = _mpCards[s.id];
        if (e && e.el) e.el.classList.remove('has3d');
        if (s.id === 'wkdetail') {
            const pane = document.getElementById('preview');
            if (pane) pane.classList.remove('has3d');
            _mpShown = false;
        }
        try { api.hide({ id: s.id }); } catch (err) {}
    });
}

function modelPreview() {
    const a = window.__prismaUI_modelPreview;
    return (a && typeof a.show === 'function') ? a : null;
}
let _mpShown = false;
function showLivePreview(r) {
    const api = modelPreview();
    const pane = document.getElementById('preview');
    const paths = meshPathsOf(r);
    if (!api || !r || !paths) {
        if (pane) pane.classList.remove('has3d');
        if (pane) pane.innerHTML = `<span class="pv-hint">${esc(pcText(r ? 'No model for this item' : 'Select an item'))}</span>`;
        if (api && _mpShown) { try { api.hide({ id: 'wkdetail' }); } catch (e) {} _mpShown = false; }
        return;
    }
    hookPreviewStatus();
    pane.innerHTML = '';
    const rc = pane.getBoundingClientRect();
    const s = Math.min(rc.width, rc.height);
    const sx = Math.round(rc.left + (rc.width - s) / 2);
    const sy = Math.round(rc.top + (rc.height - s) / 2);

    const opts = {
        id: 'wkdetail', formType: 'STAT', matSwap: r.matSwap || '',
        x: sx, y: sy, w: Math.round(s), h: Math.round(s), spin: 30, brightness: 1.15,
        ...clipTo(pane)
    };
    if (paths.length > 1) opts.nifPaths = paths; else opts.nifPath = paths[0];
    api.show(opts);
    if (pane) pane.classList.add('has3d');
    _mpShown = true;
}
function hideLivePreview() {
    const api = modelPreview();
    if (api && _mpShown) { try { api.hide({ id: 'wkdetail' }); } catch (e) {} }
    _mpShown = false;
    const pane = document.getElementById('preview');
    if (pane) pane.classList.remove('has3d');
    if (pane) pane.innerHTML = `<span class="pv-hint">${esc(pcText("Select an item"))}</span>`;
    const meta = document.getElementById('pv-meta');
    if (meta) meta.innerHTML = `<span class="dimtxt">${esc(pcText("No selection"))}</span>`;
}
window.addEventListener('resize', () => {
    if (selectedItem) showLivePreview(selectedItem);
    syncCardPreviews();
    reportPanelRects();
});

function reportPanelRects() {
    if (typeof window.prisma_panel_rects !== 'function') return;
    const w = window.innerWidth, h = window.innerHeight;
    if (!w || !h) return;
    const out = [];

    ['dock', 'controls'].forEach(id => {
        const el = document.getElementById(id);
        if (!el) return;
        const r = el.getBoundingClientRect();
        if (r.width <= 0 || r.height <= 0) return;
        out.push([r.left / w, r.top / h, r.right / w, r.bottom / h]);
    });
    try { window.prisma_panel_rects(JSON.stringify(out)); } catch (e) {}
}

(() => {
    if (typeof ResizeObserver !== 'function') return;
    let queued = false;
    const ro = new ResizeObserver(() => {
        if (queued) return;
        queued = true;
        requestAnimationFrame(() => { queued = false; reportPanelRects(); });
    });
    ['dock', 'controls'].forEach(id => {
        const el = document.getElementById(id);
        if (el) ro.observe(el);
    });
})();

(() => {
    if (typeof ResizeObserver !== 'function') return;
    const pane = document.getElementById('preview');
    if (!pane) return;
    let queued = false;
    new ResizeObserver(() => {
        if (queued) return;
        queued = true;
        requestAnimationFrame(() => {
            queued = false;
            if (selectedItem) showLivePreview(selectedItem);
            syncCardPreviews();
        });
    }).observe(pane);
})();

(() => {
    const strip = document.getElementById('strip');
    if (!strip) return;
    let queued = false;
    strip.addEventListener('scroll', () => {
        hideCardPreviewsKeepEntries();
        if (queued) return;
        queued = true;
        requestAnimationFrame(() => { queued = false; syncCardPreviews(); });
    });
})();
