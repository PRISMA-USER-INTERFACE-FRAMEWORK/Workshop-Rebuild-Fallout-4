(function () {
  'use strict';
  const el = id => document.getElementById(id);
  let state = null, slotId = '', choiceId = '', pending = null, confirmation = null, sequence = 0, returnFocus = null;
  function status(message) { el('status').textContent = message; }
  function slot() { return state && state.weapon && state.weapon.slots.find(item => item.id === slotId); }
  function choice() { const current = slot(); return current && current.choices.find(item => item.id === choiceId); }
  function ready() { return !!state && !pending && typeof window.weapon_bench_action === 'function'; }
  function text(tag, value) { const node = document.createElement(tag); node.textContent = value; return node; }
  const previews = new Map();
  let previewApi = null, previewsClosed = true;
  function validModel(model) {
    return model == null || (model &&
      (typeof model.nifPath === 'string' || (Array.isArray(model.nifPaths) && model.nifPaths.length > 0 && model.nifPaths.every(path => typeof path === 'string' && path.length > 0))) &&
      (model.matSwap == null || typeof model.matSwap === 'string') && (model.formType == null || typeof model.formType === 'string'));
  }
  function hidePreviews() {
    for (const id of previews.keys()) { try { previewApi.hide({ id }); } catch (error) {} }
    previews.clear();
    el('preview-empty').hidden = false;
    el('part-preview-empty').hidden = false;
  }
  function syncPreviews() {
    const api = window.__prismaUI_modelPreview;
    if (!api || typeof api.show !== 'function' || typeof api.hide !== 'function') return;
    if (previewApi !== api) {
      hidePreviews(); previewApi = api;
      if (typeof api.onStatus === 'function') api.onStatus(result => {
        if (!previews.has(result.id)) return;
        const fallback = el(result.id === 'weapon' ? 'preview-empty' : 'part-preview-empty');
        if (result.phase === 'error') fallback.hidden = false;
        else if (result.phase === 'loaded' || result.phase === 'textured') fallback.hidden = true;
      });
    }
    const selected = choice();
    for (const [id, panel, fallback, model] of [
      ['weapon', el('weapon-preview'), el('preview-empty'), state && state.weapon && state.weapon.model],
      ['part', el('part-preview'), el('part-preview-empty'), selected && selected.model]
    ]) {
      const bounds = panel.getBoundingClientRect(), parent = panel.closest('.assembly,.parts').getBoundingClientRect();
      const left = Math.max(0, bounds.left, parent.left), top = Math.max(0, bounds.top, parent.top);
      const right = Math.min(window.innerWidth, bounds.right, parent.right), bottom = Math.min(window.innerHeight, bounds.bottom, parent.bottom);
      if (!model || previewsClosed || confirmation || right <= left || bottom <= top) {
        if (previews.has(id)) { api.hide({ id }); previews.delete(id); }
        fallback.hidden = false; continue;
      }
      const size = Math.max(bounds.width, bounds.height);
      const options = Object.assign({}, model, { id, x: bounds.left + (bounds.width - size) / 2, y: bounds.top + (bounds.height - size) / 2,
        w: size, h: size, clipX: left, clipY: top, clipW: right - left, clipH: bottom - top, brightness: 1.95, zoom: 1.15, spin: 24 });
      const key = JSON.stringify(options);
      if (previews.get(id) === key) continue;
      fallback.hidden = false;
      try { previews.set(id, key); api.show(options); }
      catch (error) { previews.delete(id); fallback.hidden = false; }
    }
  }
  function button(title, subtitle, selected, click, className, key) {
    const node = document.createElement('button');
    node.className = className || '';
    node.dataset.key = key;
    node.appendChild(text('strong', title)); node.appendChild(text('small', subtitle));
    node.setAttribute('aria-pressed', String(selected));
    node.disabled = !!pending;
    node.addEventListener('click', click);
    return node;
  }
  function cancel() {
    confirmation = null;
    el('confirm').hidden = true;
    if (returnFocus && returnFocus.isConnected) returnFocus.focus();
    syncPreviews();
  }
  function send(action, data) {
    if (!ready()) return;
    const request = Object.assign({ action, session: state.session, revision: state.revision, requestId: String(++sequence) }, data);
    pending = request.requestId;
    status('Applying change…');
    render();
    try { window.weapon_bench_action(JSON.stringify(request)); }
    catch (error) { pending = null; render(); status('Change could not be confirmed. Reopen the bench to refresh your inventory.'); }
  }
  function confirm(action) {
    const current = slot(), selected = choice();
    if (!ready() || !current) return;
    if (action === 'install' && (!selected || !selected.compatible || selected.count < 1 || (current.installed && current.installed.id === selected.id))) return;
    if (action === 'remove' && (!current.installed || !current.removable)) return;
    confirmation = { action, weaponId: state.weapon.id, slotId, attachmentId: action === 'install' ? selected.id : current.installed.id };
    returnFocus = document.activeElement;
    el('confirm-title').textContent = action === 'install' ? 'INSTALL ' + selected.name : 'REMOVE ' + current.installed.name;
    const returned = current.installed && current.installed.returns ? '\nReturns to inventory: ' + current.installed.name : '';
    el('confirm-text').textContent = action === 'install'
      ? 'Uses 1 × ' + selected.name + ' from your inventory.' + returned
      : current.installed.returns ? 'Returns 1 × ' + current.installed.name + ' to your inventory.'
      : 'Removes 1 × ' + current.installed.name + ' from the weapon.';
    el('confirm').hidden = false;
    hidePreviews();
    el('cancel').focus();
  }
  function render() {
    const focusKey = document.activeElement && document.activeElement.dataset.key;
    const current = slot(), selected = choice(), weapon = state && state.weapon;
    el('weapons').replaceChildren();
    const query = el('search').value.toLowerCase();
    const weapons = state ? state.weapons : [];
    el('weapon-count').textContent = String(weapons.length).padStart(2, '0');
    weapons.filter(item => item.name.toLowerCase().includes(query)).forEach(item => {
      el('weapons').appendChild(button(item.name, item.equipped ? 'EQUIPPED' : 'IN INVENTORY', !!weapon && item.id === weapon.id,
        () => { if (!weapon || item.id !== weapon.id) send('selectWeapon', { weaponId: item.id }); }, '', 'weapon:' + item.id));
    });
    el('weapon-name').textContent = weapon ? weapon.name : 'WEAPON WORKBENCH';
    el('weapon-subtitle').textContent = weapon ? weapon.subtitle || '' : 'Select a weapon to begin.';
    el('slots').replaceChildren();
    const slots = weapon ? weapon.slots : [];
    el('fitted-count').textContent = slots.filter(item => item.installed).length + ' / ' + slots.length + ' FITTED';
    slots.forEach(item => el('slots').appendChild(button(item.name, item.installed ? item.installed.name : 'Empty slot', item.id === slotId,
      () => { slotId = item.id; choiceId = ''; render(); }, 'slot', 'slot:' + item.id)));
    el('stats').replaceChildren();
    (weapon && weapon.stats || []).forEach(item => {
      const node = document.createElement('div'); node.className = 'stat';
      node.appendChild(text('span', item.label)); node.appendChild(text('strong', item.value)); el('stats').appendChild(node);
    });
    el('slot-name').textContent = current ? current.name : 'SELECT A SLOT';
    el('slot-help').textContent = current ? 'Select a part below to inspect or fit it.' : 'Choose a slot to see available parts.';
    el('installed').replaceChildren();
    if (current && current.installed) {
      el('installed').appendChild(text('small', 'CURRENTLY FITTED'));
      el('installed').appendChild(text('strong', current.installed.name));
    }
    el('choices').replaceChildren();
    const choices = current ? current.choices.filter(item => !el('owned-only').checked || item.count > 0) : [];
    choices.forEach(item => {
      const label = !item.compatible ? item.reason || 'Not compatible' : item.count > 0 ? 'OWNED ×' + item.count : 'NOT OWNED';
      el('choices').appendChild(button(item.name, label, item.id === choiceId,
        () => { choiceId = item.id; render(); el('remove').scrollIntoView({ block: 'nearest', inline: 'nearest' }); }, 'choice', 'part:' + item.id));
    });
    if (!choices.length) el('choices').appendChild(text('p', current ? 'No attachments match this filter.' : 'No slot selected.'));
    el('part-name').textContent = selected ? selected.name : 'NO PART SELECTED';
    el('part-description').textContent = selected ? selected.description || '' : 'Inspect an attachment before making a change.';
    el('exchange').textContent = selected && current && current.installed ? 'Replaces ' + current.installed.name + '.' + (current.installed.returns ? ' The fitted part returns to inventory.' : '') : '';
    el('install').disabled = !ready() || !selected || !selected.compatible || selected.count < 1 || !!(current.installed && current.installed.id === selected.id);
    el('remove').disabled = !ready() || !current || !current.installed || !current.removable;
    if (focusKey) {
      const target = Array.from(document.querySelectorAll('main button:not([disabled])')).find(node => node.dataset.key === focusKey);
      if (target) target.focus();
    }
    syncPreviews();
  }
  window.setWeaponBench = function (json) {
    let next;
    try { next = typeof json === 'string' ? JSON.parse(json) : json; }
    catch (error) { status('Could not read the workbench inventory.'); return false; }
    if (!next || typeof next.session !== 'string' || !next.session || !Number.isSafeInteger(next.revision) || next.revision < 0 ||
        !Array.isArray(next.weapons) || next.weapons.some(item => !item || typeof item.id !== 'string' || typeof item.name !== 'string') ||
        (next.weapon && (typeof next.weapon.id !== 'string' || typeof next.weapon.name !== 'string' || !validModel(next.weapon.model) ||
        (next.weapon.stats !== undefined && (!Array.isArray(next.weapon.stats) || next.weapon.stats.some(item => !item || typeof item.label !== 'string' || !['string', 'number'].includes(typeof item.value)))) || !Array.isArray(next.weapon.slots) ||
        next.weapon.slots.some(item => !item || typeof item.id !== 'string' || typeof item.name !== 'string' || !Array.isArray(item.choices) ||
          typeof item.removable !== 'boolean' || (item.installed && (typeof item.installed.id !== 'string' || typeof item.installed.name !== 'string' || typeof item.installed.returns !== 'boolean')) ||
          item.choices.some(part => !part || typeof part.id !== 'string' || typeof part.name !== 'string' || !validModel(part.model) || typeof part.compatible !== 'boolean' || !Number.isSafeInteger(part.count) || part.count < 0))))) {
      status('Workbench inventory is incomplete.'); return false;
    }
    if (previewsClosed && state && next.session === state.session) return false;
    if (state && state.session === next.session && next.revision < state.revision) {
      if (pending && String(next.requestId) === pending) { pending = null; render(); status('Change superseded. Check the current inventory.'); }
      return false;
    }
    const changedSession = !state || state.session !== next.session;
    const changedWeapon = changedSession || (state.weapon && state.weapon.id) !== (next.weapon && next.weapon.id);
    if (changedSession || (pending && String(next.requestId) === pending)) pending = null;
    cancel();
    state = next;
    previewsClosed = false;
    if (changedWeapon) { slotId = ''; choiceId = ''; }
    if (!slot()) slotId = next.weapon && next.weapon.slots.length ? next.weapon.slots[0].id : '';
    if (!choice()) choiceId = '';
    render();
    status(next.message || (pending ? 'Waiting for the workbench…' : ready() ? 'Ready. Select a slot to configure your weapon.' : 'Workbench unavailable. Your inventory has not changed.'));
    return true;
  };
  function close() {
    if (!el('confirm').hidden) { cancel(); return; }
    previewsClosed = true;
    hidePreviews();
    if (typeof window.prisma_close === 'function') window.prisma_close('');
  }
  function navigate(key) {
    if (!el('confirm').hidden) {
      if (key === 'Escape') cancel();
      else if (key === 'ArrowLeft' || key === 'ArrowRight' || key === 'Tab') {
        (document.activeElement === el('cancel') ? el('apply') : el('cancel')).focus();
      } else if (key === 'Enter') document.activeElement.click();
      return;
    }
    if (key === 'Escape') { close(); return; }
    if (pending) return;
    if (key === 'ArrowLeft' || key === 'ArrowRight') {
      const slots = state && state.weapon ? state.weapon.slots : [];
      if (!slots.length) return;
      const index = slots.findIndex(item => item.id === slotId);
      slotId = slots[(index + (key === 'ArrowLeft' ? -1 : 1) + slots.length) % slots.length].id;
      choiceId = ''; render(); return;
    }
    const buttons = Array.from(document.querySelectorAll('main button:not([disabled])'));
    if (key === 'ArrowUp' || key === 'ArrowDown') {
      if (!buttons.length) return;
      const index = buttons.indexOf(document.activeElement);
      buttons[(index + (key === 'ArrowUp' ? -1 : 1) + buttons.length) % buttons.length].focus();
    } else if (key === 'Enter' && document.activeElement && document.activeElement.tagName === 'BUTTON') document.activeElement.click();
  }
  el('search').addEventListener('input', render);
  el('owned-only').addEventListener('change', render);
  el('install').addEventListener('click', () => confirm('install'));
  el('remove').addEventListener('click', () => confirm('remove'));
  el('close').addEventListener('click', close);
  el('cancel').addEventListener('click', cancel);
  el('apply').addEventListener('click', () => { const action = confirmation; cancel(); if (action) send(action.action, action); });
  document.addEventListener('keydown', event => {
    if (event.target === el('search') && el('confirm').hidden && event.key !== 'Escape') return;
    if (!['Escape', 'Enter', 'ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(event.key) && !(event.key === 'Tab' && !el('confirm').hidden)) return;
    event.preventDefault();
    if (event.repeat && (event.key === 'Enter' || event.key === 'Escape')) return;
    navigate(event.key);
  });
  window.addEventListener('prisma-controller-action', event => {
    const detail = event.detail || {};
    const keys = { 'nav.up': 'ArrowUp', 'nav.down': 'ArrowDown', 'nav.left': 'ArrowLeft', 'nav.right': 'ArrowRight',
      'craft.confirm': 'Enter', 'craft.back': 'Escape', 'craft.category_prev': 'ArrowLeft', 'craft.category_next': 'ArrowRight' };
    const key = keys[detail.action];
    if (key && (detail.state === 'pressed' || (detail.state === 'repeat' && key.startsWith('Arrow')))) navigate(key);
  });
  window.addEventListener('resize', syncPreviews);
  document.addEventListener('scroll', syncPreviews, true);
  window.addEventListener('pagehide', hidePreviews);
  render();
})();
