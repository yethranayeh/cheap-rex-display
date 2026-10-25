type SupportState = "yes" | "no";

function currentBrowser(userAgent = navigator.userAgent): string | null {
  // The matrix is desktop browsers. Phones get the footnote instead
  if (/Android|iPhone|iPad|iPod|Mobile/i.test(userAgent)) return null;

  if (/Edg\//.test(userAgent)) return "edge";
  if (/OPR\//.test(userAgent)) return "opera";
  if (/Firefox\//.test(userAgent)) return "firefox";
  if (/Chrome\/|Chromium\//.test(userAgent)) return "chrome";
  if (/Safari\//.test(userAgent)) return "safari";
  return null;
}

/**
 * `[data-when]` holds the copy for each state, `[data-pending]` the
 * no-javascript fallback, and `[data-browser]` the rows to mark with
 * `[data-current]`. Web Serial is only exposed in secure contexts, so
 * `'serial' in navigator` also covers HTTPS.
 */
export function initBrowserSupport(root: ParentNode = document): void {
  const current = currentBrowser();

  for (const support of root.querySelectorAll<HTMLElement>("[data-browser-support]")) {
    const api = support.dataset.api;
    const state: SupportState | null = api ? (api in navigator ? "yes" : "no") : null;

    for (const el of support.querySelectorAll<HTMLElement>("[data-pending]")) {
      el.hidden = state !== null;
    }

    for (const el of support.querySelectorAll<HTMLElement>("[data-when]")) {
      el.hidden = el.dataset.when !== state;
    }

    for (const button of support.querySelectorAll<HTMLButtonElement>("[data-install]")) {
      const blocked = state === "no";
      button.disabled = blocked;

      const reason = support.querySelector<HTMLElement>("[data-blocked-reason]");
      if (blocked && reason?.id) button.setAttribute("aria-describedby", reason.id);
      else button.removeAttribute("aria-describedby");
    }
  }

  for (const row of root.querySelectorAll<HTMLElement>("[data-browser]")) {
    row.toggleAttribute("data-current", current !== null && row.dataset.browser === current);
  }
}
