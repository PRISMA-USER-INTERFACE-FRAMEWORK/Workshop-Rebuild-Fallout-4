(function () {
  'use strict';
  const el = id => document.getElementById(id);
  let state = null, slotId = '', choiceId = '', pending = null, confirmation = null, sequence = 0, returnFocus = null;
  function status(message) { const node = el('status'); if (node) node.textContent = pcText(message); }
  function slot() { return state && state.weapon && state.weapon.slots.find(item => item.id === slotId); }
  function choice() { const current = slot(); return current && current.choices.find(item => item.id === choiceId); }
  function ready() { return !!state && !pending && typeof window.weapon_bench_action === 'function'; }
  function text(tag, value) { const node = document.createElement(tag); node.textContent = typeof value === 'string' ? pcText(value) : value; return node; }
  function tx(key, vars) { return pcText(key, vars); }
  function setText(id, key, vars) { const node = el(id); if (node) node.textContent = tx(key, vars); }
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
      const bounds = panel.getBoundingClientRect(), parent = panel.closest('.stage,.inspector').getBoundingClientRect();
      const left = Math.max(0, bounds.left, parent.left), top = Math.max(0, bounds.top, parent.top);
      const right = Math.min(window.innerWidth, bounds.right, parent.right), bottom = Math.min(window.innerHeight, bounds.bottom, parent.bottom);
      if (!model || previewsClosed || confirmation || right <= left || bottom <= top) {
        if (previews.has(id)) { api.hide({ id }); previews.delete(id); }
        fallback.hidden = false; continue;
      }
      const size = Math.max(80, Math.min(bounds.width, bounds.height) * 0.85);
      const options = Object.assign({}, model, { id, formType: 'WEAP', x: bounds.left + (bounds.width - size) / 2, y: bounds.top + (bounds.height - size) / 2,
        w: size, h: size, clipX: left, clipY: top, clipW: right - left, clipH: bottom - top, brightness: 1.0, zoom: 1.0, spin: 0 });
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
  let pendingTimer = null;
  function clearPendingTimer() { if (pendingTimer !== null) { clearTimeout(pendingTimer); pendingTimer = null; } }
  function armPendingTimer() {
    clearPendingTimer();
    pendingTimer = setTimeout(() => {
      pendingTimer = null;
      if (!pending) return;
      pending = null;
      render();
      status('No reply from the workbench. Reopen the bench if the change did not apply.');
    }, 3000);
  }
  function send(action, data) {
    if (!ready()) return;
    const request = Object.assign({ action, session: state.session, revision: state.revision, requestId: String(++sequence) }, data);
    pending = request.requestId;
    armPendingTimer();
    status('Applying change…');
    render();
    try { window.weapon_bench_action(JSON.stringify(request)); }
    catch (error) { clearPendingTimer(); pending = null; render(); status('Change could not be confirmed. Reopen the bench to refresh your inventory.'); }
  }
  function confirm(action) {
    const current = slot(), selected = choice();
    if (!ready() || !current) return;
    if (action === 'install' && (!selected || !selected.compatible || selected.count < 1 || (current.installed && current.installed.id === selected.id))) return;
    if (action === 'remove' && (!current.installed || !current.removable)) return;
    confirmation = { action, weaponId: state.weapon.id, slotId, attachmentId: action === 'install' ? selected.id : current.installed.id };
    returnFocus = document.activeElement;
    setText('confirm-title', action === 'install' ? 'INSTALL {{name}}' : 'REMOVE {{name}}', {name: action === 'install' ? selected.name : current.installed.name});
    const returned = current.installed && current.installed.returns ? '\n' + tx('Returns to inventory: {{name}}', {name: current.installed.name}) : '';
    el('confirm-text').textContent = action === 'install'
      ? tx('Uses 1 × {{name}} from your inventory.', {name: selected.name}) + returned
      : current.installed.returns ? tx('Returns 1 × {{name}} to your inventory.', {name: current.installed.name})
      : tx('Removes 1 × {{name}} from the weapon.', {name: current.installed.name});
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
      el('weapons').appendChild(button(item.name, item.equipped ? tx('EQUIPPED') : tx('IN INVENTORY'), !!weapon && item.id === weapon.id,
        () => { if (!weapon || item.id !== weapon.id) send('selectWeapon', { weaponId: item.id }); }, '', 'weapon:' + item.id));
    });
    if (weapon) el('weapon-name').textContent = weapon.name; else setText('weapon-name','WEAPON WORKBENCH');
    if (weapon) el('weapon-subtitle').textContent = weapon.subtitle || ''; else setText('weapon-subtitle','Select a weapon to begin.');
    el('slots').replaceChildren();
    const slots = weapon ? weapon.slots : [];
    el('fitted-count').textContent = tx('{{fitted}} / {{total}} FITTED', {fitted: slots.filter(item => item.installed).length, total: slots.length});
    const slotCount = slots.length || 1;
    slots.forEach((item, index) => {
      const node = button(item.name, item.installed ? item.installed.name : tx('Empty slot'), item.id === slotId,
        () => { slotId = item.id; choiceId = ''; render(); }, 'slot', 'slot:' + item.id);
      if (node.style && typeof node.style.setProperty === 'function') {
        const angle = -Math.PI / 2 + (index / slotCount) * Math.PI * 2;
        node.style.left = (50 + Math.cos(angle) * 37).toFixed(2) + '%';
        node.style.top = (50 + Math.sin(angle) * 34).toFixed(2) + '%';
      }
      el('slots').appendChild(node);
    });
    el('stats').replaceChildren();
    const stats = weapon && weapon.stats || [];
    const numeric = stats.map(item => typeof item.value === 'number' ? item.value : Number.parseFloat(item.value));
    const peak = Math.max(1, ...numeric.filter(Number.isFinite).map(value => Math.abs(value)));
    stats.forEach((item, index) => {
      const node = document.createElement('div'); node.className = 'stat';
      node.appendChild(text('span', item.label)); node.appendChild(text('strong', item.value));
      if (Number.isFinite(numeric[index]) && node.style && typeof node.style.setProperty === 'function')
        node.style.setProperty('--fill', Math.max(3, Math.round(Math.abs(numeric[index]) / peak * 100)) + '%');
      el('stats').appendChild(node);
    });
    if (current) el('slot-name').textContent = current.name; else setText('slot-name','SELECT A SLOT');
    setText('slot-help', current ? 'Select a part below to inspect or fit it.' : 'Choose a slot to see available parts.');
    el('installed').replaceChildren();
    if (current && current.installed) {
      el('installed').appendChild(text('small', 'CURRENTLY FITTED'));
      el('installed').appendChild(text('strong', current.installed.name));
    }
    el('choices').replaceChildren();
    const choices = current ? current.choices.filter(item => !el('owned-only').checked || item.count > 0) : [];
    choices.forEach(item => {
      const label = !item.compatible ? (item.reason ? tx(item.reason) : tx('Not compatible')) : item.count > 0 ? tx('OWNED ×{{count}}', {count: item.count}) : tx('NOT OWNED');
      el('choices').appendChild(button(item.name, label, item.id === choiceId,
        () => { choiceId = item.id; render(); el('remove').scrollIntoView({ block: 'nearest', inline: 'nearest' }); }, 'choice', 'part:' + item.id));
    });
    if (!choices.length) el('choices').appendChild(text('p', current ? 'No attachments match this filter.' : 'No slot selected.'));
    if (selected) el('part-name').textContent = selected.name; else setText('part-name','NO PART SELECTED');
    if (selected) el('part-description').textContent = selected.description || ''; else setText('part-description','Inspect an attachment before making a change.');
    el('exchange').textContent = selected && !state.installAvailable ? tx('Installing parts is not available yet.')
      : selected && current && current.installed
      ? tx('Replaces {{name}}.', {name: current.installed.name}) + (current.installed.returns ? ' ' + tx('The fitted part returns to inventory.') : '')
      : '';
    el('install').disabled = !(state && state.installAvailable) || !ready() || !selected || !selected.compatible || selected.count < 1 || !!(current.installed && current.installed.id === selected.id);
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
    if (next && next.ok === false && typeof next.session !== 'string') {
      if (pending && String(next.requestId) === pending) {
        pending = null; clearPendingTimer(); render();
        status(next.message ? tx(next.message) : 'No reply from the workbench. Reopen the bench if the change did not apply.');
        return false;
      }
      return false;
    }
    if (!next || typeof next.session !== 'string' || !next.session || !Number.isSafeInteger(next.revision) || next.revision < 0 ||
        !Array.isArray(next.weapons) || next.weapons.some(item => !item || typeof item.id !== 'string' || typeof item.name !== 'string') ||
        (next.weapon && (typeof next.weapon.id !== 'string' || typeof next.weapon.name !== 'string' || !validModel(next.weapon.model) ||
        (next.weapon.stats !== undefined && (!Array.isArray(next.weapon.stats) || next.weapon.stats.some(item => !item || typeof item.label !== 'string' || !['string', 'number'].includes(typeof item.value)))) || !Array.isArray(next.weapon.slots) ||
        next.weapon.slots.some(item => !item || typeof item.id !== 'string' || typeof item.name !== 'string' || !Array.isArray(item.choices) ||
          typeof item.removable !== 'boolean' || (item.installed && (typeof item.installed.id !== 'string' || typeof item.installed.name !== 'string' || typeof item.installed.returns !== 'boolean')) ||
          item.choices.some(part => !part || typeof part.id !== 'string' || typeof part.name !== 'string' || !validModel(part.model) || typeof part.compatible !== 'boolean' || !Number.isSafeInteger(part.count) || part.count < 0))))) {
      status('Workbench inventory is incomplete.'); return false;
    }
    if (previewsClosed && state && next.session === state.session) pending = null;
    if (state && state.session === next.session && next.revision < state.revision) {
      if (pending && String(next.requestId) === pending) { pending = null; render(); status('Change superseded. Check the current inventory.'); }
      return false;
    }
    const changedSession = !state || state.session !== next.session;
    const changedWeapon = changedSession || (state.weapon && state.weapon.id) !== (next.weapon && next.weapon.id);
    if (changedSession || (pending && String(next.requestId) === pending)) { pending = null; clearPendingTimer(); }
    if (changedSession) {
        hidePreviews();
        el('search').value = '';
        el('owned-only').checked = true;
        for (const id of ['weapons', 'slots', 'choices']) {
            const node = el(id);
            if (node) node.replaceChildren();
        }
        document.body.classList.remove('bench-ready');
    }
    cancel();
    state = next;
    if (changedSession) previewsClosed = false;
    if (changedWeapon) { slotId = ''; choiceId = ''; }
    if (!slot()) slotId = next.weapon && next.weapon.slots.length ? next.weapon.slots[0].id : '';
    if (!choice()) choiceId = '';
    render();
    status(next.message ? tx(next.message) : pending ? tx('Waiting for the workbench…') : ready() ? tx('Ready. Select a slot to configure your weapon.') : tx('Workbench unavailable. Your inventory has not changed.'));
    document.body.classList.add('bench-ready');
    if (!state._readySent && typeof window.weapon_bench_ready === 'function') {
        state._readySent = true;
        try { window.weapon_bench_ready('1'); } catch (error) {}
    }
    return true;
  };
  function close() {
    if (!el('confirm').hidden) { cancel(); return; }
    clearPendingTimer();
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
      'bench.confirm': 'Enter', 'bench.back': 'Escape', 'bench.slot_prev': 'ArrowLeft', 'bench.slot_next': 'ArrowRight' };
    const key = keys[detail.action];
    if (key && (detail.state === 'pressed' || (detail.state === 'repeat' && key.startsWith('Arrow')))) navigate(key);
  });
  window.addEventListener('prisma-input-device-change', () => {
    if (typeof window.refreshPrompts === 'function') window.refreshPrompts('');
  });
  window.setWeaponBenchPrompts = function (json) {
    let data;
    try { data = typeof json === 'string' ? JSON.parse(json) : json; } catch (error) { return; }
    const bar = el('controls');
    if (!bar) return;
    const api = window.PrismaCG;
    if (!api) { bar.replaceChildren(); return; }
    const ps = !!(data && data.ps);
    const items = [...((data && data.items) || []), ...((data && data.nav) || [])];
    bar.replaceChildren(...items.map(item => {
      if (!item || typeof item !== 'object') return null;
      const node = document.createElement('span'); node.className = 'ctrl';
      const key = document.createElement('span');
      const tokens = Array.isArray(item.t) ? item.t : [item.t];
      key.innerHTML = tokens.every(token => typeof token === 'string' && /^[A-Za-z0-9_+:/ .-]{1,40}$/.test(token))
        ? tokens.map(token => { try { return api.renderKey(token, ps); } catch (error) { return ''; } }).join('')
        : '';
      const label = document.createElement('span'); label.className = 'ctrl-label';
      label.textContent = typeof item.l === 'string' ? pcText(item.l) : '';
      node.appendChild(key); node.appendChild(label); return node;
    }).filter(Boolean));
  };
  window.addEventListener('resize', syncPreviews);
  document.addEventListener('scroll', syncPreviews, true);
  window.hidePreviews = function () { previewsClosed = true; hidePreviews(); };
  window.addEventListener('pagehide', hidePreviews);
  render();
})();
