let allItems = [];
let currentCategory = 'All';
let currentSub = 'All';
let currentSearch = '';
let filterMode = 'ALL';
let favView = false;
let selectedItem = null;
let playerResources = {};
let isPlacing = false;
let favs = new Set();
let navIndex = -1;
let pendingBuildables = '';
let pendingSelectionKey = '';
let expectedBuildableChunks = null;
let receivedBuildableChunks = 0;
let buildableDeliveryFailed = false;
let activeBuildableSerial = '';
let activeBuildableRequestId = 0;
let lastBuildableSerial = 0;
let catalogSession = 0;
let catalogRequestId = 0;
let catalogPage = 0;
let catalogPageCount = 0;
let catalogFacetTotal = 0;
let catalogCategories = [];
let catalogFavoriteCategories = [];
let pendingNavigationEnd = false;

const WORKSHOP_RENDER_LIMIT = 64;

const FAV_KEY = 'pc_ws_favs';

const CAT_COLOR = {
    'Structures': '#5b7fa6', 'Furniture': '#a6713f', 'Power': '#b39a3c', 'Defense': '#9c4640',
    'Resources': '#4f7d52', 'Decorations': '#7a5f9c', 'Food': '#6f8f43', 'Stores': '#3f7f86',
    'Crafting': '#8a6a4a', 'Special': '#96608a', 'Miscellaneous': '#5f5f5c'
};

function esc(s) {
    return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}
function cleanName(s) { return (s || '').replace(/_/g, ' ').trim(); }
function dName(r) { return cleanName(r.name) || cleanName(r.editorId) || pcText('Unknown'); }
function menuPathOf(r) {
    if (r.menuPath && r.menuPath.length) return r.menuPath;
    if (r.filters && r.filters[0]) return cleanName(r.filters[0]);
    return '';
}
function catOf(r) {
    const p = menuPathOf(r);
    if (p) { const t = p.split(' > ')[0]; if (t) return t; }
    return 'Misc';
}

function subOf(r) {
    const p = menuPathOf(r);
    if (!p) return 'General';
    const parts = p.split(' > ');
    return parts.length > 1 ? parts.slice(1).join(' > ') : 'General';
}
function isCraftable(r) {
    if (r.locked) return false;
    const c = r.components || [];
    return !c.some(comp => (comp.have || 0) < (comp.count || 0));
}

function keyOf(r) { return r ? (r.recipeKey || r.formRef || r.editorId || r.createdKey) : ''; }
function validPlacementSelectionIndex(view, selected, index) {
    if (!selected || index < 0 || index >= view.length) return -1;
    return keyOf(view[index]) === keyOf(selected) ? index : -1;
}
function imeFocus(el) {
    try { if (window.__prismaImeFocusNotify) window.__prismaImeFocusNotify(el); } catch (e) {}
}

const STAT_ICON_PATH = {
    happiness: 'M458.656 19.72c-57.77 6.715-81.53 39.592-99.28 77.093 74.776 2.09 108.602-23.12 99.28-77.094zM54.937 21.374c-9.32 53.974 24.505 79.154 99.282 77.063-17.752-37.5-41.512-70.346-99.282-77.063zM467.844 175.03c-4.32.2-9.382 2.47-15.22 7.19-8.233 6.657-16.966 12.814-26.124 18.468 28.704 32.76 45.837 74.055 56.563 118.437 10.61-69.51 11.1-145.308-15.22-144.094zM60.47 180.688c-25.47-.264-30.547 76.598-21.157 146.657 14.426-42.205 34.88-83.047 62.218-119.656-9.302-5.88-18.05-12.415-26.093-19.625-5.608-5.03-10.59-7.33-14.968-7.375zm341.592 33.407c-15.548 7.583-31.938 13.896-48.875 18.906 27.92 71.072 40.385 145.85 48.188 220.28 20.714-79.27 18.07-159.615.688-239.186zm-274.625 7.625c-16.893 65.397-28.75 128.586-20.874 194.124 13.266-63.588 33.256-123.988 66.156-178.22-15.82-4.037-31.007-9.326-45.282-15.905zm296.5 6.093c14.383 75.347 15.57 152.055-3.812 227.937 16.663-10.085 29.597-22.548 37.156-37.563 5.687-11.294 11.303-28.935 16.25-49.625-8.04-52.642-22.32-102.276-49.592-140.75zm-319.093 7.656c-27.066 40.656-46.15 86.125-58.22 132.436 4.792 20.483 10.948 38.554 18.282 51.438 5.968 10.483 15.548 19.897 27.844 28.156-15.18-73.294-4.838-142.62 12.094-212.03zm223.5 3.874a357.202 357.202 0 0 1-59.22 7.687c22.523 66.597 34.172 134.63 43.876 202.595 10.35-70.784 13.795-140.562 15.344-210.28zm-128.75 3.875c-10.376 73.08-12.596 154.426 6.5 221.75 6.98-71.93 20.608-149.46 40.78-217.626-15.99-.28-31.828-1.637-47.28-4.125zm-21.688 23.06c-31.53 58.626-49.375 124.554-60.78 194.595 21.327 9.65 46.993 16.945 74.624 21.688-21.974-66.086-22.11-144.102-13.844-216.282zm168.188 2.22c-2.162 71.314-6.807 143.27-19.813 216.78 20.76-2.794 40.505-7.197 58.25-13.28-6.67-69.114-16.73-137.997-38.436-203.5zm-87.625 6.72c-18.06 67.556-30.09 143.006-35.626 211.5 25.292 2.497 51.388 2.97 76.687 1.25-9.87-72.005-20.188-143.462-41.06-212.75z',
    population: 'M250.882 22.802c-23.366 3.035-44.553 30.444-44.553 65.935 0 19.558 6.771 36.856 16.695 48.815l11.84 14.263-18.217 3.424c-12.9 2.425-22.358 9.24-30.443 20.336-8.085 11.097-14.266 26.558-18.598 44.375-7.843 32.28-9.568 71.693-9.842 106.436h42.868l11.771 157.836c29.894 6.748 61.811 6.51 90.602.025l10.414-157.86h40.816c-.027-35.169-.477-75.126-7.584-107.65-3.918-17.934-9.858-33.372-18.04-44.343-8.185-10.97-18.08-17.745-32.563-19.989l-18.592-2.88 11.736-14.704c9.495-11.897 15.932-28.997 15.932-48.082 0-37.838-23.655-65.844-49.399-65.844z',
    food: 'M440.156 98.063c-4.05-.11-8.044 1.117-11.656 4-1.542 1.23-3.1 2.884-4.47 4.75-22.9 31.22-48.278 57.33-75.186 81-8.76 14.692 10.695 44.406 25.594 44.406 34.144-12.928 69.617-22.516 106.75-26.314 42.628-4.358 17.688-68.134-26.25-45.47 30.3-22.666 7.094-61.79-14.782-62.374zM310.47 108.156c-6.167-.108-12.215 1.172-17.595 4.28a30.82 30.82 0 0 0-4.406 3.095c-.038-.056-.09-.13-.126-.186-79.912 66.402-116.334 73.468-158.22 86.844-6.886 2.2-12.27 7.18-16.5 15.5s-6.892 19.843-7.343 33.125c-.9 26.563 6.935 59.927 23.72 89.03 16.72 28.99 43.37 53.946 67.656 67.126 12.143 6.59 23.682 10.167 32.375 10.436 8.694.27 13.915-1.95 17.782-7.375 30.986-43.45 89.343-69.052 156.157-92.25-.005-.006.003-.023 0-.03a31.008 31.008 0 0 0 5.842-2.594c8.2-4.738 13.535-12.897 16.282-22.125 2.747-9.227 3.22-19.75 2.03-31.25-.953-9.233-3.033-19.127-6.155-29.374-5.916 1.366-11.628 3.157-17.908 5.47 2.835 9.25 4.69 17.978 5.5 25.81 1.007 9.74.403 18.06-1.375 24.033-1.777 5.972-4.412 9.338-7.718 11.25-3.308 1.91-7.543 2.505-13.595 1.06-6.052-1.443-13.55-5.07-21.47-10.81-15.838-11.482-33.334-31.223-47.624-56-14.29-24.78-22.61-49.838-24.624-69.314-1.006-9.738-.403-18.027 1.375-24 1.78-5.973 4.414-9.37 7.72-11.28 1.653-.957 3.52-1.58 5.75-1.782.557-.05 1.147-.067 1.75-.063 1.81.014 3.824.272 6.094.814 6.05 1.444 13.518 5.04 21.437 10.78 8.218 5.957 16.884 14.166 25.314 24.126 5.07-4.055 9.34-8.25 13.406-13.188-8.97-10.46-18.366-19.26-27.75-26.062-9.354-6.78-18.733-11.61-28.094-13.844a46.242 46.242 0 0 0-7.03-1.125 43.882 43.882 0 0 0-2.657-.124zm7.967 34.188c-.438.024-.86.063-1.28.125-17.327 2.54-14.97 36.035 5.03 74.56 20 38.528 50.52 67.576 67.844 65.032 5.995-.88 9.263-5.687 10.658-12.875-1.67 2.187-3.847 3.484-6.72 3.907-15.316 2.248-42.316-23.093-60-57.156-17.682-34.064-19.254-63.91-3.937-66.157 3.638-.53 7.69.417 12.345 2.782-8.68-6.8-17.353-10.583-23.938-10.218zM102 325.124c-16.555 1.844-35.304 3.26-57.063 4.376-49.128 2.523-25.555 71.576 30.094 52.22-49.365 29.9 3.86 86.927 26.97 46.75 10.362-18.016 21.88-33.79 34.375-47.814-8.42-9.644-16.078-20.224-22.563-31.47a200.715 200.715 0 0 1-11.812-24.06z',
    water: 'M406.043 316c24.11 96.443-50.59 180-150 180s-174.405-82.38-150-180c15-60 90-150 150-300 60 150 135 240 150 300z',
    power: 'm192.063 20.375-44.625 98.563-36.344-13.657 40.312 47.22 29.47-50.656 17.093 159.437 88.874-159.936 7.906 138.22 74.72-140.408 32.905 9.094-32.594-57.28-58.75 91.343L300.657 27.28 216.75 153.657l-24.688-133.28zM61.25 100.813c-22.213 0-40.03 17.785-40.03 40a39.896 39.896 0 0 0 40.03 40.03c.558 0 1.104-.04 1.656-.062l19.5 72.814-27.812 7.437 4.843 18.064 27.813-7.47 6.656 24.876-27.812 7.438L70.938 322l27.812-7.438 7.156 26.782-27.78 7.437 4.81 18.033 27.814-7.438 6.906 25.813-27.78 7.437 4.843 18.063 27.78-7.438 7.188 26.75-27.813 7.47 4.844 18.06 27.81-7.467 12.25 45.78h19.345l-13.563-50.593 27.813-7.47-4.844-18.06-27.81 7.467-7.157-26.78 27.78-7.438-4.812-18.064-27.81 7.438-6.907-25.813 27.78-7.436-4.812-18.063-27.81 7.47-7.157-26.78 27.78-7.44-4.843-18.06-27.78 7.436-6.657-24.875 27.78-7.436-4.843-18.03-27.78 7.436-19.564-73.03c12.173-6.842 20.344-19.858 20.344-34.907 0-22.215-17.787-40-40-40zm391.188 0c-22.213 0-40.032 17.785-40.032 40 0 15.052 8.19 28.066 20.375 34.906l-19.56 73.03-27.814-7.438-4.844 18.032 27.813 7.47-6.656 24.842-27.814-7.437-4.844 18.06 27.813 7.44-7.156 26.78-27.814-7.47-4.844 18.064 27.813 7.437-6.906 25.814-27.814-7.438-4.844 18.063 27.813 7.436-7.156 26.78-27.845-7.467-4.813 18.06 27.813 7.47-13.563 50.594h19.313l12.25-45.78 27.813 7.467 4.843-18.06-27.81-7.47 7.186-26.75 27.813 7.438 4.842-18.063L396 385.187l6.906-25.812 27.813 7.438 4.842-18.032-27.812-7.467 7.156-26.75L442.72 322l4.842-18.063-27.812-7.437 6.656-24.875 27.813 7.47 4.842-18.064-27.812-7.436 19.5-72.813c.562.024 1.12.064 1.688.064 22.212 0 40-17.817 40-40.03 0-22.216-17.788-40-40-40z',
    defense: 'M256 20c-64 96-128 88-192 104 0 256 64 320 192 368 128-48 192-112 192-368-64-16-128-8-192-104zm0 32a9 9 0 1 1 0 18 9 9 0 0 1 0-18zm.291 30.152a269.17 269.17 0 0 0 13.84 14.15c34.949 35.385 82.966 45.887 130.685 56.428 2.732 1.184 6.62.508 8.864 1.958-2.683 64.84-5.522 131.649-32.315 191.746-12.685 31.46-36.26 57.602-64.37 76.144-15.452 10.864-32.847 18.227-49.884 26.115-6.586 4.573-13.697-.448-20.242-2.46-43.648-17.847-84.444-48.619-105.056-92.266-29.065-62.018-32.949-131.823-35.815-199.393 57.423-8.808 112.578-27.8 152.441-70.418zM256 124l-27.512 68.78C211.228 189.067 190.45 182.15 160 172c0 32-16 48-32 64 0 16 64 32 80 32 13.173-8.782 22.72-15.138 30.646-19.1C234.19 277.441 218.65 337.35 192 364c32 16 48 32 64 48 16-16 32-32 64-48-26.65-26.65-42.19-86.559-46.646-115.1 7.926 3.962 17.473 10.318 30.646 19.1 16 0 80-16 80-32-16-16-32-32-32-64-30.45 10.15-51.228 17.068-68.488 20.78zm-172 8a9 9 0 1 1 0 18 9 9 0 0 1 0-18zm344 0a9 9 0 1 1 0 18 9 9 0 0 1 0-18zM96 282a9 9 0 1 1 0 18 9 9 0 0 1 0-18zm320 0a9 9 0 1 1 0 18 9 9 0 0 1 0-18zM160 410a9 9 0 1 1 0 18 9 9 0 0 1 0-18zm192 0a9 9 0 1 1 0 18 9 9 0 0 1 0-18zm-96 50a9 9 0 1 1 0 18 9 9 0 0 1 0-18z',
    beds: 'M239.802 74.44v.19h12.275v29.433h.004v31.265a85.275 85.275 0 0 0-10.095-1.084c-26.8-1.293-55.033 8.738-73.23 13.36l-7.545 1.92.582 5.564c-.46-.176-.917-.356-1.387-.525l-.705-.256-.74-.135c-4.097-.747-8.27-1.193-12.48-1.39-29.477-1.372-60.834 9.463-81.174 14.523l-7.612 1.892.836 7.8c.605 5.644 1.218 11.59 2.774 17.493-10.642 13.072-10.078 18.35-8.417 27.184l211.14 73.916v74.053C184.03 336.45 106.252 295.828 25.582 264.49v-170h18v.125h12.374v34.77l165.848-21.414V74.44zm-2.088 77.845c1.203-.013 2.39 0 3.564.04 2.818.095 5.505.396 8.09.84 13.548 5.197 20.296 12.637 24.25 21.462-23.255 9.644-44.174 13.507-62.515 15.736-5.277-1.15-9.503-2.466-12.944-3.894a63.312 63.312 0 0 0-16.522-20.16 91.603 91.603 0 0 1-.584-3.33c17.414-4.63 38.614-10.504 56.66-10.695zm-94.35 18.528c1.38-.014 2.743 0 4.09.046a69.18 69.18 0 0 1 9.26.95c15.757 5.89 23.546 14.435 28.002 24.526-26.44 10.85-50.22 15.162-70.965 17.62-17.42-3.692-25.116-8.99-29.17-14.665-3.072-4.302-4.524-9.753-5.53-16.518 19.495-5.077 43.62-11.753 64.314-11.96zM291.8 186.295l26.406 7.453c-59.194 10.41-125.095 28.732-165.18 45.766l-27.443-9.17c21.235-3.146 45.785-8.753 72.568-20.846l5.29-2.39c1.72.44 3.5.853 5.35 1.232l1.42.29 1.44-.17c21.562-2.54 47.905-7.294 77.15-20.782zm68.797 19.418 51.336 14.49-147.905 38.377v17.6l-82.517-27.147-1.77-.59c49.176-17.717 124.438-36.303 180.857-42.73zm127.79 13.68v90.57L283.69 372.127v-99.62zM23.613 282.45 60.837 299v14.674L39.98 322.13l-16.366-10.57zm463.26 49.243v34.995l-21.91 9.515-16.367-7.4v-25.487zm-234.453 52.49 11.608 5.16 9.442 4.196 19.342-6.87v40.848l-22.704 10.043-17.687-12.685z',
};
function statSvg(key) {
    const d = STAT_ICON_PATH[key];
    return d ? `<svg viewBox="0 0 512 512" aria-hidden="true"><path fill="currentColor" d="${d}"/></svg>` : '';
}

function toggleFav(r) {
    // Must match the existing serialized favorite key; verify this against src/CobjCacheSerialization.cpp:RecipeJson.
    const k = keyOf(r); if (!k) return;
    const owned = isFav(r);
    favs.delete(k);
    if (r.legacyFirst !== false) favs.delete(String(r.formRef));
    if (!owned) favs.add(k);
    saveFavs(); renderFavCount();
    if (favView) { requestCatalogPage(catalogPage); return !owned; }
    const i = currentView().findIndex(x => keyOf(x) === k);
    const star = document.querySelectorAll('#strip .card')[i]?.querySelector('.fav');
    if (star) star.classList.toggle('on', favs.has(k));
    return !owned;
}
function renderFavCount() {
    const c = document.getElementById('fav-count'); if (c) c.textContent = String(favs.size);
    const t = document.getElementById('fav-tab'); if (t) t.classList.toggle('active', favView);
}
function toggleFavView() {
    favView = !favView;
    currentCategory = 'All'; currentSub = 'All';
    resetCatalogNavigation();
    renderFavCount(); requestCatalogPage(0);
}

let _scalePct = 100;
let _scaleSettle = [];
function applyScale(pct) {
    const p = Math.max(50, Math.min(200, parseInt(pct, 10) || 100));
    _scalePct = p;

    document.documentElement.style.setProperty('--ui-user', String(p / 100));

    if (selectedItem) setTimeout(() => showLivePreview(selectedItem), 30);
    requestAnimationFrame(syncCardPreviews);
    requestAnimationFrame(reportPanelRects);
    _scaleSettle.forEach(clearTimeout);
    _scaleSettle = [120, 300].map(ms => setTimeout(syncCardPreviews, ms));
    return p;
}
window.setUiScale = pct => applyScale(pct);

const WS_THEMES = ['tarkov', 'pipboy'];
const WS_THEME_LABEL = { tarkov: 'TARKOV', pipboy: 'PIP-BOY' };
let _wsTheme = 'tarkov';

function applyWsTheme(name, noPersist) {
    _wsTheme = WS_THEMES.includes(name) ? name : 'tarkov';
    WS_THEMES.forEach(x => document.body.classList.toggle('theme-' + x, x === _wsTheme));
    const b = document.getElementById('themeBtn');
    if (b) pcSetText(b, 'THEME: {{theme}}', {theme: pcText(WS_THEME_LABEL[_wsTheme] || _wsTheme.toUpperCase())});
    if (!noPersist && window.saveTheme) { try { window.saveTheme(_wsTheme); } catch (e) {} }
    requestAnimationFrame(syncCardPreviews);
}
function cycleTheme() {
    applyWsTheme(WS_THEMES[(WS_THEMES.indexOf(_wsTheme) + 1) % WS_THEMES.length]);
}
window.setTheme = name => applyWsTheme(name, true);
applyWsTheme('tarkov', true);
function fsStep(dir) {
    const next = applyScale(_scalePct + dir * 10);
    if (typeof window.saveUiScale === 'function') window.saveUiScale(String(next));
}

function requestCatalogPage(page, selectLast = false) {
    if (!catalogSession || typeof window.requestWorkshopPage !== 'function') return;
    pendingNavigationEnd = selectLast;
    const request = {
        session: catalogSession, requestId: ++catalogRequestId,
        search: currentSearch, category: currentCategory, sub: currentSub,
        filter: filterMode, page: Math.max(0, page), pageSize: WORKSHOP_RENDER_LIMIT,
        favoritesOnly: favView, favorites: favView ? [...favs].map(String) : []
    };
    window.requestWorkshopPage(JSON.stringify(request));
}
function commitBuildables(data) {
    _statsPoll = 0; _statsLastJson = ''; _cancelStatsPoll();
    catalogPage = data.page;
    catalogPageCount = data.pageCount;
    catalogFacetTotal = data.facetTotal;
    catalogCategories = data.categories;
    catalogFavoriteCategories = data.favoriteCategories;
    allItems = data.recipes;
    pendingBuildables = '';
    allItems.forEach(r => { r.__cat = catOf(r); r.__sub = subOf(r); r.__disp = dName(r); });

    selectedItem = allItems.find(r => keyOf(r) === pendingSelectionKey) || null;
    pendingSelectionKey = '';
    const sc = document.getElementById('siteCount');
    if (sc) pcSetText(sc, '{{count}} units', {count: catalogFacetTotal});
    renderFavCount(); renderCats(); renderSubs(); renderStrip();
    if (selectedItem) paintDetail(selectedItem);
    else if (currentView().length) {
        const item = pendingNavigationEnd ? currentView()[currentView().length - 1] : currentView()[0];
        selectItem(item);
    }
    pendingNavigationEnd = false;
    console.log(`[WS] workshop page ${catalogPage + 1}/${catalogPageCount}: ${allItems.length} items`);
    if (typeof workshopFocusRing === 'function' && workshopRingShown) workshopFocusRing();
}
window.beginBuildables = arg => {
    let meta;
    try { meta = JSON.parse(arg); } catch (e) { return; }
    if (!meta || !Number.isSafeInteger(meta.session) || !Number.isSafeInteger(meta.requestId) ||
        !Number.isSafeInteger(meta.serial) || !Number.isSafeInteger(meta.chunks) ||
        meta.session < catalogSession || meta.chunks < 1 || meta.chunks > 256) return;
    if (meta.session > catalogSession) {
        catalogSession = meta.session;
        catalogRequestId = 0;
        catalogPage = 0;
        catalogPageCount = 0;
        lastBuildableSerial = 0;
    }
    if (meta.requestId !== catalogRequestId || meta.serial <= lastBuildableSerial) return;
    lastBuildableSerial = meta.serial;
    activeBuildableSerial = String(meta.serial);
    activeBuildableRequestId = meta.requestId;
    pendingSelectionKey = keyOf(selectedItem);
    allItems = [];
    selectedItem = null;
    navIndex = -1;
    hideLivePreview();
    refreshMaterials(null);
    const sc = document.getElementById('siteCount');
    if (sc) pcSetText(sc, '{{count}} units', {count: 0});
    renderFavCount(); renderCats(); renderSubs(); renderStrip();
    pendingBuildables = '';
    expectedBuildableChunks = meta.chunks;
    receivedBuildableChunks = 0;
    buildableDeliveryFailed = false;
};
window.appendBuildables = arg => {
    if (buildableDeliveryFailed || !activeBuildableSerial ||
        activeBuildableRequestId !== catalogRequestId || typeof arg !== 'string') return;
    const sep = arg.indexOf(':');
    if (sep < 0 || arg.slice(0, sep) !== activeBuildableSerial) return;
    pendingBuildables += arg.slice(sep + 1);
    if (pendingBuildables.length > 8 * 1024 * 1024) buildableDeliveryFailed = true;
    ++receivedBuildableChunks;
};
window.finishBuildables = serial => {
    if (!activeBuildableSerial || String(serial) !== activeBuildableSerial) return;
    activeBuildableSerial = '';
    if (activeBuildableRequestId !== catalogRequestId) return;
    if (buildableDeliveryFailed || receivedBuildableChunks !== expectedBuildableChunks) {
        console.error(`[WS] discarded incomplete workshop page: expected ${expectedBuildableChunks} chunk(s), received ${receivedBuildableChunks}`);
        pendingBuildables = '';
        if (typeof window.showError === 'function') {
            window.showError('Workshop page failed to load. Refresh the cache.');
        }
        return;
    }
    let data;
    try { data = JSON.parse(pendingBuildables); } catch (e) { data = null; }
    if (!data || !Array.isArray(data.recipes) || data.recipes.length > WORKSHOP_RENDER_LIMIT ||
        !Array.isArray(data.categories) || !Array.isArray(data.favoriteCategories) ||
        !Number.isSafeInteger(data.total) ||
        !Number.isSafeInteger(data.page) || !Number.isSafeInteger(data.pageCount) ||
        !Number.isSafeInteger(data.facetTotal)) {
        if (typeof window.showError === 'function') window.showError('Workshop page failed to load. Refresh the cache.');
        return;
    }
    commitBuildables(data);
};
window.setPlacingMode = flag => {
    isPlacing = (String(flag) === '1');
    const placeButton = document.getElementById('act-place');
    if (placeButton) placeButton.disabled = false;
    if (isPlacing) {
        const q = document.getElementById('q'); if (q) q.blur();

    } else {
        requestAnimationFrame(syncCardPreviews);
    }
    if (selectedItem) showLivePreview(selectedItem);
};


let _statsPoll = 0;
let _statsPollTimer = 0;
let _statsLastJson = '';
const _STATS_POLL_MAX = 12;
function _cancelStatsPoll() {
    if (_statsPollTimer) { clearTimeout(_statsPollTimer); _statsPollTimer = 0; }
}
window.refreshWorkshopStats = () => {
    _statsPoll = 0;
    _statsLastJson = '';
    _cancelStatsPoll();
    try { if (window.requestWorkshopStats) window.requestWorkshopStats(''); } catch (e) {}
};
window.setWorkshopStats = arg => {
    const bar = document.getElementById('statbar'); if (!bar) return;
    const raw = (typeof arg === 'string') ? arg : JSON.stringify(arg || {});
    let s;
    try { s = JSON.parse(raw); } catch (e) { s = {}; }

    if (!s || typeof s.population !== 'number' || s.population < 0) { bar.hidden = true; return; }

    _cancelStatsPoll();
    const settled = s.population > 0 && raw === _statsLastJson;
    _statsLastJson = raw;
    if (!settled && _statsPoll < _STATS_POLL_MAX) {
        _statsPoll++;
        _statsPollTimer = setTimeout(() => {
            _statsPollTimer = 0;
            try { if (window.requestWorkshopStats) window.requestWorkshopStats(''); } catch (e) {}
        }, 800);
    }

    renderWorkshopStats(s);
};
function renderWorkshopStats(s) {
    const bar = document.getElementById("statbar");
    const pop = s.population, unassigned = Math.max(0, s.unassigned || 0);

    const need = { food: pop, water: pop, beds: pop };
    const defenseNeed = Math.max(0, (s.food || 0)) + Math.max(0, (s.water || 0));
    let html = '';
    STAT_DEFS.forEach(d => {
        let v = s[d.key];
        if (typeof v !== 'number' || v < 0) return;
        let cls = 'ok', txt, ico = null;
        if (d.pct) {
            txt = v + '%';
            cls = v >= 80 ? 'ok' : (v >= 50 ? 'warn' : 'bad');

            const happy = v >= 60;
            ico = `<span class="face ${happy ? 'face-happy' : 'face-sad'}"></span>`;
        } else if (d.key === 'population') {
            txt = String(v) + (unassigned ? ` <i>${esc(pcText('({{count}} idle)', {count: unassigned}))}</i>` : '');
        } else if (d.key === 'power') {
            txt = String(v);
        } else if (d.key === 'defense') {
            txt = String(v);
            cls = v >= defenseNeed ? 'ok' : 'bad';
        } else {
            const n = need[d.key];
            txt = `${v}/${n}`;
            cls = v >= n ? 'ok' : 'bad';
        }
        html += `<div class="stat ${cls}" title="${esc(pcText(d.label))}">` +
                `<span class="s-ic">${ico || statSvg(d.key)}</span>` +
                `<span class="s-lb">${esc(pcText(d.label))}</span>` +
                `<span class="s-vl">${txt}</span></div>`;
    });
    bar.innerHTML = html;
    bar.hidden = !html;
}
let _errTimer;
function showMatsLine(cls, msg, vars) {
    clearTimeout(_errTimer);
    const b = document.getElementById('materials-box'); if (!b) return;
    b.innerHTML = `<span class="${cls}">${esc(pcText(msg, vars))}</span>`;
    _errTimer = setTimeout(() => {
        selectedItem ? refreshMaterials(selectedItem) : (b.innerHTML = `<span class="dimtxt">${esc(pcText("Select an item."))}</span>`);
    }, 3000);
}
window.showError = msg => showMatsLine('err', msg);
window.showHudNotice = arg => {
    let d; try { d = typeof arg === 'string' ? JSON.parse(arg) : arg; } catch (e) { return; }
    if (d && typeof d.key === 'string') showMatsLine('notice', d.key, d.vars);
};

function selectItem(item) {
    selectedItem = item;
    navIndex = currentView().findIndex(r => keyOf(r) === keyOf(item));
    markSelection(navIndex);
    paintDetail(item);
}

function actPlace() {
    if (isPlacing) {
        if (typeof window.prisma_place === 'function') window.prisma_place('');
        return;
    }
    const placeIndex = validPlacementSelectionIndex(currentView(), selectedItem, navIndex);
    if (placeIndex < 0) { navIndex = -1; return; }
    navSelect(placeIndex, true);
}

function actCancel() {
    if (typeof window.prisma_cancel === 'function') window.prisma_cancel('');
}

function actScrap() {
    if (typeof window.prisma_scrap === 'function') window.prisma_scrap('');
}

function actTag(desired) {
    if (!selectedItem || typeof window.prisma_tag !== 'function') return;
    const state = desired === true ? ',1' : desired === false ? ',0' : '';
    window.prisma_tag(String(selectedItem["form" + "Id"]) + state);
}
function closeHelper() {

    hideCardPreviews();
    hideLivePreview();
    if (typeof window.prisma_close === 'function') window.prisma_close('');
}

let _searchTimer = null;
document.getElementById('q').addEventListener('input', e => {
    currentSearch = e.target.value;
    document.querySelector('.search')?.classList.toggle('has', !!currentSearch);
    clearTimeout(_searchTimer);
    _searchTimer = setTimeout(() => { resetCatalogNavigation(); requestCatalogPage(0); }, 180);
});

(() => {
    const q = document.getElementById('q'); if (!q) return;
    const push = v => { if (typeof window.prisma_text_focus === 'function') window.prisma_text_focus(v); };
    q.addEventListener('focus', () => push('1'));
    q.addEventListener('blur', () => push('0'));
})();
document.getElementById('refresh-cache-btn').addEventListener('click', () => {
    if (typeof window.requestBuildables === 'function') window.requestBuildables('');
});
function navSelect(i, ghost) {
    if (ghost && isPlacing) return;
    const view = currentView(); if (!view.length) return;
    if (i < 0 && catalogPage > 0) { requestCatalogPage(catalogPage - 1, true); return; }
    if (i >= view.length && catalogPage + 1 < catalogPageCount) {
        requestCatalogPage(catalogPage + 1); return;
    }
    if (i < 0) i = 0; if (i >= view.length) i = view.length - 1;
    navIndex = i;
    const item = view[i];
    selectedItem = item;
    markSelection(i);
    paintDetail(item);

    if (!ghost) return;
    if (!isCraftable(item)) {
        if (typeof window.showError === 'function') window.showError(item.locked ? 'Locked' : 'Not enough materials');
        return;
    }
    if (typeof window.prisma_build_item === 'function')
        window.prisma_build_item(`${item.formRef},${item.created}`);
}
function cycleCategory(dir) {
    if (favView) return;
    const cats = ['All'].concat(catalogCategories.map(c => c.cat));
    let i = cats.indexOf(currentCategory); if (i < 0) i = 0;
    currentCategory = cats[(i + dir + cats.length) % cats.length];
    currentSub = 'All'; resetCatalogNavigation();
    requestCatalogPage(0);
}
function toggleControls() {
    const c = document.getElementById('controls');
    if (!c) return;
    const open = c.classList.toggle('open');
    c.querySelector('.ctl-toggle')?.setAttribute('aria-expanded', open ? 'true' : 'false');
}
window.toggleControls = toggleControls;
window.setFreeState = arg => {
    let d; try { d = typeof arg === 'string' ? JSON.parse(arg) : (arg || {}); } catch (e) { return; }
    const set = (id, v) => {
        const el = document.getElementById(id);
        if (el) { pcSetText(el, v ? 'ON' : 'off'); el.classList.toggle('on', !!v); }
    };
    set('st-place', d.place);
    set('st-cam', d.cam);
    window.workshopFreeCam = !!d.cam;

    if ('gsnap' in d) set('st-snap', !d.gsnap);
    if ('osnap' in d) set('st-osnap', d.osnap);
};

(function init() {
    loadFavs();
    applyScale(100);
    renderFavCount();

    const dock = document.getElementById('dock');
    if (dock) {
        let queued = false;
        dock.addEventListener('animationend', () => {
            if (queued) return;
            queued = true;
            requestAnimationFrame(() => { queued = false; syncCardPreviews(); reportPanelRects(); });
        });
    }
    function ensureDockOnScreen() {
        const d = document.getElementById('dock');
        if (!d) return;
        const vw = window.innerWidth, vh = window.innerHeight;
        if (vw < 2 || vh < 2) return;
        const r = d.getBoundingClientRect();
        const off = r.bottom <= 0 || r.top >= vh || r.right <= 0 || r.left >= vw;
        const tiny = r.width < 40 || r.height < 40;
        if (off || tiny) {
            document.documentElement.style.setProperty('--ui-user', '1');
            d.style.position = 'absolute';
            d.style.left = '0'; d.style.right = '0'; d.style.bottom = '0';
            d.style.top = 'auto'; d.style.transform = 'none';
            reportPanelRects();
            console.log('[WS] dock was off-screen or collapsed; reset to safe bottom dock');
        }
    }
    function ready() {
        ensureDockOnScreen();
        reportPanelRects();
    }
    window.addEventListener('resize', () => setTimeout(ensureDockOnScreen, 60));
    if (document.readyState === 'complete') ready(); else window.addEventListener('load', () => setTimeout(ready, 30));
    console.log('[WS] workshop.js loaded (bottom dock, two-level categories, favorites)');
})();

window.refreshTranslations = () => {
    allItems.forEach(r => { r.__disp = dName(r); });
    renderCats(); renderSubs(); renderStrip();
    if (selectedItem) paintDetail(selectedItem);
    pcSetText(document.getElementById('themeBtn'), 'THEME: {{theme}}', {theme: pcText(WS_THEME_LABEL[_wsTheme])});
    if (_statsLastJson) renderWorkshopStats(JSON.parse(_statsLastJson));
    if (window.workshopPromptData) window.setWorkshopPrompts(window.workshopPromptData);
    if (scrapTranslationData && isScrapOverlayOpen()) renderScrapConfirm(scrapTranslationData);
};
