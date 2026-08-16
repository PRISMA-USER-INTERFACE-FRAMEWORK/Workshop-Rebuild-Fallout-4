


let ckMode = null;

function setBenchMode(m) {
  const next = (m === "cooking");
  if (ckMode === next) return;

  ckMode = next;
  document.body.classList.toggle("cooking-mode", next);
  const t = document.getElementById("titleMain");
  if (t) t.textContent = next ? "COOKING FIRE" : "PRODUCTION WORKSTATION";
  if (typeof activeTab !== "undefined") { activeTab = "ALL"; activeSub = "ALL"; }

  if (typeof setLockFilter === "function") setLockFilter("ALL", true);
}
window.setBenchMode = setBenchMode;
