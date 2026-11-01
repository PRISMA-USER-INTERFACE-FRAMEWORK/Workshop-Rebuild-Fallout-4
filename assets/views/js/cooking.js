let ckMode = null;

function setBenchMode(m) {
  const next = (m === "cooking");
  if (ckMode === next) return;

  ckMode = next;
  document.body.classList.toggle("cooking-mode", next);
  updateBenchTitle();
  if (typeof activeTab !== "undefined") { activeTab = "ALL"; activeSub = "ALL"; }

  if (typeof setLockFilter === "function") setLockFilter("ALL", true);
}
function updateBenchTitle() {
  const t = document.getElementById("titleMain");
  if (ckMode !== null) pcSetText(t, ckMode ? "COOKING FIRE" : "PRODUCTION WORKSTATION");
}
window.setBenchMode = setBenchMode;
