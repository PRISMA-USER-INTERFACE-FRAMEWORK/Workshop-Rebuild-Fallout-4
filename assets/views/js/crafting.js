function imeFocus(el){
  try{ if(window.__prismaImeFocusNotify) window.__prismaImeFocusNotify(el); }catch(e){}
}

let cheatMode = false, cheatAck = false, cheatAllowed = true;
function toggleCheat() {
  if (!cheatAllowed) return;
  if (!cheatMode && !cheatAck) {
    document.getElementById("cheatModal").style.display = "flex";
    pushPreviewOverlay();
    return;
  }
  setCheat(!cheatMode);
}
window.setCheatAllowed = (v) => { cheatAllowed = v === '1' || v === 1 || v === true; const b = document.getElementById('cheatBtn'); if (b && !cheatAllowed) { b.hidden = true; if (cheatMode) setCheat(false); } };
window.setCheatState = (v) => {
  cheatMode = v === '1' || v === 1 || v === true;
  cheatAck = false;
  const b = document.getElementById('cheatBtn');
  if (b) { pcSetText(b, cheatMode ? 'CHEAT MODE: ON' : 'CHEAT MODE: OFF'); b.classList.toggle('on', cheatMode); }
  renderGrid(); renderDetail();
};
window.setModelPreviews = (v) => {
  const on = !(v === '0' || v === 0 || v === false);
  window.__pcModelPreviews = on;
  const b = document.getElementById("previewBtn");
  if (b) pcSetText(b, on ? "3D PREVIEWS: ON" : "3D PREVIEWS: OFF");
  if (!on) { hideCardPreviews(); hideLivePreview(); if (typeof hideWbBenchPreview === "function") hideWbBenchPreview(); }
  else { syncCardPreviews(); if (selected >= 0 && filtered[selected]) showLivePreview(filtered[selected]); }
};
function toggleModelPreviews(){
  const next = !(window.__pcModelPreviews !== false);
  window.setModelPreviews(next ? "1" : "0");
  if (window.saveModelPreviews) window.saveModelPreviews(next ? "1" : "0");
}
function setCheat(on) {
  cheatMode = on;
  const b = document.getElementById("cheatBtn");
  if (b) { pcSetText(b, cheatMode ? "CHEAT MODE: ON" : "CHEAT MODE: OFF"); b.classList.toggle("on", cheatMode); }
  if (window.prisma_cheat) window.prisma_cheat(cheatMode ? "1" : "0");
  renderGrid(); renderDetail();
}
function confirmCheat() { cheatAck = true; document.getElementById("cheatModal").style.display = "none"; popPreviewOverlay(); setCheat(true); }
function cancelCheat()  { document.getElementById("cheatModal").style.display = "none"; popPreviewOverlay(); }

let filtered = [];
let serverTotal = 0;
let serverPageCount = 1;
let serverCats = [];
let selected = -1;
let activeTab = "ALL";
let activeSub = "ALL";
let filterText = "";
let lockFilter = "ALL";
let sortMode = "NAME";
let craftQty = 1;
let qtyForFid = null;
let benchIsReady = false;
let _pageSession = "", _pageRequestId = 0, _pagePayload = "", _pageTimer = null;
let _lastPageEpoch = 0;
let _pageLoading = false;

const PAGE_SIZE = 60;
let page = 0;
function lockCode(){
  if (lockFilter === "UNLOCKED") return 1;
  if (lockFilter === "LOCKED")   return 2;
  if (lockFilter === "CANMAKE")  return 3;
  return 0;
}

function toggleCanMake() {
  setLockFilter(lockFilter === "CANMAKE" ? "ALL" : "CANMAKE");
}
function sortCode(){ return sortMode === "VALUE" ? 1 : 0; }
function buildQuery(){

  return [filterText, activeTab, lockCode(), sortCode(), page, PAGE_SIZE, activeSub,
          _pageSession, _pageRequestId].join("\n");
}

function loadPage(reset){
  if (!benchIsReady || !_pageSession) return;
  clearTimeout(_pageTimer);
  _pageRequestId++;
  if (reset) { page = 0; selected = -1; }
  filtered = []; selected = -1;
  serverTotal = 0; serverPageCount = 1; _pageLoading = true;
  renderGrid(); renderDetail();
  pcSetText(document.getElementById("readyCount"), "CHECKING RECIPES…");
  _pagePayload = buildQuery();
  if (window.requestPage) window.requestPage(_pagePayload);
}

let _pendingCraft = null;
let _postCraftRestore = null;
let _reselectFormId = 0;
function setPage(arg){
  let d;
  try { d = typeof arg === "string" ? JSON.parse(arg) : (arg || {}); }
  catch(e){ return false; }
  if (!d || d.session !== _pageSession || d.requestId !== _pageRequestId) return false;
  clearTimeout(_pageTimer);
  if (d.pending) {
    const payload = _pagePayload;
    _pageTimer = setTimeout(() => {
      if (d.session === _pageSession && d.requestId === _pageRequestId && window.requestPage)
        window.requestPage(payload);
    }, 16);
    return false;
  }
  filtered        = d.recipes || [];
  serverTotal     = d.total || 0;
  serverPageCount = Math.max(1, d.pageCount || 1);
  _pageLoading    = false;
  page            = d.page || 0;
  serverCats      = d.categories || [];
  selected = -1;
  if (_reselectFormId) {
    selected = filtered.findIndex((x) => x.formRef === _reselectFormId);
    _reselectFormId = 0;
  }
  pcSetText(document.getElementById("readyCount"), "{{count}} MODULES REGISTERED", {count: serverTotal});
  const requestedTab = activeTab;
  const requestedSub = activeSub;
  populateTabs();
  if (activeTab !== requestedTab || activeSub !== requestedSub) {
    loadPage(true);
    return false;
  }
  renderGrid();
  renderDetail();
  return true;
}
window.setPage = setPage;

function setPostCraft(fid){
  const made = parseInt(String(fid).split(',')[1], 10);

  if (_pendingCraft && parseInt(fid, 16) === parseInt(_pendingCraft.formRef, 16)) {
    if (made > 0) _postCraftRestore = _pendingCraft;
    _pendingCraft = null;
  }
}
window.setPostCraft = setPostCraft;

function benchReady(session){
  session = String(session || "");
  if (!/^[0-9A-F]{8}$/.test(session) || parseInt(session, 16) <= _lastPageEpoch) return;
  _lastPageEpoch = parseInt(session, 16);
  _pageSession = session;
  _pendingCraft = null;
  benchIsReady = false;
  if (document.body.classList.contains("workbench-mode")) closeWorkbenches();
  if (document.getElementById("cheatModal").style.display === "flex") cancelCheat();
  _wbAwaitingRefresh = false;
  benchIsReady = true;
  if (_postCraftRestore) {
    page = _postCraftRestore.page;
    selected = -1;
    _reselectFormId = _postCraftRestore.formRef;
    _postCraftRestore = null;
    loadPage(false);
  } else {
    loadPage(true);
  }
}
window.benchReady = benchReady;
function setPageEpoch(session){
  session = String(session || "");
  if (!/^[0-9A-F]{8}$/.test(session)) return false;
  const epoch = parseInt(session, 16);
  if (epoch <= _lastPageEpoch) return false;
  _lastPageEpoch = epoch;
  _pageSession = session;
  return true;
}
window.setPageEpoch = setPageEpoch;
window.refreshPage = () => {
  if (selected >= 0 && filtered[selected]) _reselectFormId = filtered[selected]["form" + "Id"];
  loadPage(false);
};

function setDesc(arg) {
  if (!arg) return;
  const t = arg.indexOf("\t"); if (t < 0) return;
  const fid = arg.slice(0, t), d = arg.slice(t + 1);
  for (const item of filtered) if (item.formRef === fid) item.desc = d;
  if (selected >= 0 && selected < filtered.length && filtered[selected].formRef === fid) renderDetail();
}
window.setDesc = setDesc;

const expandedCats = {};

function railRow(cls, label, count, active, act, caret) {
  const dc = act.cat != null ? ` data-cat="${wbEsc(act.cat)}"` : '';
  const ds = act.sub != null ? ` data-sub="${wbEsc(act.sub)}"` : '';
  return `<button class="cr-row ${cls}${active ? ' active' : ''}" data-act="${act.t}"${dc}${ds}>` +
           `<span class="cr-caret">${caret || ''}</span>` +
           `<span class="cr-label">${esc(pcText(label))}</span>` +
           `<span class="cr-count">${count}</span>` +
         `</button>`;
}

function populateTabs() {
  const rail = document.getElementById("catRail");
  if (!rail) return;

  const counts = {}, subs = {};
  let totalAll = 0;
  serverCats.forEach((c) => {
    counts[c.cat] = c.count;
    subs[c.cat]   = c.subs || [];
    totalAll += c.count;
  });

  if (activeTab !== "ALL" && !counts[activeTab] && !filterText && lockFilter === "ALL") { activeTab = "ALL"; activeSub = "ALL"; }
  if (activeSub !== "ALL" && !filterText && !(subs[activeTab] || []).some((s) => s.sub === activeSub)) activeSub = "ALL";

  const ordered = CATEGORY_ORDER.filter((c) => counts[c]);
  Object.keys(counts).filter((c) => !CATEGORY_ORDER.includes(c))
        .sort().forEach((c) => { ordered.push(c); });

  let html = railRow("cr-top", "ALL REGISTER", totalAll,
                     activeTab === "ALL", { t: "all" }, "");

  ordered.forEach((c) => {
    const kids = subs[c] || [];
    const open = !!expandedCats[c] || activeTab === c;

    html += railRow("cr-top", c, counts[c],
                    activeTab === c && activeSub === "ALL",
                    { t: "tab", cat: c },
                    kids.length ? (open ? "▾" : "▸") : "");
    if (kids.length) {
      html += '<div class="cr-kids' + (open ? ' open' : '') + '">';
      kids.forEach((s) => {
        html += railRow("cr-sub", s.sub, s.count,
                        activeTab === c && activeSub === s.sub,
                        { t: "sub", cat: c, sub: s.sub }, "");
      });
      html += '</div>';
    }
  });
  rail.innerHTML = html;
  bindCatRail();
}

function bindCatRail(){
  const rail = document.getElementById("catRail");
  if (!rail || rail._catBound) return;
  rail._catBound = true;
  rail.addEventListener("click", (e) => {
    const b = e.target.closest ? e.target.closest(".cr-row") : null;
    if (!b) return;
    const act = b.getAttribute("data-act");
    if (act === "all") setTab("ALL");
    else if (act === "tab") setTab(b.dataset.cat);
    else if (act === "sub") setSub(b.dataset.cat, b.dataset.sub);
  });
}

function setCaps(arg) {
  const n = parseInt(arg, 10);
  document.getElementById("capsVal").textContent =
    isNaN(n) ? "—" : n.toLocaleString();
}

function cleanName(s) {
  s = (s || "");
  s = s.replace(/\{+[^{}]*\}+/g, "");
  s = s.replace(/\[[^\]]*\]/g, "");
  s = s.replace(/^[\s\-:|>]+/, "");
  if (s && !/\s/.test(s)) {
    s = s.replace(/[_]+/g, " ").replace(/([a-z0-9])([A-Z])/g, "$1 $2");
  }
  return s.replace(/\s+/g, " ").trim();
}
function displayName(r) {
  return cleanName(r.name) || cleanName(r.editorId) || pcText("UNNAMED ARCHIVE MATRIX");
}

function reqLabels(r) {
  if (Array.isArray(r.requirements)) {
    const labels = r.requirements.map(p => ({text: pcRequirementText(p).toUpperCase(), missing: p.met === false}));
    return {labels, anyUnmet: labels.some(p => p.missing)};
  }
  const labels = [];
  let anyUnmet = false;
  (r.perks || []).forEach((p) => {
    const nm = cleanName(p && p.name) || (p && p.name) || "";
    if (!nm || /^perk$/i.test(nm) || /unlock|unlockable|_perk/i.test(nm)) return;
    const missing = p.owned === false;
    if (missing) anyUnmet = true;
    labels.push({ text: pcText("PERK: {{name}}{{missing}}", {name: pcPerkName(p, nm), missing: missing ? pcText(p.op ? " — NOT MET" : " — MISSING") : ""}).toUpperCase(), missing });
  });
  const groups = (r.toolGroups && r.toolGroups.length)
    ? r.toolGroups
    : (r.tools || []).map((t) => ({ label: t }));
  groups.forEach((g) => {
    const nm = cleanName(g && g.label) || (g && g.label) || "";
    if (!nm) return;
    const missing = g.ok === false;
    if (missing) anyUnmet = true;
    labels.push({ text: pcText("TOOL: {{name}}{{missing}}", {name: nm, missing: missing ? pcText(" — MISSING") : ""}).toUpperCase(), missing });
  });
  return { labels, anyUnmet };
}

const CATEGORY_ORDER = ["WEAPONS","MELEE","GRENADES","MINES","AMMO","ARMOR","HEADGEAR",
  "BACKPACKS","HEALING","DRUGS","FOOD","DRINKS","HOLOTAPES","UTILITY"];

function rarity(r) {
  const n = (r.components || []).length;
  if (n >= 5) return { cls:"uncommon", lbl:pcText("COMPLEX NODE") };
  return { cls:"common", lbl:pcText("STANDARD TASK") };
}
function typeLabel(r) {
  switch ((r.formType || "").toUpperCase()) {
    case "WEAP": return pcText("GEAR // WEAPON");
    case "ARMO": return pcText("GEAR // ARMOR");
    case "ALCH": return pcText("PROVISION // CONSUMABLE");
    case "INGR": return pcText("MAT // RAW MATERIAL");
    case "AMMO": return pcText("MUNITION // AMMO");
    case "BOOK": return pcText("DATA // SCHEMATIC");
    default: return pcText("EQUIPMENT // AUX");
  }
}

function canCraft(r) {
  if (cheatMode) return true;
  if (r.locked) return false;
  const comps = r.components || [];
  for (const c of comps) {
    if (c.have === undefined) return null;
    if (c.have < (c.count || 1)) return false;
  }
  return true;
}
function availState(r) {
  if (r.locked && !cheatMode) return "locked";
  const cc = canCraft(r);
  if (cc === null) return "ok";
  return cc ? "ok" : "short";
}

function setTab(cat) {
  if (activeTab === cat && cat !== "ALL") expandedCats[cat] = !expandedCats[cat];
  else if (cat !== "ALL") expandedCats[cat] = true;
  activeTab = cat;
  activeSub = "ALL";
  populateTabs();
  loadPage(true);
}

function setSub(cat, sub) {
  activeTab = cat;
  activeSub = sub;
  expandedCats[cat] = true;
  populateTabs();
  loadPage(true);
}
let _searchTimer = null;
function onFilter() {
  filterText = document.getElementById("filterInput").value;
  if (_searchTimer) clearTimeout(_searchTimer);
  _searchTimer = setTimeout(() => { loadPage(true); }, 180);
}
function setLockFilter(f, noLoad) {
  lockFilter = f;
  document.querySelectorAll(".lk-btn[data-lk]").forEach((b) => { b.classList.toggle("active", b.getAttribute("data-lk") === f); });
  const cm = document.getElementById("canMakeBtn");
  if (cm) cm.classList.toggle("active", f === "CANMAKE");
  if (!noLoad) loadPage(true);
}
function setSort(m) {
  sortMode = (m === "VALUE") ? "VALUE" : "NAME";
  document.querySelectorAll(".lk-btn[data-sort]").forEach((b) => { b.classList.toggle("active", b.getAttribute("data-sort") === sortMode); });
  loadPage(true);
}
function toggleCraftable() {}

function applyFilter() {
  loadPage(true);
}

function pageCount() { return serverPageCount; }
function gotoPage(p) {
  page = Math.max(0, Math.min(serverPageCount - 1, p));
  selected = -1;
  loadPage(false);
}

function renderGrid() {
  const grid = document.getElementById("grid");

  hideCardPreviews();
  grid.scrollTop = 0;
  if (!filtered.length) {
    grid.innerHTML = `<div class="empty-d" style="grid-column:1/-1;height:120px">${esc(pcText(_pageLoading ? "CHECKING RECIPES…" : "NO SECTOR ENTRIES REGISTERED"))}</div>`;
    renderPager();
    return;
  }
  let html = "";
  filtered.forEach((r, i) => {
    const rar = rarity(r), avail = availState(r);
    const valTag = (r.value && r.value > 0) ? '<div class="card-val">' + r.value + '</div>' : "";
    let lvlTag = "";
    if (r.wbLvl && r.wbLvl > 0)
      lvlTag += '<div class="card-lvl' + (r.wbLocked ? ' lk' : '') + '" title="' + wbEsc(pcText('Requires {{bench}} Lv{{level}}', {bench: pcText(r.wbBench || ''), level: r.wbLvl})) + '">' + esc(pcText('L{{level}}', {level: r.wbLvl})) + '</div>';
    html +=
      '<div class="card' + (i === selected ? " sel" : "") + '" data-idx="' + i + '" data-fid="' + r.formRef + '">' +
        '<div class="card-top">' +
          '<div class="rar ' + rar.cls + '">' + esc(rar.lbl) + '</div>' +
          '<div class="avail ' + avail + '"></div>' +
        '</div>' +
        '<div class="card-icon"></div>' +
        '<div class="card-name">' + esc(displayName(r)) + '</div>' +
        '<div class="card-type">' + esc(typeLabel(r)) + '</div>' +
        lvlTag +
        valTag +
      '</div>';
  });
  grid.innerHTML = html;

  const cards = grid.querySelectorAll(".card");
  for (const card of cards) {
    const idx = parseInt(card.getAttribute("data-idx"), 10);
    const iconEl = card.querySelector(".card-icon");
    const recipe = filtered[idx];
    const paths = recipeMeshPaths(recipe);

    const iconHidden = iconEl && iconEl.getBoundingClientRect().width === 0;
    if (modelPreview() && paths && paths.length && !iconHidden) {
      const id = "card:" + recipe.formRef;
      _mpCards[id] = { id: id, el: iconEl, r: recipe };
    }

  }
  ensureCardPreviewStatus();

  requestAnimationFrame(syncCardPreviews);
  renderPager();
}

function renderPager() {
  const el = document.getElementById("pager");
  const pc = serverPageCount;
  const d0 = page <= 0 ? " disabled" : "", d1 = page >= pc - 1 ? " disabled" : "";
  el.innerHTML =
    '<button class="pg-btn" onclick="gotoPage(0)"' + d0 + '>«</button>' +
    '<button class="pg-btn nav" onclick="gotoPage(' + (page - 1) + ')"' + d0 + '>' + esc(pcText('PREV')) + '</button>' +
    `<span class="pg-info">${esc(pcText("PAGE "))}<strong>` + (page + 1) + '</strong> / ' + pc + ' [ ' + serverTotal + ' ' + esc(pcText('ENTRIES')) + ' ]</span>' +
    '<button class="pg-btn nav next" onclick="gotoPage(' + (page + 1) + ')"' + d1 + '>' + esc(pcText('NEXT')) + '</button>' +
    '<button class="pg-btn" onclick="gotoPage(' + (pc - 1) + ')"' + d1 + '>»</button>';
}

(function bindGridClick(){
  const grid = document.getElementById("grid");
  if (!grid) { setTimeout(bindGridClick, 50); return; }

  let scrollSettle = null;
  grid.addEventListener("scroll", () => {

    hideCardPreviewsKeepEntries();
    clearTimeout(scrollSettle);
    scrollSettle = setTimeout(() => {
      scrollSettle = null;
      syncCardPreviews();
    }, 120);
  }, { passive: true });
  grid.addEventListener("click", (e) => {
    const card = e.target.closest ? e.target.closest(".card") : null;
    if (!card) return;
    const idx = parseInt(card.getAttribute("data-idx"), 10);
    if (isNaN(idx)) return;
    selectCard(idx);
  });
  grid.addEventListener("dblclick", (e) => {
    const card = e.target.closest ? e.target.closest(".card") : null;
    if (!card) return;
    const idx = parseInt(card.getAttribute("data-idx"), 10);
    if (isNaN(idx)) return;
    selectCard(idx);
    craft();
  });
})();

function selectCard(idx) {
  if (idx === selected) { renderDetail(); return; }
  const grid = document.getElementById("grid");
  if (grid) {
    const prev = grid.querySelector(".card.sel");
    if (prev) prev.classList.remove("sel");
    const next = grid.querySelector('.card[data-idx="' + idx + '"]');
    if (next) next.classList.add("sel");
  }
  selected = idx;
  renderDetail();
}

function renderDetail() {
  const btn  = document.getElementById("craftBtn");
  const body = document.getElementById("detailBody");
  const prev = document.getElementById("preview");

  if (selected < 0 || selected >= filtered.length) {
    hideLivePreview();
    prev.innerHTML = '<span id="previewIcon"></span>';
    body.innerHTML = `<div class="empty-d">${esc(pcText("SELECT ACTIVE TARGET SPECIFICATION"))}</div>`;
    btn.textContent = pcText("CHOOSE PRODUCTION SCHEME"); btn.className = "cant";
    return;
  }

  const r = filtered[selected];
  const cc = canCraft(r);

  showLivePreview(r);

  if (!r._descAsked && (!r.desc || !r.desc.length) && r.formRef && window.requestDesc) { r._descAsked = true; window.requestDesc(r.formRef); }

  let html = '<div class="d-name">' + esc(displayName(r)) + '</div>';
  html += '<div class="d-sub">' + esc(typeLabel(r)) + ' &middot; ' + esc(pcText('FIELD YIELD ×{{count}}', {count: r.count || 1})) +
          ((r.value && r.value > 0) ? ' &middot; ' + esc(pcText('CAPS: ')) + '<span style="color:var(--accent)">' + r.value + '</span>' : '') + '</div>';
  if (r.desc && r.desc.length) html += '<div class="d-desc">' + esc(r.desc) + '</div>';

  if (r.wbBench) {
    html += '<div class="d-bench' + (r.wbLocked ? ' lk' : '') + '">' + esc(pcText('NODE DEP: ')) + esc(pcText(r.wbBench || '')) +
            ' &middot; <strong>' + esc(r.wbLvl > 0 ? pcText('LEVEL {{level}}', {level: r.wbLvl}) : pcText('BASE NODE')) + '</strong></div>';
  }

  if (r.wbLocked) {
    html += '<div class="lockbanner">' + esc(pcText('SYSTEM DENIED — UPGRADE STATION: {{bench}} TO LVL {{level}}', {bench: pcText(r.wbBench || ''), level: r.wbLvl})) + '</div>';
  }

  if (r.locked && !cheatMode) {

    const unmet = reqLabels(r).labels.filter((l) => l.missing);
    if (unmet.length) html += `<div class="lockbanner">${esc(pcText("REQUIRES: "))}` + unmet.map((l) => esc(l.text)).join(' · ') + '</div>';
    else if (!r.wbLocked) html += `<div class="lockbanner">${esc(pcText("OTHER RECIPE CONDITION NOT MET"))}</div>`;
  }

  if (Array.isArray(r.requirements) && r.requirements.length) {
    html += '<div class="sec-label">' + esc(pcText('RECIPE REQUIREMENTS')) + '</div>';
    reqLabels(r).labels.forEach(label => {
      html += '<div class="d-bench' + (label.missing ? ' lk' : '') + '">' +
        esc(label.text) + '</div>';
    });
  }
  const comps = r.components || [];
  html += '<div class="sec-label">' +
          esc(document.body.classList.contains("cooking-mode") ? pcText("COOKING INGREDIENTS")
                                                            : pcText("REQUIRED RECONSTRUCTION PARTS")) + '</div>';
  if (!comps.length) {
    html += `<div style="font-size: calc(11px * var(--ui-fs));color:var(--dim);padding:8px 0">${esc(pcText("NO RESOURCE COST SPECIFIED."))}</div>`;
  } else {
    comps.forEach((c) => {
      const have = c.have, need = c.count || 1;
      const hasData = have !== undefined;
      const ok = hasData && have >= need;
      const pct = hasData ? Math.min(100, Math.round(have / need * 100)) : 100;
      const barColor = ok ? "var(--green)" : (hasData ? "var(--red)" : "var(--dim)");
      const ctClass = !hasData ? "unk" : (ok ? "ok" : "short");
      const ctText  = hasData ? (have + " / " + need) : ("× " + need);
      html +=
        '<div class="comp-row">' +
          '<div class="comp-info">' +
            '<div class="comp-nm">' + esc(cleanName(c.name) || c.formRef) + '</div>' +
            '<div class="comp-bar-bg"><div class="comp-bar" style="width:' + pct + '%;background:' + barColor + '"></div></div>' +
          '</div>' +
          '<div class="comp-ct ' + ctClass + '">' + ctText + '</div>' +
        '</div>';
    });
  }
  body.innerHTML = html;

  if (r.formRef !== qtyForFid) { craftQty = 1; qtyForFid = r.formRef; }

  const craftable = (cc === true) && !(r.locked && !cheatMode);
  const maxq = craftable ? maxCraftable(r) : 1;
  craftQty = Math.max(1, Math.min(craftQty, maxq));
  const qtyRow = document.getElementById("qtyRow");
  if (craftable && maxq > 1) {
    const sl = document.getElementById("qtySlider");
    sl.max = maxq; sl.value = craftQty;
    document.getElementById("qtyVal").textContent = craftQty;
    qtyRow.style.display = "flex";
  } else {
    qtyRow.style.display = "none";
  }

  if (r.locked && !cheatMode) {
    const reasons = [];
    if (r.wbLocked) reasons.push(pcText("BENCH: {{bench}} LVL {{level}} REQUIRED", {bench: pcText(r.wbBench || ""), level: r.wbLvl || 0}));
    reqLabels(r).labels.filter((l) => l.missing).forEach((l) => reasons.push(l.text));
    btn.textContent = reasons.length ? reasons.join(" + ") : pcText("LOCKED");
    btn.className = "cant";
  }
  else if (cc === true)  { btn.className = "can"; updateCraftBtnText(); }
  else { btn.textContent = pcText("INSUFFICIENT MATERIAL MATRIX"); btn.className = "cant"; }
}

function maxCraftable(r) {
  if (cheatMode) return 99;
  const comps = r.components || [];
  if (!comps.length) return 99;
  let m = Infinity;
  comps.forEach((c) => {
    if (c.have === undefined) return;
    m = Math.min(m, Math.floor(c.have / (c.count || 1)));
  });
  return isFinite(m) ? Math.max(0, m) : 99;
}
function updateCraftBtnText() {
  const btn = document.getElementById("craftBtn");
  if (!btn || !btn.classList.contains("can") || selected < 0 || selected >= filtered.length) return;
  const r = filtered[selected];
  btn.textContent = pcText(r.locked ? "START PRODUCTION (CHEAT) ×{{count}}" : "START PRODUCTION ×{{count}}", {count: craftQty});
}
function setQty(v) {
  craftQty = Math.max(1, parseInt(v, 10) || 1);
  document.getElementById("qtyVal").textContent = craftQty;
  updateCraftBtnText();
}
function bumpQty(d) {
  const sl = document.getElementById("qtySlider");
  if (!sl) return;
  const max = parseInt(sl.max, 10) || 1;
  craftQty = Math.max(1, Math.min(max, craftQty + d));
  sl.value = craftQty;
  setQty(craftQty);
}
function esc(s) {
  return String(s == null ? "" : s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;").replace(/'/g,"&#39;");
}
function craft() {
  if (_pendingCraft) return;
  if (selected < 0 || selected >= filtered.length) return;
  const r = filtered[selected];
  if (canCraft(r) !== true) return;
  const qty = Math.max(1, Math.min(craftQty, maxCraftable(r)));
  _pendingCraft = { ["form" + "Id"]: r["form" + "Id"], page: page, name: displayName(r) };
  if (window.prisma_craft) window.prisma_craft(r.formRef + "," + qty);
}
function closeMenu() {
  if (typeof _wbCompleteTimer !== "undefined") { clearTimeout(_wbCompleteTimer); _wbCompleteTimer = null; }
  clearTimeout(_pageTimer);
  _pendingCraft = null;
  _postCraftRestore = null;
  _pageSession = ""; benchIsReady = false;
  if (window.prisma_close) window.prisma_close("");
}

let _fsPct = 100;

let _fsSettle = [];
function _fsResync(){
  requestAnimationFrame(resyncPreviews);
  _fsSettle.forEach(clearTimeout);
  _fsSettle = [120, 300].map((ms) => setTimeout(resyncPreviews, ms));
}
function fsApply(pct, noPersist){
  pct = Math.max(50, Math.min(200, parseInt(pct, 10) || 100));
  _fsPct = pct;

  document.documentElement.style.setProperty("--ui-user", String(pct / 100));

  _fsResync();
  if (!noPersist && window.saveUiScale) {
    try { window.saveUiScale(String(pct)); } catch (e) {}
  }
}
function fsStep(dir){ fsApply(_fsPct + (dir > 0 ? 10 : -10)); }

const THEMES = ["tarkov", "pipboy"];
const THEME_LABEL = { tarkov: "TARKOV", pipboy: "PIP-BOY" };
let _theme = "tarkov";
function applyTheme(name, noPersist){
  _theme = THEMES.includes(name) ? name : "tarkov";
  THEMES.forEach((t) => document.body.classList.toggle("theme-" + t, t === _theme));
  const b = document.getElementById("themeBtn");
  if (b) pcSetText(b, "THEME: {{theme}}", {theme: pcText(THEME_LABEL[_theme] || _theme.toUpperCase())});
  if (!noPersist && window.saveTheme) { try { window.saveTheme(_theme); } catch (e) {} }

  if (typeof resyncPreviews === "function") requestAnimationFrame(resyncPreviews);
}
function cycleTheme(){ applyTheme(THEMES[(THEMES.indexOf(_theme) + 1) % THEMES.length]); }
window.setTheme = (t) => applyTheme(t, true);
applyTheme("tarkov", true);
window.setUiScale = (p) => { fsApply(p, true); };

window.setPrompts = function (json) {
  var d;
  try { d = JSON.parse(json); } catch (e) { return; }
  var el = document.getElementById("controls");
  if (!el) return;
  if (!window.PrismaCG) { el.innerHTML = ""; return; }
  window.craftingPromptData = d;
  var ps = !!d.ps;
  el.innerHTML = (d.items || []).map(function (it) {
    return '<span class="ctrl">' + window.PrismaCG.renderKey(it.t, ps) +
           '<span class="ctrl-label">' + esc(pcText(it.l)) + "</span></span>";
  }).join("");
};

fsApply(100, true);

(function fireWhenReady(){
  function fire(n){
    n = n || 0;
    if (window.prisma_loaded) { try { window.prisma_loaded(""); } catch (e) {} return; }
    setTimeout(() => { fire(n + 1); }, n < 100 ? 50 : 500);
  }
  if (document.readyState === "complete") setTimeout(fire, 30);
  else window.addEventListener("load", () => { setTimeout(fire, 30); });
})();

window.refreshTranslations = () => {
  const grid = document.getElementById("grid");
  const scrollTop = grid.scrollTop;
  populateTabs(); renderGrid(); renderDetail();
  grid.scrollTop = scrollTop;
  if (document.body.classList.contains("workbench-mode")) renderWorkbenches();
  updateBenchTitle();
  pcSetText(document.getElementById("themeBtn"), "THEME: {{theme}}", {theme: pcText(THEME_LABEL[_theme])});
  if (window.craftingPromptData) window.setPrompts(JSON.stringify(window.craftingPromptData));
};
