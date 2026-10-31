import {
  completionsFor,
  type CompletionItem,
  type CompletionResult,
  type CompletionSchema,
} from './autocomplete';

const POPUP_WIDTH = 300;

function caretPosition(
  textarea: HTMLTextAreaElement,
  position: number
): { left: number; top: number; lineHeight: number } {
  const style = getComputedStyle(textarea);
  const mirror = document.createElement('div');
  mirror.style.position = 'fixed';
  mirror.style.left = '-10000px';
  mirror.style.top = '0';
  mirror.style.width = `${textarea.clientWidth}px`;
  mirror.style.height = 'auto';
  mirror.style.visibility = 'hidden';
  mirror.style.overflow = 'hidden';
  mirror.style.boxSizing = 'border-box';
  mirror.style.whiteSpace = 'pre-wrap';
  mirror.style.overflowWrap = 'anywhere';
  mirror.style.wordBreak = 'break-word';
  mirror.style.fontFamily = style.fontFamily;
  mirror.style.fontSize = style.fontSize;
  mirror.style.fontWeight = style.fontWeight;
  mirror.style.fontStyle = style.fontStyle;
  mirror.style.letterSpacing = style.letterSpacing;
  mirror.style.lineHeight = style.lineHeight;
  mirror.style.textTransform = style.textTransform;
  mirror.style.tabSize = style.tabSize;
  mirror.style.padding = style.padding;
  mirror.style.border = '0';
  mirror.textContent = textarea.value.slice(0, position);
  const marker = document.createElement('span');
  marker.textContent = '\u200b';
  mirror.append(marker);
  document.body.append(mirror);

  const mirrorRect = mirror.getBoundingClientRect();
  const markerRect = marker.getBoundingClientRect();
  const parsedLineHeight = Number.parseFloat(style.lineHeight);
  const lineHeight = Number.isFinite(parsedLineHeight)
    ? parsedLineHeight
    : Number.parseFloat(style.fontSize) * 1.4;
  const left =
    textarea.getBoundingClientRect().left +
    textarea.clientLeft +
    markerRect.left -
    mirrorRect.left -
    textarea.scrollLeft;
  const top =
    textarea.getBoundingClientRect().top +
    textarea.clientTop +
    markerRect.top -
    mirrorRect.top -
    textarea.scrollTop +
    lineHeight;
  mirror.remove();
  return { left, top, lineHeight };
}

function kindLabel(item: CompletionItem): string {
  switch (item.kind) {
    case 'keyword':
      return 'keyword';
    case 'function':
      return 'fn';
    case 'label':
      return 'label';
    case 'relationship':
      return 'rel';
    case 'property':
      return 'prop';
    case 'procedure':
      return 'proc';
  }
}

export function attachCypherAutocomplete(
  textarea: HTMLTextAreaElement,
  schemaProvider: () => CompletionSchema
): void {
  const popup = document.createElement('div');
  popup.className = 'cypher-completion hidden';
  popup.setAttribute('role', 'listbox');
  document.body.append(popup);

  let current: CompletionResult | null = null;
  let selected = 0;

  function close(): void {
    current = null;
    selected = 0;
    popup.classList.add('hidden');
    popup.replaceChildren();
  }

  function position(): void {
    if (popup.classList.contains('hidden')) return;
    const caret = caretPosition(textarea, textarea.selectionStart);
    const maxLeft = Math.max(8, window.innerWidth - POPUP_WIDTH - 8);
    const left = Math.max(8, Math.min(caret.left, maxLeft));
    let top = caret.top + 4;
    if (top + popup.offsetHeight > window.innerHeight - 8) {
      top = Math.max(8, caret.top - popup.offsetHeight - caret.lineHeight);
    }
    popup.style.left = `${left}px`;
    popup.style.top = `${top}px`;
  }

  function render(): void {
    if (!current || current.items.length === 0) {
      close();
      return;
    }
    popup.replaceChildren();
    current.items.forEach((item, index) => {
      const row = document.createElement('button');
      row.type = 'button';
      row.className = `cypher-completion-item${index === selected ? ' selected' : ''}`;
      row.setAttribute('role', 'option');
      row.setAttribute('aria-selected', index === selected ? 'true' : 'false');
      row.title = item.detail;
      const kind = document.createElement('span');
      kind.className = `cypher-completion-kind kind-${item.kind}`;
      kind.textContent = kindLabel(item);
      const text = document.createElement('span');
      text.className = 'cypher-completion-text';
      text.textContent = item.text;
      const detail = document.createElement('span');
      detail.className = 'cypher-completion-detail';
      detail.textContent = item.detail;
      row.append(kind, text, detail);
      row.addEventListener('pointerdown', (event) => {
        event.preventDefault();
      });
      row.addEventListener('click', () => accept(index));
      popup.append(row);
    });
    popup.classList.remove('hidden');
    position();
  }

  function show(force = false): void {
    if (!textarea.isConnected) return;
    const result = completionsFor(
      textarea.value,
      textarea.selectionStart,
      schemaProvider(),
      force
    );
    if (result.items.length === 0) {
      close();
      return;
    }
    current = result;
    selected = 0;
    render();
  }

  function accept(index: number): void {
    const result = current;
    const item = result?.items[index];
    if (!result || !item) return;
    const start = result.start;
    textarea.focus();
    textarea.setRangeText(item.insert, result.start, result.end, 'end');
    const caret = start + (item.caretInside ?? item.insert.length);
    textarea.setSelectionRange(caret, caret);
    close();
    textarea.dispatchEvent(new Event('input', { bubbles: true }));
  }

  textarea.addEventListener('input', () => show(false));
  textarea.addEventListener('click', () => show(false));
  textarea.addEventListener('blur', () => {
    window.setTimeout(() => {
      if (document.activeElement !== textarea) close();
    }, 100);
  });
  textarea.addEventListener('scroll', () => position());
  textarea.addEventListener('keydown', (event) => {
    if ((event.ctrlKey || event.metaKey) && event.code === 'Space') {
      event.preventDefault();
      show(true);
      return;
    }
    if (event.ctrlKey || event.metaKey) return;
    if (!current) return;
    if (event.key === 'ArrowDown') {
      event.preventDefault();
      event.stopPropagation();
      selected = (selected + 1) % current.items.length;
      render();
      return;
    }
    if (event.key === 'ArrowUp') {
      event.preventDefault();
      event.stopPropagation();
      selected = (selected - 1 + current.items.length) % current.items.length;
      render();
      return;
    }
    if (event.key === 'Enter' || event.key === 'Tab') {
      event.preventDefault();
      event.stopPropagation();
      accept(selected);
      return;
    }
    if (event.key === 'Escape') {
      event.preventDefault();
      event.stopPropagation();
      close();
    }
  });
}
