/**
 * Place a fixed preview card next to its trigger, clamped to the viewport.
 * The width comes from the card's own classes, so it can't drift from the CSS.
 */
export function placePreviewCard(trigger: HTMLElement, card: HTMLElement) {
  const margin = 12;
  const rect = trigger.getBoundingClientRect();
  const cardWidth = card.offsetWidth;
  const height = card.offsetHeight;
  let left = rect.right - cardWidth;
  left = Math.max(margin, Math.min(left, window.innerWidth - cardWidth - margin));
  const preferBelow = window.innerWidth < 640;
  let below = preferBelow;
  let top = below ? rect.bottom + 10 : rect.top - height - 10;

  if (top < margin) {
    top = rect.bottom + 10;
    below = true;
  }

  if (top + height > window.innerHeight - margin) {
    top = Math.max(margin, window.innerHeight - height - margin);
  }

  if (!below && top + height > rect.top - 4) {
    top = Math.min(rect.bottom + 10, window.innerHeight - height - margin);
    below = true;
  }

  card.style.left = `${left}px`;
  card.style.top = `${top}px`;
  card.style.setProperty("--card-shift", below ? "-6px" : "6px");
  card.style.setProperty("--card-origin", below ? "top right" : "bottom right");
}

interface PreviewOptions {
  trigger: HTMLElement;
  card: HTMLElement;
  close?: HTMLElement | null;
  /** Thumbnails stay open on click; note markers navigate instead. */
  pinOnClick?: boolean;
}

/**
 * Hover, focus, click, escape and scroll for one preview card. The root holds
 * `data-open`; the card and trigger read it through Tailwind group variants.
 */
export function wirePreviewCard(
  root: HTMLElement,
  { trigger, card, close: closeButton, pinOnClick = false }: PreviewOptions,
) {
  const hoverable = window.matchMedia("(hover: hover)");
  let pinned = false;
  let hoverTimer = 0;
  let closeTimer = 0;
  let ignoreScrollUntil = 0;
  let suppressFocusOpen = false;

  const isOpen = () => root.hasAttribute("data-open");

  function syncExpanded() {
    if (trigger.hasAttribute("aria-expanded")) {
      trigger.setAttribute("aria-expanded", String(isOpen()));
    }
  }

  function open() {
    window.clearTimeout(closeTimer);
    if (isOpen()) return;
    placePreviewCard(trigger, card);
    root.setAttribute("data-open", "");
    syncExpanded();
  }

  function close() {
    if (!isOpen()) return;
    root.removeAttribute("data-open");
    card.style.removeProperty("left");
    card.style.removeProperty("top");
    card.style.removeProperty("--card-shift");
    card.style.removeProperty("--card-origin");
    syncExpanded();
  }

  function forceClose() {
    pinned = false;
    window.clearTimeout(hoverTimer);
    window.clearTimeout(closeTimer);
    close();
  }

  function returnFocus() {
    if (document.activeElement === trigger) return;
    suppressFocusOpen = true;
    trigger.focus();
  }

  if (hoverable.matches) {
    trigger.addEventListener("mouseenter", () => {
      window.clearTimeout(closeTimer);
      hoverTimer = window.setTimeout(open, 140);
    });
    root.addEventListener("mouseleave", () => {
      window.clearTimeout(hoverTimer);
      if (!pinned) closeTimer = window.setTimeout(close, 180);
    });
    card.addEventListener("mouseenter", () => window.clearTimeout(closeTimer));
    card.addEventListener("mouseleave", () => {
      if (!pinned) closeTimer = window.setTimeout(close, 180);
    });
  }

  trigger.addEventListener("focus", () => {
    if (suppressFocusOpen) {
      suppressFocusOpen = false;
      return;
    }
    ignoreScrollUntil = performance.now() + 500;
    open();
  });
  trigger.addEventListener("click", () => {
    if (!pinOnClick) {
      forceClose();
      return;
    }
    if (pinned) forceClose();
    else {
      pinned = true;
      open();
    }
  });
  root.addEventListener("focusout", (event) => {
    if (!root.contains(event.relatedTarget as Node | null)) {
      if (!pinned) closeTimer = window.setTimeout(close, 120);
    }
  });
  closeButton?.addEventListener("click", (event) => {
    event.preventDefault();
    event.stopPropagation();
    forceClose();
    returnFocus();
  });

  document.addEventListener("pointerdown", (event) => {
    if (!root.contains(event.target as Node)) forceClose();
  });
  document.addEventListener("keydown", (event) => {
    if (event.key !== "Escape" || !isOpen()) return;
    forceClose();
    returnFocus();
  });
  window.addEventListener("resize", forceClose);
  window.addEventListener(
    "scroll",
    () => {
      if (performance.now() < ignoreScrollUntil) {
        if (isOpen()) placePreviewCard(trigger, card);
        return;
      }
      forceClose();
    },
    { passive: true },
  );
}

export function initPreviewCards(root: ParentNode = document) {
  for (const element of root.querySelectorAll<HTMLElement>("[data-preview-root]")) {
    if (element.hasAttribute("data-preview-ready")) continue;
    const trigger = element.querySelector<HTMLElement>("[data-preview-trigger]");
    const card = element.querySelector<HTMLElement>("[data-preview-card]");
    if (!trigger || !card) continue;
    element.setAttribute("data-preview-ready", "");
    wirePreviewCard(element, {
      trigger,
      card,
      close: card.querySelector<HTMLElement>("[data-preview-close]"),
      pinOnClick: element.hasAttribute("data-preview-pinned"),
    });
  }
}
