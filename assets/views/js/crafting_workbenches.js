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
    a.volume = 0.55;
    if (typeof a.play === "function") {
      const p = a.play();
      if (p && p.catch) p.catch((e) => { console.log(`[LAMP] play blocked: ${e}`); });
    } else {
      console.log("[LAMP] audio playback unavailable; continuing without ambience");
    }
  } catch (e) { console.log(`[LAMP] audio graph failed: ${e}`); }
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
  if (benchesUnlocked || document.body.classList.contains("workbench-mode")) return;
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

function wbMatIconHtml(m) {

  return '<span class="wb-mat-svg">' + getMaterialSvg(m?.name) + '</span>';
}

function renderWorkbenches(){
  const list = document.getElementById("wbList");
  const det  = document.getElementById("wbDetail");
  if (!wbData?.benches?.length){
    if (list) list.innerHTML = "";
    if (det)  det.innerHTML  = `<div class="wb-maxed">${wbEsc(pcText("INITIALIZING DATA FEED…"))}</div>`;
    hideWbBenchPreview();
    return;
  }
  list.innerHTML = wbData.benches.map((b) => {
    const z = b.level<=0 ? " zero" : "";
    return '<div class="wb-li'+(b.id===wbSel?' sel':'')+'" onclick="wbSelect(\''+wbId(b.id)+'\')">'
      + '<span class="lname">'+wbEsc(pcText(b.name))+'</span>'
      + '<span class="llvl'+z+'">'+wbEsc(b.level>0?pcText('LVL {{level}}', {level:b.level}):pcText('BASE NODE'))+'</span></div>';
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

  pcSetText(hName, b.name);
  hStatPill.style.display = "flex";

  const isLocked = b.level <= 0;
  if (isLocked) {
    hStatPill.className = "wb-status-pill";
    pcSetText(hStatText, "LOCKED");
    hHexLock.style.display = "flex";
  } else {
    hStatPill.className = "wb-status-pill unlocked";
    pcSetText(hStatText, "BUILT ({{level}}/{{max}})", {level: b.level, max: b.maxLevel});
    hHexLock.style.display = "none";
  }

  let html = '<div class="wb-preview" id="wbPreview"><span class="pv-none"></span></div>';
  html += '<div class="wb-station-desc">' + wbEsc(pcText(b.desc)) + '</div>';

  if (b.level >= b.maxLevel){
    html += `<div class="wb-sec">${wbEsc(pcText("CONSTRUCTION REQUIREMENTS"))}</div>`;
    html += `<div class="wb-maxed">${wbEsc(pcText("MAX NODE CONFIGURATION SECURED"))}</div>`;
  } else if (b.next){
    const n = b.next;

    html += `<div class="wb-sec">${wbEsc(pcText("CONSTRUCTION REQUIREMENTS"))}</div>`;
    html += '<div class="wb-mats-row">';

    html += n.mats.map((m) => {
      const ok = m.have >= m.need;
      const statusIcon = ok ? '<span class="wb-mat-badge ok">✓</span>' : '<span class="wb-mat-badge bad">✕</span>';
      return '<div class="wb-mat-slot">'
        + '<div class="wb-mat-box">' + wbMatIconHtml(m) + '</div>'
        + '<div class="wb-mat-name">' + wbEsc(pcText(m.name || "")) + '</div>'
        + '<div class="wb-mat-ratio-box"><span>' + m.have + '/' + m.need + '</span>' + statusIcon + '</div>'
        + '</div>';
    }).join("");

    html += '</div>';

    html += `<div class="wb-sec">${wbEsc(pcText("FUTURE BONUSES"))}</div>`;
    html += '<div class="wb-bonus-list">';
    html += b.levels.map((L) => {
      const c = L.earned ? "have" : "miss";
      return '<div class="wb-bonus-bullet ' + c + '">'
        + '<svg viewBox="0 0 24 24"><path d="M12 17c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2zm6-9h-1V6c0-2.76-2.24-5-5-5S7 3.24 7 6h1.9c0-1.71 1.39-3.1 3.1-3.1 1.71 0 3.1 1.39 3.1 3.1v2H9c-1.1 0-2 .9-2 2v10c0 1.1.9 2 2 2h9c1.1 0 2-.9 2-2V10c0-1.1-.9-2-2-2zm0 12H9V10h9v10z"/></svg>'
        + '<span>' + wbEsc(pcText(L.bonus)) + '</span>'
        + '</div>';
    }).join("");
    html += '</div>';

    const capsOk = n.capsHave >= n.caps;
    const canUp = n.ready && !b.upgrading && capsOk;
    const activeClass = canUp ? " active" : "";

    const remain = b.upgrading ? Math.max(0, (b.completeAt || 0) - wbServerNow()) : 0;
    const btnText = b.upgrading ? pcText("UPGRADING  {{time}}", {time: wbFmtDur(remain)}) : pcText(b.level > 0 ? "UPGRADE" : "CONSTRUCT");

    html += '<div class="wb-upgrade-action-container">';
    html += '<button class="wb-up-action-btn' + activeClass + '" ' + (canUp ? 'onclick="wbUpgrade(\''+wbId(b.id)+'\')"' : 'disabled') + '>' + wbEsc(btnText) + '</button>';

    const capStatusClass = capsOk ? "" : " bad";
    html += '<span class="wb-action-caps-check' + capStatusClass + '">' + wbEsc(pcText('CAPS: ')) + n.capsHave.toLocaleString() + ' / ' + n.caps.toLocaleString() + '</span>';
    html += '</div>';
  }

  d.innerHTML = html;

  showWbBenchPreview(b);
}

function wbTick(){
  if (!wbData?.benches) return;
  if (!document.body.classList.contains("workbench-mode")) return;
  let anyDone = false;
  wbData.benches.forEach((b) => {
    if (b.upgrading){
      if (((b.completeAt || 0) - wbServerNow()) <= 0) anyDone = true;
    }
  });

  if (anyDone){
    if (!_wbAwaitingRefresh && window.requestWorkbenches){ _wbAwaitingRefresh = true; window.requestWorkbenches(); }
    return;
  }
  const b = wbBench(wbSel);
  if (b?.upgrading) pcSetText(document.querySelector('#wbDetail .wb-up-action-btn'),
    "UPGRADING  {{time}}", {time: wbFmtDur(Math.max(0, (b.completeAt || 0) - wbServerNow()))});
}

function wbUpgrade(id){ if (window.startUpgrade) window.startUpgrade(id); }
