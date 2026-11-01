let craftNavZone = 'recipes';
let craftNavIndex = 0;
let craftStationIndex = 0;

function craftNavItems(zone) {
  const selectors = {
    header: '.hdr-right button:not([disabled])',
    filters: '.filter-wrap button:not([disabled])',
    categories: '#catRail .cr-row',
    recipes: '#grid .card',
    pager: '#pager button:not([disabled])',
    detail: '#detail button:not([disabled]), #detail input:not([disabled])',
    stations: '#wbList .wb-li',
    stationAction: '#wbDetail .wb-up-action-btn:not([disabled])',
    modal: '#cheatModal .modal-btn'
  };
  return Array.from(document.querySelectorAll(selectors[zone] || '')).filter(el => el.offsetParent !== null);
}

function setCraftNav(zone, index) {
  const items = craftNavItems(zone);
  if (!items.length) return false;
  document.querySelectorAll('.controller-focus').forEach(el => el.classList.remove('controller-focus'));
  craftNavZone = zone;
  craftNavIndex = Math.max(0, Math.min(items.length - 1, index));
  if (zone === 'stations') craftStationIndex = craftNavIndex;
  const el = items[craftNavIndex];
  el.classList.add('controller-focus');
  if (zone === 'recipes') {
    const recipeIndex = parseInt(el.dataset.idx, 10);
    if (!isNaN(recipeIndex) && recipeIndex !== selected) selectCard(recipeIndex);
  }
  el.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  return true;
}

function restoreRecipeNav() {
  setCraftNav('recipes', Math.max(0, selected));
}

function moveCraftNav(delta) {
  const next = craftNavIndex + delta;
  if (next < 0 || next >= craftNavItems(craftNavZone).length) return false;
  return setCraftNav(craftNavZone, next);
}

function craftGridColumns() {
  const cards = craftNavItems('recipes');
  if (!cards.length || cards[0].offsetTop === undefined) return 1;
  return Math.max(1, cards.filter(el => el.offsetTop === cards[0].offsetTop).length);
}

function cycleCraftCategory(dir) {
  const rows = craftNavItems('categories');
  if (!rows.length) return;
  const current = rows.findIndex(el => el.classList.contains('active'));
  const next = rows[Math.max(0, Math.min(rows.length - 1, current + dir))];
  if (next !== rows[current]) next.click();
  setCraftNav('recipes', 0);
}

let craftPointerTarget = null;
let craftPointerAt = 0;
let craftNavAt = 0;
let craftInputSeq = 0;
document.addEventListener('mousemove', e => {
  craftPointerTarget = e.target;
  craftPointerAt = ++craftInputSeq;
}, true);

function clickCraftPointerTarget() {
  if (!craftPointerTarget || craftPointerAt <= craftNavAt || !craftPointerTarget.closest) return false;
  const el = craftPointerTarget.closest('button:not([disabled]), .card, .cr-row, .wb-li, input');
  if (!el) return false;
  el.click();
  const cardIndex = craftNavItems('recipes').indexOf(el);
  if (cardIndex >= 0) setCraftNav('recipes', cardIndex);
  return true;
}

if (typeof window.setPage === 'function') {
  const setPageWithoutControllerFocus = window.setPage;
  window.setPage = arg => {
    if (setPageWithoutControllerFocus(arg) === false) return;
    restoreRecipeNav();
  };
}

if (typeof window.setWorkbenches === 'function') {
  const setWorkbenchesWithoutControllerFocus = window.setWorkbenches;
  window.setWorkbenches = arg => {
    setWorkbenchesWithoutControllerFocus(arg);
    if (!document.body.classList.contains('workbench-mode')) return;
    if (craftNavZone === 'stationAction' && setCraftNav('stationAction', craftNavIndex)) return;
    if (craftNavZone === 'stations' || craftNavZone === 'stationAction') setCraftNav('stations', craftStationIndex);
  };
}

function handleCraftingNavigation(e) {
  if (e.key === 'PageUp' || e.key === 'PageDown') {
    if (document.getElementById('cheatModal').style.display === 'flex' ||
        document.body.classList.contains('workbench-mode')) return;
    cycleCraftCategory(e.key === 'PageUp' ? -1 : 1);
    e.preventDefault();
    e.stopImmediatePropagation();
    return;
  }
  if (!['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Enter', 'Escape', 'Tab'].includes(e.key)) return;
  if (['ArrowLeft', 'ArrowRight', 'Enter'].includes(e.key)) {
    const t = e.target;
    if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)) return;
  }
  const modalOpen = document.getElementById('cheatModal').style.display === 'flex';
  const stationOpen = document.body.classList.contains('workbench-mode');
  if (e.key === 'Escape' || e.key === 'Tab') {
    const t = e.target;
    if (e.key === 'Tab' && t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)) return;
    if (modalOpen) {
      cancelCheat();
      restoreRecipeNav();
    } else if (stationOpen) {
      closeWorkbenches();
      restoreRecipeNav();
    } else if (craftNavZone === 'detail') restoreRecipeNav();
    else closeMenu();
  } else if (modalOpen) {
    if (craftNavZone !== 'modal') setCraftNav('modal', 0);
    else if (e.key === 'Enter') {
      const item = craftNavItems('modal')[craftNavIndex];
      if (item) item.click(); else setCraftNav('modal', 0);
    }
    else moveCraftNav(e.key === 'ArrowLeft' || e.key === 'ArrowUp' ? -1 : 1);
  } else if (stationOpen) {
    if (craftNavZone !== 'stations' && craftNavZone !== 'stationAction') setCraftNav('stations', 0);
    else if (e.key === 'Enter') {
      const item = craftNavItems(craftNavZone)[craftNavIndex];
      if (item) item.click(); else setCraftNav('stations', craftStationIndex);
    } else if (e.key === 'ArrowLeft') setCraftNav('stations', craftStationIndex);
    else if (e.key === 'ArrowRight') setCraftNav('stationAction', 0);
    else if (craftNavZone === 'stations') moveCraftNav(e.key === 'ArrowUp' ? -1 : 1);
  } else {
    const zones = ['categories', 'recipes', 'detail'];
    if (e.key === 'Enter') {
      if (craftNavZone === 'recipes') {
        const item = craftNavItems('recipes')[craftNavIndex];
        const recipeIndex = item ? parseInt(item.dataset.idx, 10) : NaN;
        if (!isNaN(recipeIndex) && recipeIndex !== selected) selectCard(recipeIndex);
        const detailItems = craftNavItems('detail');
        const craftIndex = detailItems.findIndex(el => el.id === 'craftBtn');
        setCraftNav('detail', craftIndex >= 0 ? craftIndex : 0);
      } else {
        const item = craftNavItems(craftNavZone)[craftNavIndex];
        if (item) item.click();
      }
    } else if (e.key === 'ArrowUp') {
      if (craftNavZone === 'header') {}
      else if (craftNavZone === 'pager') restoreRecipeNav();
      else if (craftNavZone === 'filters') setCraftNav('header', 0);
      else if (!moveCraftNav(craftNavZone === 'recipes' ? -craftGridColumns() : -1)) setCraftNav('filters', 0);
    } else if (e.key === 'ArrowDown') {
      if (craftNavZone === 'header') setCraftNav('filters', 0);
      else if (craftNavZone === 'filters') restoreRecipeNav();
      else if (craftNavZone === 'pager') {}
      else if (craftNavZone !== 'recipes') moveCraftNav(1);
      else if (!moveCraftNav(craftGridColumns())) {
        const cols = craftGridColumns();
        const last = craftNavItems('recipes').length - 1;
        if (Math.floor(craftNavIndex / cols) < Math.floor(last / cols)) setCraftNav('recipes', last);
        else setCraftNav('pager', 0);
      }
    } else if (craftNavZone === 'filters' || craftNavZone === 'header' || craftNavZone === 'pager') {
      moveCraftNav(e.key === 'ArrowLeft' ? -1 : 1);
    } else if (craftNavZone === 'recipes') {
      moveCraftNav(e.key === 'ArrowLeft' ? -1 : 1);
    } else {
      const current = zones.includes(craftNavZone) ? zones.indexOf(craftNavZone) : 1;
      setCraftNav(zones[Math.max(0, Math.min(zones.length - 1, current + (e.key === 'ArrowLeft' ? -1 : 1)))], 0);
    }
  }
  e.preventDefault();
  e.stopImmediatePropagation();
}

document.addEventListener('keydown', handleCraftingNavigation, true);
window.addEventListener('prisma-input-device-change', () => {
  if (typeof window.notePrompts === 'function') window.notePrompts('');
  if (typeof window.refreshPrompts === 'function') window.refreshPrompts('');
});

const CRAFT_CONTROLLER_KEYS = {
  'nav.up': 'ArrowUp',
  'nav.down': 'ArrowDown',
  'nav.left': 'ArrowLeft',
  'nav.right': 'ArrowRight',
  'craft.confirm': 'Enter',
  'craft.back': 'Escape',
  'craft.category_prev': 'PageUp',
  'craft.category_next': 'PageDown'
};

window.addEventListener('prisma-controller-action', event => {
  const detail = event && event.detail;
  if (!detail) return;
  const key = CRAFT_CONTROLLER_KEYS[detail.action];
  if (!key) return;
  const repeats = detail.action === 'nav.up' || detail.action === 'nav.down' ||
    detail.action === 'nav.left' || detail.action === 'nav.right';
  if (detail.state !== 'pressed' && !(repeats && detail.state === 'repeat')) return;
  const ae = document.activeElement;
  if (repeats && ae && (ae.tagName === 'INPUT' || ae.tagName === 'TEXTAREA')) return;
  if (detail.action !== 'craft.confirm') craftNavAt = ++craftInputSeq;
  if (detail.action === 'craft.confirm' && clickCraftPointerTarget()) return;
  handleCraftingNavigation({
    key,
    target: null,
    preventDefault() {},
    stopImmediatePropagation() {}
  });
});

const craftNavKeys = (tokens, ps) => {
  const list = Array.isArray(tokens) ? tokens : [tokens];
  return list.map(token => window.PrismaCG.renderKey(token, ps)).join('');
};

if (typeof window.setPrompts === 'function') {
  const baseSetPrompts = window.setPrompts;
  window.setPrompts = function (json) {
    baseSetPrompts(json);
    let d;
    try { d = JSON.parse(json); } catch (e) { return; }
    const nav = d.nav || [];
    if (!nav.length || !window.PrismaCG) return;
    const el = document.getElementById('controls');
    if (!el) return;
    const ps = !!d.ps;
    el.innerHTML += nav.map(it =>
      '<span class="ctrl ctrl-nav">' + craftNavKeys(it.t, ps) +
      '<span class="ctrl-label">' + esc(pcText(it.l)) + '</span></span>').join('');
  };
}
