function releasePanelFocus() {
    if (typeof window.prisma_release_focus === 'function') window.prisma_release_focus('');
}

window.controllerScrap = actScrap;
window.controllerFavorite = () => {
    if (!selectedItem) return;
    toggleFav(selectedItem);
    actTag();
};

const WORKSHOP_ZONES = ['top', 'cats', 'subs', 'filters', 'strip'];
const WORKSHOP_ZONE_SELECTOR = {
    top: '.rail-right button',
    cats: '#cat-row .cat',
    subs: '#sub-row .sub',
    filters: '.lk-group .lk-btn'
};
let workshopZone = 'strip';
let workshopTopIndex = 0;

function workshopZoneItems(zone) {
    const selector = WORKSHOP_ZONE_SELECTOR[zone];
    if (!selector || typeof document.querySelectorAll !== 'function') return [];
    return Array.from(document.querySelectorAll(selector)).filter(el => !el.disabled && el.offsetParent !== null);
}

function workshopActiveIndex(items) {
    return items.findIndex(el => el.classList.contains('active'));
}

function workshopFocusRing() {
    const marked = typeof document.querySelectorAll === 'function'
        ? document.querySelectorAll('.controller-focus') : [];
    Array.from(marked).forEach(el => el.classList.remove('controller-focus'));
    let el = null;
    if (workshopZone === 'strip') el = document.querySelector('#strip .card.sel');
    else {
        const items = workshopZoneItems(workshopZone);
        el = workshopZone === 'top' ? items[workshopTopIndex] : items[workshopActiveIndex(items)];
    }
    if (!el || typeof el.classList.add !== 'function') return;
    el.classList.add('controller-focus');
    if (typeof el.scrollIntoView === 'function') el.scrollIntoView({ block: 'nearest', inline: 'nearest' });
}

function workshopEnterZone(zone) {
    workshopZone = zone;
    if (zone === 'top') workshopTopIndex = Math.min(workshopTopIndex, workshopZoneItems('top').length - 1);
    if (zone === 'strip' && navIndex < 0) navSelect(0, false);
    workshopFocusRing();
}

function workshopMoveZone(dir) {
    for (let i = WORKSHOP_ZONES.indexOf(workshopZone) + dir; i >= 0 && i < WORKSHOP_ZONES.length; i += dir) {
        const zone = WORKSHOP_ZONES[i];
        if (zone !== 'strip' && !workshopZoneItems(zone).length) continue;
        workshopEnterZone(zone);
        return;
    }
}

let workshopNavStreak = 0;
const WORKSHOP_NAV_FAST_AFTER = 10;
const WORKSHOP_NAV_FAST_STEP = 3;

function workshopStepWithinZone(dir, state) {
    if (workshopZone === 'strip') {
        workshopNavStreak = state === 'repeat' ? workshopNavStreak + 1 : 0;
        const step = workshopNavStreak > WORKSHOP_NAV_FAST_AFTER ? WORKSHOP_NAV_FAST_STEP : 1;
        navSelect(navIndex < 0 ? 0 : Math.max(0, navIndex + dir * step), false);
    } else if (workshopZone === 'top') {
        const count = workshopZoneItems('top').length;
        workshopTopIndex = Math.max(0, Math.min(count - 1, workshopTopIndex + dir));
    } else {
        const items = workshopZoneItems(workshopZone);
        const next = items[Math.max(0, Math.min(items.length - 1, workshopActiveIndex(items) + dir))];
        if (next && !next.classList.contains('active')) next.click();
    }
    workshopFocusRing();
}

function workshopNavAction(action, state) {
    if (action === 'nav.up') workshopMoveZone(-1);
    else if (action === 'nav.down') workshopMoveZone(1);
    else if (action === 'nav.left') workshopStepWithinZone(-1, state);
    else if (action === 'nav.right') workshopStepWithinZone(1, state);
    else return false;
    return true;
}

function workshopConfirm() {
    if (isPlacing || workshopZone === 'strip') { actPlace(); return; }
    if (workshopZone === 'top') {
        const el = workshopZoneItems('top')[workshopTopIndex];
        if (el) el.click();
        workshopFocusRing();
        return;
    }
    workshopEnterZone('strip');
}

function workshopBack() {
    if (isPlacing) actCancel();
    else if (workshopZone !== 'strip') workshopEnterZone('strip');
    else closeHelper();
}

const WORKSHOP_TAB_IDS = ['fav-tab', 'themeBtn'];

function workshopTabStops() {
    const stops = workshopZoneItems('top').map((el, index) => ({ el, zone: 'top', index }))
        .filter(stop => WORKSHOP_TAB_IDS.includes(stop.el.id));
    ['cats', 'filters'].forEach(zone =>
        workshopZoneItems(zone).forEach((el, index) => stops.push({ el, zone, index })));
    return stops;
}

function workshopStepTab(dir) {
    const stops = workshopTabStops();
    if (!stops.length) return;
    const on = stop => stop.zone === 'top' ? stop.index === workshopTopIndex : stop.el.classList.contains('active');
    let at = stops.findIndex(stop => stop.zone === workshopZone && on(stop));
    if (at < 0) at = stops.findIndex(stop => stop.zone === 'cats' && on(stop));
    const stop = stops[at < 0 ? (dir > 0 ? 0 : stops.length - 1) : (at + dir + stops.length) % stops.length];
    workshopZone = stop.zone;
    if (stop.zone === 'top') workshopTopIndex = stop.index;
    else if (!stop.el.classList.contains('active')) stop.el.click();
    workshopFocusRing();
}

const workshopFreedomHeld = { rotate: false, move: false };

const WORKSHOP_FREEDOM_MOD = {
    rotate: { 'nav.left': 'r0-', 'nav.right': 'r0+', 'nav.up': 'r1+', 'nav.down': 'r1-',
              'workshop.shoulder_left': 'r2-', 'workshop.shoulder_right': 'r2+', 'workshop.snap': 'osnap' },
    move: { 'nav.up': 'm0+', 'nav.down': 'm0-', 'nav.left': 'm1-', 'nav.right': 'm1+',
            'workshop.shoulder_left': 'm2-', 'workshop.shoulder_right': 'm2+', 'workshop.snap': 'reset' },
    none: { 'workshop.snap': 'snap', 'workshop.free_place': 'place', 'workshop.free_cam': 'cam' }
};

function workshopFreedomAction(action, state) {
    if (action === 'workshop.mod_rotate' || action === 'workshop.mod_move') {
        workshopFreedomHeld[action === 'workshop.mod_rotate' ? 'rotate' : 'move'] = state !== 'released';
        return true;
    }
    if (action === 'workshop.panel_focus' && window.workshopFreeCam) {
        if (state === 'pressed' && typeof window.prisma_freedom === 'function') window.prisma_freedom('cam');
        return true;
    }
    const mode = workshopFreedomHeld.rotate ? 'rotate' : (workshopFreedomHeld.move ? 'move' : 'none');
    const cmd = WORKSHOP_FREEDOM_MOD[mode][action];
    if (!cmd) return false;
    const nudge = cmd.length === 3 && (cmd[0] === 'r' || cmd[0] === 'm');
    if (state === 'pressed' || (nudge && state === 'repeat')) {
        if (typeof window.prisma_freedom === 'function') window.prisma_freedom(cmd);
        if (cmd === 'cam') releasePanelFocus();
    }
    return true;
}

function workshopHandleAction(action, state) {
    if (isScrapOverlayOpen()) {
        if (state !== 'pressed') return;
        if (action === 'workshop.panel_focus' || action === 'workshop.back') actScrapCancel();
        else if (action === 'workshop.place') actScrapConfirm();
        return;
    }
    if (workshopFreedomAction(action, state)) return;
    if (state === 'pressed') {
        if (action === 'workshop.back') { workshopBack(); return; }
        if (action === 'workshop.panel_focus') { releasePanelFocus(); return; }
        if (action === 'workshop.scrap') { window.controllerScrap(); return; }
        if (action === 'workshop.favorite') { window.controllerFavorite(); return; }
        if (action === 'workshop.place') { workshopConfirm(); return; }
        if (action === 'workshop.shoulder_left') { workshopStepTab(-1); return; }
        if (action === 'workshop.shoulder_right') { workshopStepTab(1); return; }
    }
    if (state === 'pressed' || state === 'repeat') workshopNavAction(action, state);
}

window.addEventListener('prisma-controller-action', event => {
    const detail = event && event.detail;
    if (detail) workshopHandleAction(detail.action, detail.state);
});

window.workshopUnfocusedAction = action => {
    workshopFreedomHeld.rotate = false;
    workshopFreedomHeld.move = false;
    workshopHandleAction(String(action || ''), 'pressed');
};

window.addEventListener('prisma-input-device-change', () => {
    if (typeof window.refreshPrompts === 'function') window.refreshPrompts('');
});

function workshopKeys(tokens, ps) {
    const list = Array.isArray(tokens) ? tokens : [tokens];
    return list.map(token => window.PrismaCG.renderKey(token, ps)).join('');
}

window.setWorkshopPrompts = arg => {
    let d; try { d = typeof arg === 'string' ? JSON.parse(arg) : (arg || {}); } catch (e) { return; }
    if (!window.PrismaCG) return;
    window.workshopPromptData = d;
    const ps = !!d.ps;
    (d.items || []).forEach(item => {
        const key = document.querySelector('#' + item.id + ' .k');
        const label = document.querySelector('#' + item.id + ' .l');
        if (key) {
            key.innerHTML = workshopKeys(item.t, ps);
            if (key.classList) key.classList.toggle('k-gp', key.innerHTML.indexOf('cg-img') >= 0);
        }
        pcSetText(label, item.l);
    });
    const hint = document.getElementById('controller-hint');
    if (hint) hint.innerHTML = (d.nav || []).map(item =>
        workshopKeys(item.t, ps) + ' ' + esc(pcText(item.l))).join(' <i>/</i> ');
};

document.addEventListener('keydown', e => {
    if (isScrapOverlayOpen()) {
        if (e.key === 'Enter') actScrapConfirm();
        else if (e.key === 'Escape' || e.key === 'Tab') actScrapCancel();
        else { e.preventDefault(); return; }
        e.preventDefault();
        e.stopImmediatePropagation();
        return;
    }
    if (e.key === 'Escape' || e.key === 'Tab') {
        if (isPlacing) actCancel();
        else if (e.key === 'Escape') releasePanelFocus();
        else closeHelper();
        e.preventDefault();
        e.stopImmediatePropagation();
        return;
    }

    const ae = document.activeElement;
    if (ae && (ae.tagName === 'INPUT' || ae.tagName === 'TEXTAREA')) return;
    if (e.ctrlKey || e.altKey || e.metaKey) return;
    const k = (e.key || '').toLowerCase();
    if (k === 'a' || e.key === 'ArrowLeft') navSelect(navIndex <= 0 ? 0 : navIndex - 1, false);
    else if (k === 'd' || e.key === 'ArrowRight') navSelect(navIndex < 0 ? 0 : navIndex + 1, false);
    else if (k === 'w' || e.key === 'ArrowUp') cycleCategory(-1);
    else if (k === 's' || e.key === 'ArrowDown') cycleCategory(1);
    else if (k === 'f' && selectedItem) toggleFav(selectedItem);
    else if (k === 'q' && selectedItem) actTag();
    else if (k === 'e' || k === 'enter') actPlace();
    else return;
    e.preventDefault();
});
