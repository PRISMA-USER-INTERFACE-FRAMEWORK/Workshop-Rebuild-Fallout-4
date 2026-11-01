(() => {
  const completeCraft = window.setPostCraft;
  let timer = 0;

  function show(count, item) {
    let notice = document.getElementById('craftNotice');
    if (!notice) {
      notice = document.createElement('div');
      notice.id = 'craftNotice';
      notice.setAttribute('role', 'status');
      notice.setAttribute('aria-live', 'polite');
      document.body.appendChild(notice);
    }
    pcSetText(notice, "CRAFTED {{count}} × {{item}}", {count, item});
    notice.classList.add('show');
    clearTimeout(timer);
    timer = setTimeout(() => notice.classList.remove('show'), 2200);
  }

  window.setPostCraft = payload => {
    const made = parseInt(String(payload).split(',')[1], 10);
    const pending = _pendingCraft;
    completeCraft(payload);
    if (pending && !_pendingCraft && Number.isFinite(made) && made > 0)
      show(made, pending.name || pcText("ITEM"));
  };
})();
