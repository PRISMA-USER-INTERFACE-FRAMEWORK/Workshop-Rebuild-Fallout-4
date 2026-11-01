const CAT_SVG = {
    'Structures':   '<path d="M3 20V9l9-5 9 5v11"/><path d="M3 12h18M9 20V12M15 20V12"/>',
    'Furniture':    '<path d="M3 17v-5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2v5"/><path d="M3 17h18M5 17v3M19 17v3M6 10V7h12v3"/>',
    'Power':        '<path d="M13 2L5 14h6l-1 8 8-12h-6z"/>',
    'Defense':      '<path d="M12 3l8 3v6c0 5-3.5 8-8 9-4.5-1-8-4-8-9V6z"/>',
    'Resources':    '<path d="M12 3v18M12 8l5-3M12 14l-5-3"/><circle cx="12" cy="21" r="1"/>',
    'Decorations':  '<rect x="4" y="4" width="16" height="16"/><path d="M4 15l4-4 4 4 3-3 5 5"/><circle cx="9" cy="9" r="1.4"/>',
    'Food':         '<path d="M6 3v8a3 3 0 0 0 6 0V3M9 11v10"/><path d="M17 3c-1.5 2-2 4-2 6s.5 3 2 3v9"/>',
    'Stores':       '<path d="M4 9h16l-1 11H5z"/><path d="M8 9V6a4 4 0 0 1 8 0v3"/>',
    'Crafting':     '<path d="M14 7l3-3 3 3-3 3z"/><path d="M15 9L5 19l-2-2L13 7"/><path d="M4 4l4 4"/>',
    'Raider':       '<path d="M12 3l9 16H3z"/><path d="M12 9v5M12 17h.01"/>',
    'Special':      '<path d="M12 3l2.6 6.2 6.4.5-4.9 4.2 1.5 6.1L12 16.8 6.4 20l1.5-6.1L3 9.7l6.4-.5z"/>',
    'Decor':        '<rect x="4" y="4" width="16" height="16"/><path d="M4 15l4-4 4 4 3-3 5 5"/><circle cx="9" cy="9" r="1.4"/>'
};
function catSvg(cat) {
    const body = CAT_SVG[cat] || '<rect x="4" y="4" width="16" height="16"/><path d="M8 12h8"/>';
    return `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.4"
        stroke-linecap="round" stroke-linejoin="round">${body}</svg>`;
}
function loadFavs() {
    try {
        const raw = JSON.parse(localStorage.getItem(FAV_KEY) || '[]');
        favs = new Set(Array.isArray(raw) ? raw : []);
    } catch (e) { favs = new Set(); }
}
function saveFavs() {
    try { localStorage.setItem(FAV_KEY, JSON.stringify([...favs])); } catch (e) {}
}
function isFav(r) {
    // Must match the legacy favorite key; verify this against src/CobjCacheSerialization.cpp:RecipeJson.
    return favs.has(keyOf(r)) || (r.legacyFirst !== false && favs.has(String(r.formRef)));
}
const STAT_DEFS = [
    { key: 'happiness',  label: 'Happy',   pct: true },
    { key: 'population', label: 'Pop' },
    { key: 'food',       label: 'Food' },
    { key: 'water',      label: 'Water' },
    { key: 'power',      label: 'Power' },
    { key: 'defense',    label: 'Defense' },
    { key: 'beds',       label: 'Beds' }
];

function scrollChipIntoView(row, el) {
    if (!row || !el) return;
    const pad = 24;
    const left = el.offsetLeft - pad;
    const right = el.offsetLeft + el.offsetWidth + pad;
    if (left < row.scrollLeft) row.scrollLeft = left;
    else if (right > row.scrollLeft + row.clientWidth) row.scrollLeft = right - row.clientWidth;
}
function resetCatalogNavigation() {
    navIndex = -1;
    selectedItem = null;
    hideLivePreview();
}
function renderCats() {
    const row = document.getElementById('cat-row'); if (!row) return;
    if (favView) {
        row.innerHTML = `<div class="sub-empty">${esc(pcText("Showing favorites — click the star again to leave"))}</div>`;
        updateRowArrows();
        return;
    }
    const counts = Object.create(null);
    catalogCategories.forEach(c => { counts[c.cat] = c.count; });
    if (currentCategory !== 'All' && !Object.prototype.hasOwnProperty.call(counts, currentCategory))
        counts[currentCategory] = 0;
    const cats = ['All'].concat(Object.keys(counts).sort());
    row.innerHTML = '';
    let active = null;
    cats.forEach(c => {
        const b = document.createElement('div');
        b.className = 'cat' + (c === currentCategory ? ' active' : '');
        const n = (c === 'All') ? catalogFacetTotal : counts[c];
        b.innerHTML = `<span>${esc(pcText(c))}</span><span class="n">${n}</span>`;
        b.onclick = () => {
            currentCategory = c; currentSub = 'All'; resetCatalogNavigation();
            requestCatalogPage(0);
        };
        row.appendChild(b);
        if (c === currentCategory) active = b;
    });
    scrollChipIntoView(row, active);
    updateRowArrows();
}
function subsFor(cat) {
    const counts = Object.create(null);
    for (const category of (favView ? catalogFavoriteCategories : catalogCategories)) {
        if (cat !== 'All' && category.cat !== cat) continue;
        for (const sub of category.subs) counts[sub.sub] = (counts[sub.sub] || 0) + sub.count;
    }
    return counts;
}
function renderSubs() {
    const row = document.getElementById('sub-row'); if (!row) return;
    if (currentCategory === 'All' && !favView) {
        currentSub = 'All';
        row.innerHTML = `<div class="sub-empty">${esc(pcText("Pick a category to filter by sub-type"))}</div>`;
        updateRowArrows();
        return;
    }
    const counts = subsFor(currentCategory);
    if (currentSub !== 'All' && !Object.prototype.hasOwnProperty.call(counts, currentSub))
        counts[currentSub] = 0;
    const keys = Object.keys(counts).sort();
    row.innerHTML = '';
    if (keys.length <= 1 && currentSub === 'All') {
        row.innerHTML = `<div class="sub-empty">${esc(pcText("No sub-categories"))}</div>`;
        updateRowArrows();
        return;
    }
    let activeSub = null;
    ['All'].concat(keys).forEach(s => {
        const b = document.createElement('div');
        b.className = 'sub' + (s === currentSub ? ' active' : '');
        b.textContent = pcText(s);
        b.onclick = () => { currentSub = s; resetCatalogNavigation(); requestCatalogPage(0); };
        row.appendChild(b);
        if (s === currentSub) activeSub = b;
    });
    scrollChipIntoView(row, activeSub);
    updateRowArrows();
}
function setFilter(f) {
    filterMode = f;
    document.querySelectorAll('.lk-btn').forEach(b => b.classList.toggle('active', b.dataset.f === f));
    resetCatalogNavigation(); requestCatalogPage(0);
}
function clearSearch() {
    const q = document.getElementById('q');
    if (q) { q.value = ''; currentSearch = ''; }
    document.querySelector('.search')?.classList.remove('has');
    resetCatalogNavigation(); requestCatalogPage(0);
}
function currentView() {
    return allItems;
}
function resList(r) { return Array.isArray(r && r.resources) ? r.resources : []; }
function resSign(v) { return (v > 0 ? '+' : '') + v; }
const RES_SHORT = { Power: 'PWR', Food: 'FOOD', Water: 'H2O', Defense: 'DEF', Beds: 'BED', People: 'POP' };
function resBadges(r) {
    const list = resList(r);
    if (!list.length) return '';
    return '<span class="res">' + list.map(x =>
        `<span class="res-b ${x.value < 0 ? 'neg' : 'pos'}">${esc(resSign(x.value))}&#8239;${esc(pcText(RES_SHORT[x.label] || x.label))}</span>`
    ).join('') + '</span>';
}
function costSummary(r) {
    const comps = r.components || [];
    if (!comps.length) return { text: pcText('Free'), short: false };
    let short = false;
    comps.forEach(c => { if ((c.have || 0) < (c.count || 0)) short = true; });
    const first = comps[0];
    const extra = comps.length > 1 ? ` +${comps.length - 1}` : '';
    return { text: `${cleanName(first.name)} x${first.count || 0}${extra}`, short };
}
function renderStrip() {
    const strip = document.getElementById('strip'); if (!strip) return;
    const view = currentView();
    const visible = view.slice(0, WORKSHOP_RENDER_LIMIT);
    if (selectedItem) {
        const selectedIndex = view.findIndex(r => keyOf(r) === keyOf(selectedItem));
        if (selectedIndex < 0) {
            selectedItem = null;
            navIndex = -1;
            hideLivePreview();
        } else {
            navIndex = selectedIndex;
        }
    } else {
        navIndex = -1;
    }
    hideCardPreviews();
    strip.innerHTML = '';
    if (!view.length) {
        strip.innerHTML = `<div class="empty">${esc(pcText(favView && !favs.size ? 'No favorites yet — press F on an item' : 'Nothing matches the current filters'))}</div>`;
        return;
    }
    visible.forEach((item, i) => {
        const card = document.createElement('div');
        const buildable = isCraftable(item);
        card.className = 'card' + (selectedItem && keyOf(item) === keyOf(selectedItem) ? ' sel' : '') + (buildable ? '' : ' locked');
        card.style.animationDelay = Math.min(i, 14) * 12 + 'ms';
        const cs = costSummary(item);
        const col = CAT_COLOR[item.__cat] || 'var(--accent-d)';
        card.innerHTML =
            `<span class="edge" style="background:${col}"></span>` +
            (item.locked ? `<span class="lk">${esc(pcText("Locked"))}</span>` : '') +
            `<span class="fav${isFav(item) ? ' on' : ''}">&#9733;</span>` +
            `<span class="ico"><span class="ico-svg">${catSvg(item.__cat)}</span></span>` +
            `<span class="nm">${esc(item.__disp)}</span>` +
            resBadges(item) +
            `<span class="cost ${cs.short ? 'short' : 'ok'}">${esc(cs.text)}</span>`;
        card.onclick = () => selectItem(item);
        card.querySelector('.fav').onclick = e => { e.stopPropagation(); toggleFav(item); };
        strip.appendChild(card);
        if (meshPathsOf(item)) {
            const id = 'wk_' + keyOf(item).replace(/[^A-Za-z0-9]/g, c => '_' + c.charCodeAt(0) + '_');
            _mpCards[id] = { id: id, el: card.querySelector('.ico'), r: item };
        }
    });
    hookPreviewStatus();
    requestAnimationFrame(syncCardPreviews);
    settleSync();
    whenPreviewApiReady();
}
let _settleTimers = [];
function settleSync() {
    _settleTimers.forEach(clearTimeout);
    _settleTimers = [120, 300, 500].map(ms => setTimeout(syncCardPreviews, ms));
}
let _mpWaitTimer = null;
function whenPreviewApiReady() {
    if (mpApi()) { hookPreviewStatus(); syncCardPreviews(); return; }
    if (_mpWaitTimer) return;
    let tries = 0;
    _mpWaitTimer = setInterval(() => {
        if (mpApi()) {
            clearInterval(_mpWaitTimer); _mpWaitTimer = null;
            hookPreviewStatus();
            syncCardPreviews();
        } else if (++tries > 75) {
            clearInterval(_mpWaitTimer); _mpWaitTimer = null;
        }
    }, 200);
}
function nudge(dir) {
    const strip = document.getElementById('strip'); if (!strip) return;
    const next = catalogPage + dir;
    if (next >= 0 && next < catalogPageCount) {
        requestCatalogPage(next, dir < 0);
        return;
    }
    strip.scrollLeft += dir * strip.clientWidth * 0.7;
}
function wireHorizontalWheel(el) {
    if (!el) return;
    el.addEventListener('wheel', e => {
        const unit = e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? el.clientWidth : 1;
        const delta = (Math.abs(e.deltaX) > Math.abs(e.deltaY) ? e.deltaX : e.deltaY) * unit;
        if (!delta) return;
        el.scrollLeft += delta;
        e.preventDefault();
    }, { passive: false });
}
wireHorizontalWheel(document.getElementById('strip'));
wireHorizontalWheel(document.getElementById('cat-row'));
wireHorizontalWheel(document.getElementById('sub-row'));
const ROW_RAILS = [
    { scroll: 'cat-scroll', row: 'cat-row', prev: 'cat-prev', next: 'cat-next' },
    { scroll: 'sub-scroll', row: 'sub-row', prev: 'sub-prev', next: 'sub-next' },
];
function updateRowArrows() {
    ROW_RAILS.forEach(r => {
        const scroll = document.getElementById(r.scroll);
        const row = document.getElementById(r.row);
        const prev = document.getElementById(r.prev);
        const next = document.getElementById(r.next);
        if (!scroll || !row || !prev || !next) return;
        const fits = row.scrollWidth <= row.clientWidth + 1;
        scroll.classList.toggle('fits', fits);
        prev.disabled = fits || row.scrollLeft <= 0;
        next.disabled = fits || row.scrollLeft + row.clientWidth >= row.scrollWidth - 1;
    });
}
ROW_RAILS.forEach(r => {
    const row = document.getElementById(r.row);
    const prev = document.getElementById(r.prev);
    const next = document.getElementById(r.next);
    if (!row || !prev || !next) return;
    const page = dir => {
        row.scrollLeft += dir * Math.max(80, row.clientWidth * 0.8);
        updateRowArrows();
    };
    prev.onclick = () => page(-1);
    next.onclick = () => page(1);
    row.addEventListener('scroll', updateRowArrows, { passive: true });
});
window.addEventListener('resize', updateRowArrows);
function markSelection(i) {
    const cards = document.querySelectorAll('#strip .card');
    cards.forEach((c, n) => c.classList.toggle('sel', n === i));
    const card = cards[i];
    if (card && card.scrollIntoView) card.scrollIntoView({ block: 'nearest', inline: 'center' });
}
function paintDetail(item) {
    const meta = document.getElementById('pv-meta');
    const resFull = resList(item).map(x =>
        `<span class="pv-res-b ${x.value < 0 ? 'neg' : 'pos'}">${esc(resSign(x.value))} ${esc(pcText(x.label))}</span>`
    ).join('');
    const pathText = String(menuPathOf(item) || item.__cat).split(' > ').map(part => pcText(part)).join(' > ');
    if (meta) meta.innerHTML = `<span class="pv-name">${esc(item.__disp)}</span>` +
        `<span class="pv-path">${esc(pathText)}</span>` +
        (resFull ? `<span class="pv-res">${resFull}</span>` : '');
    refreshMaterials(item);
    showLivePreview(item);
}
function refreshMaterials(item) {
    const box = document.getElementById('materials-box'); if (!box) return;
    const forEl = document.getElementById('req-for');
    if (forEl) forEl.textContent = item ? item.__disp : '';
    const comps = item?.components || [];
    let html = '';
    comps.forEach(c => {
        const need = c.count || 0;
        const have = (typeof c.have === 'number') ? c.have : (playerResources[cleanName(c.name).toLowerCase()] || 0);
        const stored = (typeof c.stored === 'number') ? c.stored : have;
        const carried = Math.max(0, have - stored);
        const tip = carried > 0 ? pcText('{{stored}} stored in the workshop, {{carried}} carried by you', {stored, carried}) : pcText('{{count}} stored in the workshop', {count: have});
        html += `<span class="mchip ${have < need ? 'short' : 'ok'}" title="${esc(tip)}">` +
                `<span class="mn">${esc(cleanName(c.name))}</span><span class="mq">${have}/${need}</span></span>`;
    });
    const configured = Array.isArray(item?.requirements);
    (configured ? item.requirements : item?.perks || []).forEach(p => {
        const missing = configured ? p.met === false : p.owned === false;
        html += `<span class="mchip perkchip${missing ? ' miss' : ''}"><span class="mn">${esc(configured ? pcRequirementText(p) : pcPerkName(p))}</span></span>`;
    });
    if (!configured) {
        const groups = item?.toolGroups?.length ? item.toolGroups : (item?.tools || []).map(label => ({label}));
        groups.forEach(g => {
            const label = cleanName(g?.label) || g?.label || '';
            if (!label) return;
            html += `<span class="mchip perkchip${g.ok === false ? ' miss' : ''}"><span class="mn">${esc(label)}</span></span>`;
        });
    }
    box.innerHTML = html || `<span class="dimtxt">${esc(pcText(item ? "No materials required." : "Select an item."))}</span>`;
}
