window.ICON_MANIFEST = window.ICON_MANIFEST || {};
window.TURNTABLE_MANIFEST = window.TURNTABLE_MANIFEST || {};

function imeFocus(el){
  console.log(`[SEARCHDIAG] imeFocus el=${el && el.id} notify=${typeof window.__prismaImeFocusNotify}`);
  try{ if(window.__prismaImeFocusNotify) window.__prismaImeFocusNotify(el); }catch(e){ console.log(`[SEARCHDIAG] imeFocus error ${e}`); }
}

document.addEventListener("focusin",  (e) => { console.log(`[SEARCHDIAG] focusin id=${e.target && e.target.id} tag=${e.target && e.target.tagName}`); }, true);
document.addEventListener("keydown",  (e) => { console.log(`[SEARCHDIAG] keydown key=${e.key}`); }, true);
document.addEventListener("input",    (e) => { console.log(`[SEARCHDIAG] input id=${e.target && e.target.id} val=${e.target && e.target.value}`); }, true);

let cheatMode = false, cheatAck = false;
function toggleCheat() {
  if (!cheatMode && !cheatAck) {
    document.getElementById("cheatModal").style.display = "flex";
    pushPreviewOverlay();
    return;
  }
  setCheat(!cheatMode);
}
function setCheat(on) {
  cheatMode = on;
  const b = document.getElementById("cheatBtn");
  if (b) { b.textContent = cheatMode ? "CHEAT MODE: ON" : "CHEAT MODE: OFF"; b.classList.toggle("on", cheatMode); }
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
let menuKind = "";
let benchIsReady = false;

function setMenuKind(k) {
  menuKind = k || "";
  document.getElementById("titleMain").textContent = "PRODUCTION WORKSTATION";
}
function allowedCat(){ return true; }

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

  return [filterText, activeTab, lockCode(), sortCode(), page, PAGE_SIZE, activeSub].join("\n");
}

function loadPage(reset){
  if (reset) { page = 0; selected = -1; }
  if (window.requestPage) window.requestPage(buildQuery());
}

let _pendingCraft = null;
let _postCraftRestore = null;
let _reselectFormId = 0;
function setPage(arg){
  let d;
  try { d = typeof arg === "string" ? JSON.parse(arg) : (arg || {}); }
  catch(e){ d = {}; }
  filtered        = d.recipes || [];
  serverTotal     = d.total || 0;
  serverPageCount = Math.max(1, d.pageCount || 1);
  page            = d.page || 0;
  serverCats      = d.categories || [];
  selected = -1;
  if (_reselectFormId) {
    selected = filtered.findIndex((x) => x.formId === _reselectFormId);
    _reselectFormId = 0;
  }
  document.getElementById("readyCount").textContent = `${serverTotal} MODULES REGISTERED`;
  populateTabs();
  renderGrid();
  renderDetail();
}
window.setPage = setPage;

function setPostCraft(fid){

  if (_pendingCraft && parseInt(fid, 16) === parseInt(_pendingCraft.formId, 16)) {
    _postCraftRestore = _pendingCraft;
    _pendingCraft = null;
  }
}
window.setPostCraft = setPostCraft;

function benchReady(){
  benchIsReady = true;
  if (_postCraftRestore) {
    page = _postCraftRestore.page;
    selected = -1;
    _reselectFormId = _postCraftRestore.formId;
    _postCraftRestore = null;
    if (window.requestPage) window.requestPage(buildQuery());
  } else {
    loadPage(true);
  }
}
window.benchReady = benchReady;

function setDesc(arg) {
  if (!arg) return;
  const t = arg.indexOf("\t"); if (t < 0) return;
  const fid = arg.slice(0, t), d = arg.slice(t + 1);
  for (const item of filtered) if (item.formId === fid) item.desc = d;
  if (selected >= 0 && selected < filtered.length && filtered[selected].formId === fid) renderDetail();
}
window.setDesc = setDesc;

const expandedCats = {};

function railRow(cls, label, count, active, act, caret) {
  const dc = act.cat != null ? ` data-cat="${wbEsc(act.cat)}"` : '';
  const ds = act.sub != null ? ` data-sub="${wbEsc(act.sub)}"` : '';
  return `<button class="cr-row ${cls}${active ? ' active' : ''}" data-act="${act.t}"${dc}${ds}>` +
           `<span class="cr-caret">${caret || ''}</span>` +
           `<span class="cr-label">${esc(label)}</span>` +
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

  if (activeTab !== "ALL" && !counts[activeTab]) { activeTab = "ALL"; activeSub = "ALL"; }

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

let wbData = null, wbSel = null, wbPushClient = 0, wbTimer = null, _wbAwaitingRefresh = false;
let _wbBenchShown = null;
function wbEsc(s){ return String(s==null?"":s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;"); }

function wbId(s){ return String(s==null?"":s).replace(/\\/g,"\\\\").replace(/'/g,"\\'").replace(/"/g,"&quot;"); }
function wbServerNow(){ return (wbData?.now || 0) + (Date.now()/1000 - wbPushClient); }
function wbFmtDur(sec){
  sec = Math.max(0, Math.floor(sec));
  const h = Math.floor(sec/3600), m = Math.floor((sec%3600)/60), s = sec%60;
  function p(n){ return (n<10?"0":"")+n; }
  return (h>0 ? h+":" : "") + p(m) + ":" + p(s);
}
function wbBench(id){ return (wbData && wbData.benches) ? wbData.benches.find((b) => b.id===id) : null; }

function setWorkbenches(arg){
  try { wbData = typeof arg === "string" ? JSON.parse(arg) : (arg || null); }
  catch(e){ wbData = null; }
  wbPushClient = Date.now()/1000;
  _wbAwaitingRefresh = false;
  if (wbData?.benches?.length){
    if (!wbSel || !wbData.benches.some((b) => b.id===wbSel)) wbSel = wbData.benches[0].id;
  }
  if (document.body.classList.contains("workbench-mode")) renderWorkbenches();
}
window.setWorkbenches = setWorkbenches;

let _lampCtx = null, _lampAnalyser = null, _lampData = null, _lampRaf = 0, _lampLevel = 0.5;

function wbLampTick(){
  const lamp = document.getElementById("wbLamp");
  if (!lamp || !_lampAnalyser) { _lampRaf = 0; return; }
  _lampAnalyser.getByteTimeDomainData(_lampData);

  let peak = 0;
  for (let i = 0; i < _lampData.length; i++) {
    const d = Math.abs(_lampData[i] - 128);
    if (d > peak) peak = d;
  }
  const amp = Math.min(1, peak / 96);

  const target = 0.16 + amp * 0.95;
  _lampLevel += (target - _lampLevel) * (target > _lampLevel ? 0.65 : 0.14);
  lamp.style.opacity = _lampLevel.toFixed(3);
  _lampRaf = requestAnimationFrame(wbLampTick);
}

function startLamp(){
  const a = document.getElementById("wbBuzz");
  if (!a) return;
  try {
    if (!_lampCtx) {
      const AC = window.AudioContext || window.webkitAudioContext;
      if (AC) {
        _lampCtx = new AC();

        const src = _lampCtx.createMediaElementSource(a);
        _lampAnalyser = _lampCtx.createAnalyser();
        _lampAnalyser.fftSize = 256;
        _lampData = new Uint8Array(_lampAnalyser.fftSize);
        src.connect(_lampAnalyser);
        _lampAnalyser.connect(_lampCtx.destination);
      }
    }
    if (_lampCtx && _lampCtx.state === "suspended") _lampCtx.resume();
  } catch (e) { console.log(`[LAMP] audio graph failed: ${e}`); }
  a.volume = 0.55;
  const p = a.play();
  if (p && p.catch) p.catch((e) => { console.log(`[LAMP] play blocked: ${e}`); });
  if (!_lampRaf && _lampAnalyser) _lampRaf = requestAnimationFrame(wbLampTick);
}

function stopLamp(){
  const a = document.getElementById("wbBuzz");
  if (a) { try { a.pause(); a.currentTime = 0; } catch (e) {} }
  if (_lampRaf) { cancelAnimationFrame(_lampRaf); _lampRaf = 0; }
  const lamp = document.getElementById("wbLamp");
  if (lamp) lamp.style.opacity = "0.5";
  _lampLevel = 0.5;
}

let benchesUnlocked = false;
function setUnlockAll(v){
  benchesUnlocked = (v === "1" || v === 1 || v === true);
  const b = document.getElementById("stationsBtn");
  if (b) b.style.display = benchesUnlocked ? "none" : "";
}
window.setUnlockAll = setUnlockAll;

function openWorkbenches(){
  if (benchesUnlocked) return;
  document.body.classList.add("workbench-mode");

  pushPreviewOverlay();
  startLamp();
  bindWbScroll();
  if (window.requestWorkbenches) window.requestWorkbenches();
  renderWorkbenches();
  if (wbTimer) clearInterval(wbTimer);
  wbTimer = setInterval(wbTick, 1000);
}
function closeWorkbenches(){
  document.body.classList.remove("workbench-mode");
  if (wbTimer){ clearInterval(wbTimer); wbTimer = null; }
  stopLamp();
  hideWbBenchPreview();
  popPreviewOverlay();

  loadPage(true);
}
function wbSelect(id){ wbSel = id; renderWorkbenches(); }

function getMaterialSvg(name) {
  const n = String(name || "").toUpperCase();
  if (n.includes("STEEL"))
    return '<svg viewBox="0 0 24 24" fill="none" stroke="#9aa0a8" stroke-width="1.5"><rect x="4" y="9" width="16" height="6"/><path d="M4 9l3-4h10l3 4M4 15l3 4h10l3-4"/></svg>';
  if (n.includes("ALUMINUM") || n.includes("ALUMINIUM"))
    return '<svg viewBox="0 0 24 24" fill="none" stroke="#c8ccd4" stroke-width="1.5"><rect x="5" y="6" width="14" height="12"/><path d="M5 10h14M5 14h14"/></svg>';
  if (n.includes("SCREW") || n.includes("BOLT"))
    return '<svg viewBox="0 0 24 24" fill="none" stroke="#b8a87a" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M7 4.5h10"/><path d="M10 4.5l-.5 3.5h5l-.5-3.5"/><path d="M9 8.5h6l-.6 2.5h-4.8z"/><path d="M9.7 11.5h4.6l-.6 2.5h-3.4z"/><path d="M10.4 14.5h3.2l-.5 2.3h-2.2z"/><path d="M11 17l1 2.5 1-2.5"/></svg>';
  if (n.includes("GEAR"))
    return '<svg viewBox="0 0 24 24" fill="none" stroke="#9a8866" stroke-width="1.5"><circle cx="12" cy="12" r="4"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M5 5l2 2M17 17l2 2M5 19l2-2M17 7l2-2"/></svg>';
  if (n.includes("CIRCUIT"))
    return '<svg viewBox="0 0 24 24" fill="none" stroke="#6ab06a" stroke-width="1.5"><rect x="4" y="4" width="16" height="16"/><path d="M4 9h4M4 15h4M16 9h4M16 15h4M9 4v4M15 4v4M9 16v4M15 16v4"/></svg>';
  if (n.includes("OIL") || n.includes("ACID"))
    return '<svg viewBox="0 0 24 24" fill="none" stroke="#b88c3a" stroke-width="1.5"><path d="M12 3l-5 9a5 5 0 0 0 10 0z"/></svg>';
  if (n.includes("ADHESIVE") || n.includes("GLUE"))
    return '<svg viewBox="0 0 24 24" fill="none" stroke="#cfa8d6" stroke-width="1.5"><path d="M9 3h6v4l2 3v9H7v-9l2-3z"/><path d="M11 13v3"/></svg>';
  if (n.includes("CLOTH"))
    return '<svg viewBox="0 0 24 24" fill="none" stroke="#a89e8a" stroke-width="1.5"><path d="M4 6c4-3 12-3 16 0v12c-4 3-12 3-16 0z"/><path d="M4 12c4-3 12-3 16 0"/></svg>';
  if (n.includes("LEATHER") || n.includes("RUBBER"))
    return '<svg viewBox="0 0 24 24" fill="none" stroke="#8b6b48" stroke-width="1.5"><path d="M5 7c3-3 11-3 14 0v10c-3 3-11 3-14 0z"/></svg>';
  if (n.includes("WOOD"))
    return '<svg viewBox="0 0 24 24" fill="none" stroke="#a07a48" stroke-width="1.5"><circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="5"/><circle cx="12" cy="12" r="2"/></svg>';

  return '<svg viewBox="0 0 24 24" fill="none" stroke="#7c7c7a" stroke-width="1.5"><path d="M6 19h12M8 15h8M9 11h6M11 7h2"/></svg>';
}

const _wbMatReq = window._wbMatReq || (window._wbMatReq = {});

function wbMatIconHtml(m) {
  const nif = m?.nif || "";
  const svg = getMaterialSvg(m?.name);
  if (!nif) return svg;
  const iconSlug = ttSlug(m.name || nif);
  const nifSlug = ttSlug(nif);
  if (window.requestTurntable && !_wbMatReq[nifSlug]) {
    _wbMatReq[nifSlug] = 1;
    try { window.requestTurntable(nifSlug + "\n" + nif + "\n"); } catch (e) {}
  }
  const src = "icons/auto/" + iconSlug + ".webp";
  return ''
    + '<span class="wb-mat-svg">' + svg + '</span>'
    + '<img class="wb-mat-img" data-mat-slug="' + nifSlug + '" data-mat-stage="0" '
    +     'src="' + src + '" style="display:none" '
    +     'onload="wbMatIconShown(this)" onerror="wbMatIconError(this)" alt="">';
}

function wbMatIconShown(img) {
  img.style.display = "";
  const box = img.parentNode;
  if (!box) return;
  const svg = box.querySelector(".wb-mat-svg");
  if (svg) svg.style.display = "none";
}

function wbMatIconError(img) {
  const slug = img.getAttribute("data-mat-slug");
  const stage = parseInt(img.getAttribute("data-mat-stage") || "0", 10);
  if (stage === 0) {
    img.setAttribute("data-mat-stage", "1");
    img.src = "icons/turntable/" + slug + "/00.png";
    return;
  }
  const n = stage - 1;
  if (n >= 20) return;
  img.setAttribute("data-mat-stage", String(stage + 1));
  setTimeout(() => {
    if (!img.parentNode) return;
    img.src = "icons/turntable/" + slug + "/00.png?cb=" + n;
  }, 1500);
}

function renderWorkbenches(){
  const list = document.getElementById("wbList");
  const det  = document.getElementById("wbDetail");
  if (!wbData?.benches?.length){
    if (list) list.innerHTML = "";
    if (det)  det.innerHTML  = '<div class="wb-maxed">INITIALIZING DATA FEED…</div>';
    hideWbBenchPreview();
    return;
  }
  list.innerHTML = wbData.benches.map((b) => {
    const z = b.level<=0 ? " zero" : "";
    return '<div class="wb-li'+(b.id===wbSel?' sel':'')+'" onclick="wbSelect(\''+wbId(b.id)+'\')">'
      + '<span class="lname">'+wbEsc(b.name)+'</span>'
      + '<span class="llvl'+z+'">'+(b.level>0?('LVL '+b.level):'BASE NODE')+'</span></div>';
  }).join("");
  renderWbDetail();
}

function renderWbDetail(){
  const b = wbBench(wbSel), d = document.getElementById("wbDetail");
  const hName = document.getElementById("wbName"),
      hStatText = document.getElementById("wbStatusText"),
      hStatPill = document.getElementById("wbStatusPill"),
      hHexLock = document.getElementById("wbHeaderHexLock");

  if (!b){
    if (d) d.innerHTML = "";
    hStatPill.style.display="none";
    hideWbBenchPreview();
    return;
  }

  hName.textContent = b.name;
  hStatPill.style.display = "flex";

  const isLocked = b.level <= 0;
  if (isLocked) {
    hStatPill.className = "wb-status-pill";
    hStatText.textContent = "LOCKED";
    hHexLock.style.display = "flex";
  } else {
    hStatPill.className = "wb-status-pill unlocked";
    hStatText.textContent = "BUILT (" + b.level + "/" + b.maxLevel + ")";
    hHexLock.style.display = "none";
  }

  let html = '<div class="wb-preview" id="wbPreview"><span class="pv-none"></span></div>';
  html += '<div class="wb-station-desc">' + wbEsc(b.desc) + '</div>';

  if (b.level >= b.maxLevel){
    html += '<div class="wb-sec">CONSTRUCTION REQUIREMENTS</div>';
    html += '<div class="wb-maxed">MAX NODE CONFIGURATION SECURED</div>';
  } else if (b.next){
    const n = b.next;

    html += '<div class="wb-sec">CONSTRUCTION REQUIREMENTS</div>';
    html += '<div class="wb-mats-row">';

    html += n.mats.map((m) => {
      const ok = m.have >= m.need;
      const statusIcon = ok ? '<span class="wb-mat-badge ok">✓</span>' : '<span class="wb-mat-badge bad">✕</span>';
      return '<div class="wb-mat-slot">'
        + '<div class="wb-mat-box">' + wbMatIconHtml(m) + '</div>'
        + '<div class="wb-mat-name">' + wbEsc(m.name || "") + '</div>'
        + '<div class="wb-mat-ratio-box"><span>' + m.have + '/' + m.need + '</span>' + statusIcon + '</div>'
        + '</div>';
    }).join("");

    html += '</div>';

    html += '<div class="wb-sec">FUTURE BONUSES</div>';
    html += '<div class="wb-bonus-list">';
    html += b.levels.map((L) => {
      const c = L.earned ? "have" : "miss";
      return '<div class="wb-bonus-bullet ' + c + '">'
        + '<svg viewBox="0 0 24 24"><path d="M12 17c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm6-9h-1V6c0-2.76-2.24-5-5-5S7 3.24 7 6h1.9c0-1.71 1.39-3.1 3.1-3.1 1.71 0 3.1 1.39 3.1 3.1v2H9c-1.1 0-2 .9-2 2v10c0 1.1.9 2 2 2h9c1.1 0 2-.9 2-2V10c0-1.1-.9-2-2-2zm0 12H9V10h9v10z"/></svg>'
        + '<span>' + wbEsc(L.bonus) + '</span>'
        + '</div>';
    }).join("");
    html += '</div>';

    const capsOk = n.capsHave >= n.caps;
    const canUp = n.ready && !b.upgrading && capsOk;
    const activeClass = canUp ? " active" : "";

    const remain = b.upgrading ? Math.max(0, (b.completeAt || 0) - wbServerNow()) : 0;
    const btnText = b.upgrading ? ("UPGRADING  " + wbFmtDur(remain)) : (b.level > 0 ? "UPGRADE" : "CONSTRUCT");

    html += '<div class="wb-upgrade-action-container">';
    html += '<button class="wb-up-action-btn' + activeClass + '" ' + (canUp ? 'onclick="wbUpgrade(\''+wbId(b.id)+'\')"' : 'disabled') + '>' + btnText + '</button>';

    const capStatusClass = capsOk ? "" : " bad";
    html += '<span class="wb-action-caps-check' + capStatusClass + '">CAPS: ' + n.capsHave.toLocaleString() + ' / ' + n.caps.toLocaleString() + '</span>';
    html += '</div>';
  }

  d.innerHTML = html;

  showWbBenchPreview(b);
}

function wbTick(){
  if (!wbData?.benches) return;
  if (!document.body.classList.contains("workbench-mode")) return;
  let anyUpgrading = false, anyDone = false;
  wbData.benches.forEach((b) => {
    if (b.upgrading){
      anyUpgrading = true;
      if (((b.completeAt || 0) - wbServerNow()) <= 0) anyDone = true;
    }
  });

  if (anyDone){
    if (!_wbAwaitingRefresh && window.requestWorkbenches){ _wbAwaitingRefresh = true; window.requestWorkbenches(); }
    return;
  }
  if (anyUpgrading) renderWbDetail();
}

function wbUpgrade(id){ if (window.startUpgrade) window.startUpgrade(id); }

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
  return cleanName(r.name) || cleanName(r.editorId) || "UNNAMED ARCHIVE MATRIX";
}

function reqLabels(r) {
  const labels = [];
  let anyUnmet = false;
  (r.perks || []).forEach((p) => {
    const nm = cleanName(p && p.name) || (p && p.name) || "";
    if (!nm || /^perk$/i.test(nm) || /unlock|unlockable|_perk/i.test(nm)) return;
    const missing = p.owned === false;
    if (missing) anyUnmet = true;
    labels.push({ text: ("PERK: " + nm + (missing ? " — MISSING" : "")).toUpperCase(), missing });
  });
  const groups = (r.toolGroups && r.toolGroups.length)
    ? r.toolGroups
    : (r.tools || []).map((t) => ({ label: t }));
  groups.forEach((g) => {
    const nm = cleanName(g && g.label) || (g && g.label) || "";
    if (!nm) return;
    const missing = g.ok === false;
    if (missing) anyUnmet = true;
    labels.push({ text: ("TOOL: " + nm + (missing ? " — MISSING" : "")).toUpperCase(), missing });
  });
  return { labels, anyUnmet };
}

const CATEGORY_ORDER = ["WEAPONS","MELEE","GRENADES","MINES","AMMO","ARMOR","HEADGEAR",
  "BACKPACKS","HEALING","DRUGS","FOOD","DRINKS","HOLOTAPES","UTILITY"];

function category(r) {
  if (r.workshop && r.filters && r.filters.length) return (r.filters[0] || "MISC").toUpperCase();
  const t = (r.formType || "").toUpperCase();
  const n = (displayName(r) + " " + (r.editorId || "")).toLowerCase();

  if (/backpack|duffle|rucksack|satchel|bandolier|mailbag|shoulder bag|back pouch|sling pouch|travel pouch|carrying device|\bpack\b/.test(n))
    return "BACKPACKS";
  if (/gas ?mask|respirator|goggle|eyepatch|\bvisor\b|night ?vision/.test(n))
    return "HEADGEAR";

  if (t === "WEAP") {
    if (/grenade|molotov/.test(n))                                                return "GRENADES";
    if (/\bmine\b|land ?mine|caltrop|\btrap\b|bear ?trap/.test(n))                           return "MINES";
    if (/knife|blade|sword|machete|\bbat\b|board|knuckle|fist|\baxe\b|hammer|ripper|melee|spear|baton|\bclub\b|gauntlet/.test(n)) return "MELEE";
    return "WEAPONS";
  }
  if (t === "ARMO") {
    if (/mask|hood|helm|\bhat\b|goggle|visor|respirator|glasses|eyepatch|scarf|bandana/.test(n)) return "HEADGEAR";
    return "ARMOR";
  }
  if (t === "AMMO") return "AMMO";
  if (t === "BOOK") return "HOLOTAPES";
  if (t === "ALCH" || t === "INGR") {
    if (/stim|medkit|med-?x|med ?x|\bheal|bandage|gauze|splint|tourniquet|antibiotic|anodyne|\bcure\b|first ?aid|trauma|rad-?away|rad-?x|addictol|antivenom|hydra|quikclot|ibuprofen|injector|iodide|painkiller|morphine|antiseptic|disinfect|salve|ointment|aspirin|penicillin|\bmed\b|medic|healthpak|pharmaceutical/.test(n)) return "HEALING";
    if (/\bjet\b|psycho|buffout|mentat|day ?tripper|calmex|overdrive|fury|ultra ?jet|x-cell|stealth ?boy|skeeto|bufftats|\bchem|\bpax\b|berry/.test(n)) return "DRUGS";
    if (/water|nuka|\bbeer\b|wine|vodka|whiskey|\brum\b|liquor|beverage|\bdrink\b|juice|coffee|\btea\b|\bmilk\b|cola|moonshine|bourbon|buzzbites/.test(n)) return "DRINKS";
    return "FOOD";
  }
  return "UTILITY";
}
function rarity(r) {
  const n = (r.components || []).length;
  if (n >= 5) return { cls:"uncommon", lbl:"COMPLEX NODE" };
  return { cls:"common", lbl:"STANDARD TASK" };
}
function typeLabel(r) {
  switch ((r.formType || "").toUpperCase()) {
    case "WEAP": return "GEAR // WEAPON";
    case "ARMO": return "GEAR // ARMOR";
    case "ALCH": return "PROVISION // CONSUMABLE";
    case "INGR": return "MAT // RAW MATERIAL";
    case "AMMO": return "MUNITION // AMMO";
    case "BOOK": return "DATA // SCHEMATIC";
    default: return "EQUIPMENT // AUX";
  }
}
function iconChar(r) { return ""; }

function weaponIconName(name) {
  const n = (name || "").toLowerCase();
  if (/grenade|molotov/.test(n))                                            return "grenade";
  if (/\bmine\b|land ?mine|caltrop|\btrap\b|bear ?trap/.test(n))            return "land-mine";
  if (/\bbomb\b|nuke|satchel|\bcharge\b/.test(n))                           return "bomb";
  if (/launcher|missile|rocket|fat ?man|mirv|broadsider/.test(n))          return "launcher";
  if (/minigun|gatling|flamer|gauss ?minigun|\bheavy\b/.test(n))           return "heavyweapongun";
  if (/laser|plasma|gauss|institute|gamma|cryo|tesla|\benergy\b|beam/.test(n)) return "laserweapon";
  if (/ak-?47|ak-?112|\bakm\b|kalash/.test(n))                             return "ak-47";
  if (/sniper|hunting rifle|bolt-?action|lever-?action/.test(n))          return "sniper-rifle";
  if (/shotgun|double ?barrel/.test(n))                                    return "shotgun";
  if (/smg|submachine|thompson|grease gun/.test(n))                        return "submachine";
  if (/revolver|western|\.44|44 ?pistol|six ?shooter/.test(n))            return "revolver";
  if (/knife|blade|sword|machete|\bbat\b|board|knuckle|fist|\baxe\b|hammer|ripper|melee|spear|shishkebab|gauntlet|baton|\bpole\b|\bclub\b|wrench/.test(n)) return "melee";
  if (/assault|combat rifle|service rifle|battle rifle/.test(n))           return "assault-rifle";
  if (/pistol|10mm|deliverer|\b9mm\b|handgun/.test(n))                     return "pistol";
  if (/rifle|carbine|musket|bullpup|\bgun\b|pipe/.test(n))                 return "rifle";
  return "rifle";
}
function iconName(formType, name) {
  const t = (formType || "").toUpperCase();
  const n = (name || "").toLowerCase();
  if (t !== "WEAP" && t !== "ARMO") {
    if (/purified water/.test(n))                  return "purifiedwater";
    if (/dirty water/.test(n) || /\bwater\b/.test(n)) return "dirtywater";
    if (/\bmeat\b|\bflesh\b|\bsteak\b|mole.?rat|brahmin|venison|\bribs?\b|drumstick|\bchops?\b/.test(n)) return "meat";
  }
  if (/backpack|duffle|rucksack|satchel|bandolier|mailbag|shoulder bag|back pouch|sling pouch|travel pouch|carrying device|\bpack\b/.test(n)) return "backpack";
  if (/\bmine\b|landmine|land-mine|caltrop|bear ?trap/.test(n)) return "land-mine";
  if (/syringe|injector/.test(n)) return "healthitems";
  if (t === "WEAP") return weaponIconName(name);
  if (t === "ARMO") {
    if (/backpack|\bpack\b|pouch|bandolier|satchel|rucksack/.test(n)) return "backpack";
    if (/helm|mask|hood|\bhat\b|head|goggle|respirator/.test(n))      return "gas-mask";
    return "armor";
  }
  if (t === "ALCH") return /stim|heal|\bmed|medkit|rad.?away|rad.?x|psycho|\bjet\b|fury|overdrive|buffout|mentat|daytripper|calmex|chem|addictol|antibiotic|anodyne|antimicrobial|injector|stimulant|cure|serum/.test(n) ? "healthitems" : "fooditems";
  if (t === "INGR") return "fooditems";
  if (t === "AMMO") return "bullet";
  return "misc";
}
const ICON_OVERRIDE = {
  "pistol":      "Fallout4_10mm_pistol.webp",
  "bullet":      "Fo4CC_12.7mm_round.webp",
  "grenade":     "Fragmentation_grenade_29.webp",
  "healthitems": "FO4_Loot_Prewar_Medkit_Chems.webp",
  "backpack":    "Modular_Backpack.png",
  "misc":        "Wrench_fo4.webp",
  "purifiedwater": "FO4PurifiedWater.webp",
  "dirtywater":    "Fo4_dirty_water.png",
  "meat":          "Fo4_mole_rat_meat.webp"
};
function iconFile(key) { return "icons/" + (ICON_OVERRIDE[key] || (key + ".png")); }
function iconUrl(formType, name) { return iconFile(iconName(formType, name)); }

function manifestIcon(r) {
  if (!r || !window.ICON_MANIFEST) return null;
  const f = (r.createdKey && window.ICON_MANIFEST[r.createdKey]) || (r.created && window.ICON_MANIFEST[r.created]);
  return f ? ("icons/" + f) : null;
}
const NAME_ICON = [
  [/master control|\bswitcher\b|\bcontroller\b|scope control|workshop spawner|workshop recipe/, "Icon_holotape2.png"],
  [/owen gun/,                            "owengun.webp"],
  [/handmade bullpup/,                    "handmadebullpup.webp"],
  [/8.?ball grenade|eight.?ball grenade/, "Eight_ball.png"],
  [/incendiary grenade/,                  "IncendiaryGrenadeFNV.webp"],
  [/acid jar/,                            "Acid_concentrate.webp"],
  [/pipe ?bomb/,                          "Pipe_bomb.png"],
  [/quantum cocktail/,                    "Nuka_Quantum_grenade.webp"],
  [/\bpax\b/,                             "pax.png"],
  [/\bcAMPing\b|camp ?fire|\btent\b|sleeping bag/, "FO4FH_Tent_bed.webp"],
  [/flare gun|bengal gun/,                "Fallout4_Flare_gun.webp"],
  [/bengal (red|white)|\bflare round\b|bengal.*flare/, "F76WL_flare.png"],
  [/proximity detector|feral boss/,       "Flux_Sensor.png"],
  [/gamma.?repeater/,                     "gamma-repeater.webp"],
  [/\bkimber\b/,                          "kimbercustom.webp"],
  [/thompson|\bm1a1\b/,                   "m1a1thompson.webp"],
  [/ots.?14|\bgroza\b/,                   "ots-14groza.webp"],
  [/x-?12.*plasma|x12 ?plasma/,           "x12plasma.webp"],
  [/plasma grenade/,                      "PlasmaGrenade.webp"],
  [/\bmolotov\b/,                         "Fo4_Molotov_cocktail.webp"],
  [/throwable ?screw/,                    "throwablescrew.webp"],
  [/frag(mentation)? mine|\bfrag mine\b/, "Fragmine.webp"],
  [/\bmachete\b/,                         "FO76_Machete.webp"],
  [/\baer.?15\b/,                         "AER15.webp"],
  [/\bholotape\b|holodisk/,               "Icon_holotape2.png"],
  [/healing powder/,                      "HealingPowderFNV.png"],
  [/syringe|injector/,                    "syringe.webp"],
  [/stimpak|stimpack/,                    "stimpak.webp"],
  [/doctor'?s bag/,                       "DoctorsBag.webp"],
  [/\bmilk\b/,                            "milk.webp"],
  [/\btea\b|green ?tea/,                  "auto/drink-refreshing-beverage.webp"],
  [/nuka.?cola float|nukacolafloat/,      "nukacolafloat.png"],
  [/squirrel on a stick/,                 "Squirrel_on_a_stick.webp"],
  [/iguana on a stick/,                   "Iguana_on_a_stick.png"],
  [/nuka.?cola quantum|\bquantum\b/,      "NCF.png"],
  [/slocum'?s buzzbites|buzzbites/,       "Slocums_buzzbites.webp"],
  [/brain skewer|brain fungus/,           "Brain_fungus.webp"],
  [/\bbacon\b|mystery bacon/,             "MysteryBacon-NukaWorld.webp"],
  [/vegetable soup|veggie soup|vegsoup/,  "vegsoup.webp"],
  [/\bstew\b/,                            "Fo4_stew.webp"],
  [/gas ?mask/,                           "Gasmask.webp"]
];
const ICON_BLOCK = /berry mentats|stealth ?boy|\bpsycho\b|doctor'?s bag|b-?ball grenade|survivor mask|post.?apoc/;
function nameIconFor(name) {
  const n = (name || "").toLowerCase();
  for (const entry of NAME_ICON) if (entry[0].test(n)) return "icons/" + entry[1];
  return null;
}
function bestIcon(r) {
  const nm = displayName(r);
  const forced = nameIconFor(nm); if (forced) return forced;
  if (!ICON_BLOCK.test(nm.toLowerCase())) { const m = manifestIcon(r); if (m) return m; }
  return iconUrl(r.formType, nm);
}

function iconCandidates(r) {
  const nm = displayName(r);
  const list = [];
  const forced = nameIconFor(nm); if (forced) list.push(forced);
  if (!ICON_BLOCK.test(nm.toLowerCase())) { const m = manifestIcon(r); if (m) list.push(m); }
  if (r.nifPath) list.push("icons/auto/" + ttSlug(r.nifPath) + ".webp");
  list.push(iconUrl(r.formType, nm));
  return list;
}
function setFallbackIcon(iconEl, r) {
  iconEl.innerHTML = "";
  const cands = iconCandidates(r);
  const img = document.createElement("img");
  let i = 0;
  img.onerror = () => {
    if (++i < cands.length) { img.src = cands[i]; }
    else { img.onerror = null; iconEl.textContent = iconChar(r); }
  };
  img.src = cands[0];
  iconEl.appendChild(img);
}

const TT_FRAMES = 12;
window.__ttReady = window.__ttReady || {};
let _ttState = null;
let _ttPoll  = null;
let _ttReqSlug = null;
let _ttListenersBound = false;

function ttSlug(n){
  let s = (n || "").toLowerCase().replace(/\\/g, "/");
  s = s.replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "");
  return s.slice(0, 120) || "model";
}
function frameName(i){ return (i < 10 ? "0" + i : "" + i) + ".png"; }

function stopTurntable() {
  if (_ttState && _ttState.timer) clearInterval(_ttState.timer);
  _ttState = null;
  if (_ttPoll) { clearTimeout(_ttPoll); _ttPoll = null; }
  _ttReqSlug = null;
}

function spinFrames(dir, frames) {
  if (_ttState && _ttState.timer) clearInterval(_ttState.timer);
  const p = document.getElementById("preview");
  p.innerHTML = "";
  const imgs = [];
  for (let i = 0; i < frames; i++) { const im = new Image(); im.src = "icons/" + dir + "/" + frameName(i); imgs.push(im); }
  const view = document.createElement("img");
  view.className = "tt-view"; view.draggable = false; view.src = imgs[0].src;
  p.appendChild(view);
  const hint = document.createElement("div");
  hint.className = "tt-hint"; hint.textContent = "DRAG TO ROTATE TARGET";
  p.appendChild(hint);

  _ttState = { frames: imgs, idx: 0, dragging: false, lastX: 0, view: view, timer: null };
  function set(idx) { const n = _ttState.frames.length; _ttState.idx = ((idx % n) + n) % n; _ttState.view.src = _ttState.frames[_ttState.idx].src; }
  _ttState.timer = setInterval(() => { if (_ttState && !_ttState.dragging) set(_ttState.idx + 1); }, 90);
  p.onmousedown = (e) => { if (!_ttState) return; _ttState.dragging = true; _ttState.lastX = e.clientX; hint.style.opacity = "0"; e.preventDefault(); };
  if (!_ttListenersBound) {
    _ttListenersBound = true;
    window.addEventListener("mousemove", (e) => {
      if (!_ttState || !_ttState.dragging) return;
      const dx = e.clientX - _ttState.lastX;
      if (Math.abs(dx) >= 12) { set2(_ttState.idx + (dx > 0 ? 1 : -1)); _ttState.lastX = e.clientX; }
      function set2(idx){ const n=_ttState.frames.length; _ttState.idx=((idx%n)+n)%n; _ttState.view.src=_ttState.frames[_ttState.idx].src; }
    });
    window.addEventListener("mouseup", () => { if (_ttState) _ttState.dragging = false; });
  }
}

function requestTurntableFor(r) {
  stopTurntable();
  if (!r || !r.nifPath) return;
  const slug = ttSlug(r.nifPath);
  const dir  = "turntable/" + slug;
  if (window.__ttReady[slug]) { spinFrames(dir, TT_FRAMES); return; }
  if (window.requestTurntable) {
    try { window.requestTurntable(slug + "\n" + r.nifPath + "\n" + (r.createdKey || r.created || "")); } catch (e) {}
  }
  _ttReqSlug = slug;
  let tries = 0;
  const last = frameName(TT_FRAMES - 1);
  (function poll(){
    if (_ttReqSlug !== slug) return;
    const probe = new Image();
    probe.onload = () => { if (_ttReqSlug !== slug) return; window.__ttReady[slug] = true; spinFrames(dir, TT_FRAMES); };
    probe.onerror = () => { if (_ttReqSlug !== slug) return; if (++tries > 40) return; _ttPoll = setTimeout(poll, 1500); };
    probe.src = "icons/" + dir + "/" + last + "?cb=" + tries;
  })();
}

function showFallbackPreview(r) {
  const p = document.getElementById("preview");
  p.innerHTML = "";
  const img = document.createElement("img");
  img.src = bestIcon(r);
  img.onerror = (function(rec){ return function(){
    if (this.src.includes("/auto/")) { this.onerror = () => { p.innerHTML = '<span id="previewIcon">' + iconChar(rec) + '</span>'; };
      this.src = iconUrl(rec.formType, displayName(rec)); }
    else p.innerHTML = '<span id="previewIcon">' + iconChar(rec) + '</span>';
  }; })(r);
  p.appendChild(img);
}

let _mpDetail = null;

let _mpCards = {};
let _mpCardStatusHooked = false;

const DETAIL_BRIGHTNESS = 1.95, DETAIL_ZOOM = 1.15;
const CARD_BRIGHTNESS   = 1.75, CARD_ZOOM   = 1.0;
function modelPreview(){
  const a = window.__prismaUI_modelPreview;
  return (a && typeof a.show === 'function') ? a : null;
}

function clipHostForPreview(){
  const pv = document.getElementById("preview");
  if (pv && getComputedStyle(pv).position === "fixed") return pv;
  return document.getElementById("detail");
}
function paneRect(id){
  const el = document.getElementById(id); if (!el) return null;
  const r = el.getBoundingClientRect();
  return { x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height) };
}
function cardPreviewOpts(){

  return { spin: 0 };
}
function detailPreviewOpts(){

  return { spin: 24 };
}
function recipeMeshPaths(r){
  if (!r) return null;
  if (r.nifPaths && r.nifPaths.length > 1) return r.nifPaths;
  return r.nifPath ? [r.nifPath] : null;
}

function cardIconVisible(el){
  const grid = document.getElementById("grid");
  if (!grid || !el || !el.isConnected) return false;
  const r = el.getBoundingClientRect(), gr = grid.getBoundingClientRect();
  if (!(r.width > 0 && r.height > 0)) return false;
  return r.bottom > gr.top && r.top < gr.bottom && r.right > gr.left && r.left < gr.right;
}

function clipTo(el){
  if (!el) return {};
  const c = el.getBoundingClientRect();
  return { clipX: Math.round(c.left), clipY: Math.round(c.top),
           clipW: Math.round(c.width), clipH: Math.round(c.height) };
}
function ensureCardPreviewStatus(){
  if (_mpCardStatusHooked) return;
  const api = modelPreview();
  if (!api || typeof api.onStatus !== "function") return;
  _mpCardStatusHooked = true;
  api.onStatus((s) => {

    if (!s || s.phase !== "error") return;

    if (retryWithAltMesh(s.id)) return;

    const entry = _mpCards[s.id];
    if (entry && entry.el && entry.el.isConnected) setFallbackIcon(entry.el, entry.r);
  });
}

function retryWithAltMesh(id){
  const api = modelPreview();
  if (!api) return false;
  const isDetail = (id === "detail");
  const entry = isDetail ? (_mpDetail ? { id: "detail", r: _mpDetail } : null) : _mpCards[id];
  const r = entry && entry.r;
  if (!r || !r.altNifPaths || !r.altNifPaths.length || r._altTried) return false;
  r._altTried = true;

  r.nifPath = r.altNifPaths[0];
  r.nifPaths = r.altNifPaths.length > 1 ? r.altNifPaths.slice() : null;
  console.log("[PC] primary mesh unreadable for " + displayName(r) + " -- retrying with its armor mesh");
  if (isDetail) showLivePreview(r); else showCardPreview(entry);
  return true;
}
function showCardPreview(entry){
  const api = modelPreview();

  if (_overlayDepth > 0) {
    if (api && entry) api.hide({ id: entry.id });
    return;
  }
  if (!api || !entry || !cardIconVisible(entry.el)) {
    if (api && entry) api.hide({ id: entry.id });
    return;
  }
  const r = entry.el.getBoundingClientRect();
  const s = Math.floor(Math.min(r.width, r.height));

  if (s < 24) {
    if (api) api.hide({ id: entry.id });
    setFallbackIcon(entry.el, entry.r);
    console.log("[PC] card icon box degenerate (" + Math.round(r.width) + "x" + Math.round(r.height) +
                ") -- using flat icon for " + displayName(entry.r));
    return;
  }
  const opts = { id: entry.id, formType: entry.r.formType || "", matSwap: entry.r.matSwap || "",
               x: Math.round(r.left + (r.width - s) / 2), y: Math.round(r.top + (r.height - s) / 2),
               w: s, h: s, brightness: CARD_BRIGHTNESS, zoom: CARD_ZOOM,

               ...clipTo(document.getElementById("grid")) };
  const pose = cardPreviewOpts();
  opts.spin = pose.spin; if (pose.roll !== undefined) opts.roll = pose.roll;
  const paths = recipeMeshPaths(entry.r);
  if (paths && paths.length > 1) opts.nifPaths = paths; else if (paths) opts.nifPath = paths[0]; else return;
  api.show(opts);
}
function syncCardPreviews(){
  Object.keys(_mpCards).forEach((id) => { showCardPreview(_mpCards[id]); });
}
function hideCardPreviews(){
  const api = modelPreview();
  if (api) Object.keys(_mpCards).forEach((id) => { api.hide({ id: id }); });
  _mpCards = {};
}

function hideCardPreviewsKeepEntries(){
  const api = modelPreview();
  if (api) Object.keys(_mpCards).forEach((id) => { api.hide({ id: id }); });
}
function showLivePreview(r){
  const api = modelPreview();

  if (_overlayDepth > 0) { if (api) api.hide({ id: "detail" }); _overlayDetail = r; return; }
  const p = document.getElementById("preview");

  const hasComposite = r && r.nifPaths && r.nifPaths.length > 1;
  if (!api || !r || (!r.nifPath && !hasComposite)) {
    if (p) p.innerHTML = '<span id="previewIcon" class="pv-none">' + (api ? '' : '3D PREVIEW OFF') + '</span>';
    _mpDetail = null; return;
  }
  if (p) p.innerHTML = '';
  const rc = paneRect("preview");

  const s = Math.min(rc.w, rc.h);
  const sx = rc.x + Math.round((rc.w - s) / 2), sy = rc.y + Math.round((rc.h - s) / 2);

  const opts = { id: "detail", formType: (r.formType || ""), matSwap: (r.matSwap || ""),
               x: sx, y: sy, w: s, h: s, brightness: DETAIL_BRIGHTNESS, zoom: DETAIL_ZOOM,
               ...clipTo(clipHostForPreview()) };
  const pose = detailPreviewOpts();
  opts.spin = pose.spin; if (pose.roll !== undefined) opts.roll = pose.roll;
  if (hasComposite) opts.nifPaths = r.nifPaths; else opts.nifPath = r.nifPath;
  api.show(opts);
  _mpDetail = r;
}
function hideLivePreview(){
  const api = modelPreview(); if (api) api.hide({ id: "detail" });
  _mpDetail = null;
}

let _overlayDepth = 0;
let _overlayDetail = null;
function pushPreviewOverlay(){
  if (++_overlayDepth !== 1) return;
  _overlayDetail = _mpDetail;
  hideCardPreviewsKeepEntries();
  hideLivePreview();
}
function popPreviewOverlay(){
  if (_overlayDepth === 0 || --_overlayDepth !== 0) return;
  syncCardPreviews();
  if (_overlayDetail) showLivePreview(_overlayDetail);
  _overlayDetail = null;
}

function showWbBenchPreview(b, force){
  const api = modelPreview();
  const p = document.getElementById("wbPreview");
  if (!p) { _wbBenchShown = null; return; }
  if (!api || !b || !b.nif) {
    p.innerHTML = '<span class="pv-none">' + (api ? '' : '3D PREVIEW OFF') + '</span>';
    if (api) api.hide({ id: "wbPreview" });
    _wbBenchShown = null;
    return;
  }

  if (!force && _wbBenchShown && _wbBenchShown.id === b.id && _wbBenchShown.nif === b.nif) return;
  p.innerHTML = '';
  const rc = paneRect("wbPreview");
  const s = Math.min(rc.w, rc.h);
  const sx = rc.x + Math.round((rc.w - s) / 2), sy = rc.y + Math.round((rc.h - s) / 2);

  api.show({ id: "wbPreview", nifPath: b.nif, formType: "", matSwap: "",
             x: sx, y: sy, w: s, h: s, spin: 30, brightness: 1.15,
             ...clipTo(document.querySelector(".wb-detail")) });
  _wbBenchShown = { id: b.id, nif: b.nif };
}

function bindWbScroll(){
  const d = document.querySelector(".wb-detail");
  if (!d || d._wbScrollBound) return;
  d._wbScrollBound = true;
  d.addEventListener("scroll", () => {
    if (!_wbBenchShown) return;
    const b = wbBench(_wbBenchShown.id);
    if (b) showWbBenchPreview(b, true);
  }, { passive: true });
}

function hideWbBenchPreview(){
  const api = modelPreview(); if (api) api.hide({ id: "wbPreview" });
  _wbBenchShown = null;
}

function resyncPreviews() {
  if (_mpDetail) showLivePreview(_mpDetail);
  syncCardPreviews();
  if (_wbBenchShown && document.body.classList.contains("workbench-mode")) {
    const b = wbBench(wbSel);
    if (b) showWbBenchPreview(b);
  }
}
window.addEventListener("resize", resyncPreviews);

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
    grid.innerHTML = '<div class="empty-d" style="grid-column:1/-1;height:120px">NO SECTOR ENTRIES REGISTERED</div>';
    renderPager();
    return;
  }
  let html = "";
  filtered.forEach((r, i) => {
    const rar = rarity(r), avail = availState(r);
    const valTag = (r.value && r.value > 0) ? '<div class="card-val">' + r.value + '</div>' : "";
    let lvlTag = "";
    if (r.wbLvl && r.wbLvl > 0)
      lvlTag += '<div class="card-lvl' + (r.wbLocked ? ' lk' : '') + '" title="Requires ' + wbEsc(r.wbBench || '') + ' Lv' + r.wbLvl + '">L' + r.wbLvl + '</div>';
    html +=
      '<div class="card' + (i === selected ? " sel" : "") + '" data-idx="' + i + '" data-fid="' + r.formId + '">' +
        '<div class="card-top">' +
          '<div class="rar ' + rar.cls + '">' + esc(rar.lbl) + '</div>' +
          '<div class="avail ' + avail + '"></div>' +
        '</div>' +
        '<div class="card-icon"></div>' +
        '<div class="card-name">' + esc(displayName(r)) + '</div>' +
        '<div class="card-type">' + typeLabel(r) + '</div>' +
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
      const id = "card:" + recipe.formId;
      _mpCards[id] = { id: id, el: iconEl, r: recipe };
    } else if (iconHidden) {

    } else {
      setFallbackIcon(iconEl, recipe);
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
    '<button class="pg-btn nav" onclick="gotoPage(' + (page - 1) + ')"' + d0 + '>PREV</button>' +
    '<span class="pg-info">PAGE <strong>' + (page + 1) + '</strong> / ' + pc + ' [ ' + serverTotal + ' ENTRIES ]</span>' +
    '<button class="pg-btn nav next" onclick="gotoPage(' + (page + 1) + ')"' + d1 + '>NEXT</button>' +
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
    stopTurntable();
    hideLivePreview();
    prev.innerHTML = '<span id="previewIcon"></span>';
    body.innerHTML = '<div class="empty-d">SELECT ACTIVE TARGET SPECIFICATION</div>';
    btn.textContent = "CHOOSE PRODUCTION SCHEME"; btn.className = "cant";
    return;
  }

  const r = filtered[selected];
  const cc = canCraft(r);

  stopTurntable();
  showLivePreview(r);

  if ((!r.desc || !r.desc.length) && r.formId && window.requestDesc) { r._descAsked = true; window.requestDesc(r.formId); }

  let html = '<div class="d-name">' + esc(displayName(r)) + '</div>';
  html += '<div class="d-sub">' + typeLabel(r) + ' &middot; FIELD YIELD &times;' + (r.count || 1) +
          ((r.value && r.value > 0) ? ' &middot; CAPS: <span style="color:var(--accent)">' + r.value + '</span>' : '') + '</div>';
  if (r.desc && r.desc.length) html += '<div class="d-desc">' + esc(r.desc) + '</div>';

  if (r.wbBench) {
    html += '<div class="d-bench' + (r.wbLocked ? ' lk' : '') + '">NODE DEP: ' + esc(r.wbBench) +
            ' &middot; <strong>' + (r.wbLvl > 0 ? 'LEVEL ' + r.wbLvl : 'BASE NODE') + '</strong></div>';
  }

  if (r.wbLocked) {
    html += '<div class="lockbanner">SYSTEM DENIED — UPGRADE STATION: ' + esc(r.wbBench || '') + ' TO LVL ' + r.wbLvl + '</div>';
  }

  if (r.locked && !cheatMode) {

    const unmet = reqLabels(r).labels.filter((l) => l.missing);
    if (unmet.length) html += '<div class="lockbanner">REQUIRES: ' + unmet.map((l) => esc(l.text)).join(' · ') + '</div>';
    else if (!r.wbLocked) html += '<div class="lockbanner">OTHER RECIPE CONDITION NOT MET</div>';
  }

  const comps = r.components || [];
  html += '<div class="sec-label">' +
          (document.body.classList.contains("cooking-mode") ? "COOKING INGREDIENTS"
                                                            : "REQUIRED RECONSTRUCTION PARTS") + '</div>';
  if (!comps.length) {
    html += '<div style="font-size: calc(11px * var(--ui-fs));color:var(--dim);padding:8px 0">NO RESOURCE COST SPECIFIED.</div>';
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
            '<div class="comp-nm">' + esc(cleanName(c.name) || c.formId) + '</div>' +
            '<div class="comp-bar-bg"><div class="comp-bar" style="width:' + pct + '%;background:' + barColor + '"></div></div>' +
          '</div>' +
          '<div class="comp-ct ' + ctClass + '">' + ctText + '</div>' +
        '</div>';
    });
  }
  body.innerHTML = html;

  if (r.formId !== qtyForFid) { craftQty = 1; qtyForFid = r.formId; }

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
    if (r.wbLocked) reasons.push("BENCH: " + (r.wbBench || "") + " LVL " + (r.wbLvl || 0) + " REQUIRED");
    reqLabels(r).labels.filter((l) => l.missing).forEach((l) => reasons.push(l.text));
    btn.textContent = reasons.length ? reasons.join(" + ") : "LOCKED";
    btn.className = "cant";
  }
  else if (cc === true)  { btn.className = "can"; updateCraftBtnText(); }
  else { btn.textContent = "INSUFFICIENT MATERIAL MATRIX"; btn.className = "cant"; }
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
  btn.textContent = (r.locked ? "START PRODUCTION (CHEAT) ×" : "START PRODUCTION ×") + craftQty;
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
  return String(s == null ? "" : s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;");
}
function craft() {
  if (selected < 0 || selected >= filtered.length) return;
  const r = filtered[selected];
  if (canCraft(r) !== true) return;
  const qty = Math.max(1, Math.min(craftQty, maxCraftable(r)));
  _pendingCraft = { formId: r.formId, page: page };
  if (window.prisma_craft) window.prisma_craft(r.formId + "," + qty);
}
function closeMenu() { if (window.prisma_close) window.prisma_close(""); }


let _fsPct = 125;

let _fsSettle = [];
function _fsResync(){
  requestAnimationFrame(resyncPreviews);
  _fsSettle.forEach(clearTimeout);
  _fsSettle = [120, 300].map((ms) => setTimeout(resyncPreviews, ms));
}
function fsApply(pct, noPersist){
  pct = Math.max(80, Math.min(200, parseInt(pct, 10) || 125));
  _fsPct = pct;

  document.documentElement.style.setProperty("--ui-user", String(pct / 100));

  _fsResync();
  try { localStorage.setItem("pc_uiScale", String(pct)); } catch (e) {}
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
  if (b) b.textContent = "THEME: " + (THEME_LABEL[_theme] || _theme.toUpperCase());
  try { localStorage.setItem("pc_theme", _theme); } catch (e) {}
  if (!noPersist && window.saveTheme) { try { window.saveTheme(_theme); } catch (e) {} }

  if (typeof resyncPreviews === "function") requestAnimationFrame(resyncPreviews);
}
function cycleTheme(){ applyTheme(THEMES[(THEMES.indexOf(_theme) + 1) % THEMES.length]); }
window.setTheme = (t) => applyTheme(t, true);
(function restoreTheme(){
  let t = "tarkov";
  try { t = localStorage.getItem("pc_theme") || "tarkov"; } catch (e) {}
  applyTheme(t, true);
})();
window.setUiScale = (p) => { fsApply(p, true); };
(function restoreFs(){
  let p = 125;
  try { p = parseInt(localStorage.getItem("pc_uiScale"), 10) || 125; } catch (e) {}
  fsApply(p, true);
})();

document.addEventListener("keydown", (e) => {
  if (e.key === "Tab") {
    const t = e.target, tag = t && t.tagName;
    if (tag === "INPUT" || tag === "TEXTAREA" || (t && t.isContentEditable)) return;
    e.preventDefault(); closeMenu(); return;
  }
  if (e.key === "Escape") {
    if (document.body.classList.contains("workbench-mode")) closeWorkbenches();
    else closeMenu();
  }
}, true);

(function fireWhenReady(){
  function fire(n){
    n = n || 0;
    if (window.prisma_loaded) { try { window.prisma_loaded(""); } catch (e) {} return; }
    if (n < 100) setTimeout(() => { fire(n + 1); }, 50);
  }
  if (document.readyState === "complete") setTimeout(fire, 30);
  else window.addEventListener("load", () => { setTimeout(fire, 30); });
})();
