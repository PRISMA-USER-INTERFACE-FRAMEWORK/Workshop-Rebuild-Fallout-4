function scrapOverlayEl() { return document.getElementById('scrap-overlay'); }
function isScrapOverlayOpen() {
    const el = scrapOverlayEl();
    return !!(el && !el.hidden);
}
function actScrapConfirm() {
    if (typeof window.prisma_scrap === 'function') window.prisma_scrap('');
}
function actScrapCancel() {
    if (typeof window.prisma_scrap_cancel === 'function') window.prisma_scrap_cancel('');
}
window.actScrapConfirm = actScrapConfirm;
window.actScrapCancel = actScrapCancel;
let scrapTranslationData = null;
window.showScrapConfirm = arg => {
    let d; try { d = typeof arg === 'string' ? JSON.parse(arg) : (arg || {}); } catch (e) { return; }
    const el = scrapOverlayEl();
    if (!el) return;
    scrapTranslationData = d;
    renderScrapConfirm(d);
    el.hidden = false;
    const cancel = el.querySelector('.scrap-action-cancel');
    if (cancel) cancel.focus();
};
function renderScrapConfirm(d) {
    const el = scrapOverlayEl();
    if (!el) return;
    const title = document.getElementById('scrap-title');
    const box = document.getElementById('scrap-yields');
    const count = document.getElementById('scrap-count');
    if (title) title.textContent = d.name || '';
    const yields = Array.isArray(d.yields) ? d.yields : [];
    pcSetText(count, yields.length === 1 ? '{{count}} RECOVERABLE MATERIAL' : '{{count}} RECOVERABLE MATERIALS', {count: yields.length});
    if (!box) return;
    if (!yields.length) {
        box.innerHTML = `<span class="dimtxt">${esc(pcText("No components returned."))}</span>`;
    } else {
        box.innerHTML = yields.map(y =>
            `<div class="scrap-line">` +
            `<span class="scrap-line-name">${esc(cleanName(y.name))}</span>` +
            `<span class="scrap-line-add">+${y.add}</span>` +
            `<span class="scrap-line-stored">${esc(pcText('{{count}} STORED', {count: y.stored}))}</span>` +
            `</div>`
        ).join('');
    }
}
window.hideScrapConfirm = () => {
    scrapTranslationData = null;
    const el = scrapOverlayEl();
    if (el) el.hidden = true;
};
