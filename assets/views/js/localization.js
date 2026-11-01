function pcText(message, vars) {
  const table = window.PrismaL10N;
  const text = table && typeof table.t === 'function' ? table.t(message) : message;
  return String(text).replace(/\{\{([^{}]+)\}\}/g, (match, name) =>
    vars && Object.prototype.hasOwnProperty.call(vars, name) ? String(vars[name]) : match);
}

function applyTranslations() {
  document.querySelectorAll('[data-l10n]').forEach(el => {
    el.textContent = pcText(el.getAttribute('data-l10n'), el.pcTranslationVars);
  });
  ['title', 'placeholder', 'data-pb-label'].forEach(attr => {
    document.querySelectorAll('[data-l10n-' + attr + ']').forEach(el => {
      el.setAttribute(attr, pcText(el.getAttribute('data-l10n-' + attr)));
    });
  });
  if (window.refreshTranslations) window.refreshTranslations();
}
function pcSetText(el, message, vars) {
  if (!el) return;
  el.setAttribute('data-l10n', message);
  el.pcTranslationVars = vars;
  el.textContent = pcText(message, vars);
}
function pcPerkName(perk, name = perk.name) {
  return perk.op ? pcText("{{name}} (RANK {{op}} {{value}})", {name, op: perk.op, value: perk.value}) : name;
}
function pcRequirementText(requirement) {
  const types = {perk: 'PERK RANK', itemcount: 'ITEM COUNT', keyword: 'KEYWORD COUNT', global: 'GLOBAL VALUE'};
  return pcText("{{type}}: {{name}} ({{op}} {{value}}){{missing}}", {
    type: pcText(types[requirement.type] || 'CONDITION'), name: requirement.name || requirement.id,
    op: requirement.op, value: requirement.value,
    missing: requirement.met === false ? pcText(' — NOT MET') : requirement.met === true ? pcText(' — MET') : ''
  });
}
window.applyTranslations = applyTranslations;
