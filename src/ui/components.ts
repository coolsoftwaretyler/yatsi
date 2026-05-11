export function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  attrs?: Record<string, string>,
  ...children: (Node | string)[]
): HTMLElementTagNameMap[K] {
  const elem = document.createElement(tag);
  if (attrs) {
    for (const [key, val] of Object.entries(attrs)) {
      if (key === 'className') {
        elem.className = val;
      } else {
        elem.setAttribute(key, val);
      }
    }
  }
  for (const child of children) {
    if (typeof child === 'string') {
      elem.appendChild(document.createTextNode(child));
    } else {
      elem.appendChild(child);
    }
  }
  return elem;
}

export function badge(text: string, className: string): HTMLSpanElement {
  return el('span', { className: `badge ${className}` }, text);
}

export function clearChildren(elem: HTMLElement): void {
  while (elem.firstChild) {
    elem.removeChild(elem.firstChild);
  }
}
