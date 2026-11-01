let _mpDetail = null;

let _mpCards = {};
let _mpCardStatusHooked = false;

const DETAIL_BRIGHTNESS = 1.95, DETAIL_ZOOM = 1.15;
const CARD_BRIGHTNESS   = 1.75, CARD_ZOOM   = 1.0;
if (window.__pcModelPreviews === undefined) window.__pcModelPreviews = true;
function previewApi(){
  const a = window.__prismaUI_modelPreview;
  return (a && typeof a.show === 'function') ? a : null;
}
function modelPreview(){
  if (window.__pcModelPreviews === false) return null;
  return previewApi();
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

function clipTo(el, boundary){
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
function ensureCardPreviewStatus(){
  if (_mpCardStatusHooked) return;
  const api = previewApi();
  if (!api || typeof api.onStatus !== "function") return;
  _mpCardStatusHooked = true;
  api.onStatus((s) => {

    if (!s || s.phase !== "error") return;

    if (retryWithAltMesh(s.id)) return;

    const entry = s.id === "detail" ? (_mpDetail ? { el: document.getElementById("preview") } : null)
                                    : _mpCards[s.id];
    if (entry && entry.el) entry.el.classList.remove("has3d");

    if (s.id === "detail") _mpDetail = null;
    const api = previewApi();
    if (api) api.hide({ id: s.id });
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
  const api = previewApi();
  const enabled = window.__pcModelPreviews !== false;
  const box = entry && entry.el;

  if (_overlayDepth > 0) {
    if (api && entry) api.hide({ id: entry.id });
    if (box) box.classList.remove("has3d");
    return;
  }
  if (!api || !enabled || !entry || !box || !cardIconVisible(box)) {
    if (api && entry) api.hide({ id: entry.id });
    if (box) box.classList.remove("has3d");
    return;
  }
  ensureCardPreviewStatus();
  const r = box.getBoundingClientRect();
  const s = Math.floor(Math.min(r.width, r.height));

  if (s < 24) {
    if (api) api.hide({ id: entry.id });
    box.classList.remove("has3d");
    return;
  }
  const opts = { id: entry.id, formType: entry.r.formType || "", matSwap: entry.r.matSwap || "",
               x: Math.round(r.left + (r.width - s) / 2), y: Math.round(r.top + (r.height - s) / 2),
               w: s, h: s, brightness: CARD_BRIGHTNESS, zoom: CARD_ZOOM,

               ...clipTo(box, document.getElementById("grid")) };
  const pose = cardPreviewOpts();
  opts.spin = pose.spin; if (pose.roll !== undefined) opts.roll = pose.roll;
  const paths = recipeMeshPaths(entry.r);
  if (paths && paths.length > 1) opts.nifPaths = paths;
  else if (paths) opts.nifPath = paths[0];
  else { api.hide({ id: entry.id }); box.classList.remove("has3d"); return; }
  const key = JSON.stringify(opts);
  if (entry.key === key) return;
  entry.key = key;
  api.show(opts);
  box.classList.add("has3d");
}
function syncCardPreviews(){
  Object.keys(_mpCards).forEach((id) => { showCardPreview(_mpCards[id]); });
}
function hideCardPreviews(){
  const api = previewApi();
  Object.keys(_mpCards).forEach((id) => {
    if (api) api.hide({ id: id });
    if (_mpCards[id].el) _mpCards[id].el.classList.remove("has3d");
  });
  _mpCards = {};
}

function hideCardPreviewsKeepEntries(){
  const api = previewApi();
  Object.keys(_mpCards).forEach((id) => {
    if (api) api.hide({ id: id });
    if (_mpCards[id].el) _mpCards[id].el.classList.remove("has3d");
  });
}
function showLivePreview(r){
  const api = previewApi();

  if (_overlayDepth > 0) { if (api) api.hide({ id: "detail" }); _overlayDetail = r; return; }
  const p = document.getElementById("preview");
  const hasComposite = r && r.nifPaths && r.nifPaths.length > 1;
  if (!api || window.__pcModelPreviews === false || !r || (!r.nifPath && !hasComposite)) {
    hideLivePreview();
    if (p) p.innerHTML = '<span id="previewIcon" class="pv-none">' + (api ? '' : esc(pcText('3D PREVIEW OFF'))) + '</span>';
    return;
  }
  ensureCardPreviewStatus();
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
  if (p) p.classList.add("has3d");
  _mpDetail = r;
}
function hideLivePreview(){
  const api = previewApi(); if (api) api.hide({ id: "detail" });
  const p = document.getElementById("preview");
  if (p) p.classList.remove("has3d");
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
  const raw = previewApi();
  const p = document.getElementById("wbPreview");
  if (!p) { _wbBenchShown = null; return; }
  if (!api || !b || !b.nif) {
    p.innerHTML = '<span class="pv-none">' + (api ? '' : esc(pcText('3D PREVIEW OFF'))) + '</span>';
    if (raw) raw.hide({ id: "wbPreview" });
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
  const api = previewApi(); if (api) api.hide({ id: "wbPreview" });
  _wbBenchShown = null;
}

function resyncPreviews() {
  if (_mpDetail) showLivePreview(_mpDetail);
  syncCardPreviews();
  if (_wbBenchShown && document.body.classList.contains("workbench-mode")) {
    const b = wbBench(wbSel);
    if (b) showWbBenchPreview(b, true);
  }
}
window.addEventListener("resize", resyncPreviews);
