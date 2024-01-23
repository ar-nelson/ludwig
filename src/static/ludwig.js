htmx.config.allowScriptTags = false;

const components = {};

document.addEventListener('DOMContentLoaded', () => {
  // HTMX error handling
  document.body.addEventListener('htmx:afterRequest', evt => {
    if (evt.detail.successful) return;
    function probablyHtml(s) {
      return !s.trim().length || (s.includes("<") && s.includes(">"));
    }
    const toasts = document.getElementById("toasts");
    const toast = document.createElement("p");
    const xhr = evt.detail.xhr;
    toast.innerText = xhr && xhr.status
      ? (probablyHtml(xhr.responseText) ? `Error ${xhr.status}: ${xhr.statusText}` : xhr.responseText)
      : "Unexpected request error";
    toast.setAttribute("class", "toast toast-error");
    toast.setAttribute("aria-live", "polite");
    const timer = window.setTimeout(() => toasts.removeChild(toast), 30000);
    toast.addEventListener("click", () => {
      toasts.removeChild(toast);
      window.clearTimeout(timer);
    });
    toasts.appendChild(toast);
  });

  // Component system
  for (const el of document.querySelectorAll("[data-component]")) {
    const name = el.getAttribute("data-component"), ctor = components[name];
    if (ctor) {
      try {
        ctor(el);
      } catch (e) {
        console.error(`data-component="${name}" init failed`, e);
      }
    } else console.error(`data-component="${name}" does not match any known component`);
  }
});

// Components
components.Form = el => {
  el.querySelectorAll("input,select,textarea").forEach(input => {
    input.addEventListener("invalid", () => input.classList.add("error"), false);
  });
};
