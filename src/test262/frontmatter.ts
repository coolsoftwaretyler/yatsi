import type { TestMetadata } from '../types/test262';

const FRONTMATTER_RE = /\/\*---\s*\n([\s\S]*?)\n---\*\//;

export function parseFrontmatter(source: string): TestMetadata {
  const match = source.match(FRONTMATTER_RE);
  if (!match) {
    return emptyMetadata();
  }

  const yaml = match[1];
  return parseYaml(yaml);
}

function emptyMetadata(): TestMetadata {
  return {
    description: '',
    features: [],
    flags: [],
    includes: [],
    negative: null,
    locale: [],
  };
}

/**
 * Minimal YAML parser for test262 frontmatter.
 * Handles: scalar values, lists (both inline [...] and block - item), and the `negative` nested object.
 */
function parseYaml(yaml: string): TestMetadata {
  const meta = emptyMetadata();
  const lines = yaml.split('\n');
  let i = 0;

  while (i < lines.length) {
    const line = lines[i];
    const keyMatch = line.match(/^(\w+)\s*:\s*(.*)/);

    if (!keyMatch) {
      i++;
      continue;
    }

    const key = keyMatch[1];
    let value = keyMatch[2].trim();

    if (key === 'negative') {
      // Parse nested object
      const neg: Record<string, string> = {};
      i++;
      while (i < lines.length) {
        const nested = lines[i].match(/^\s+(\w+)\s*:\s*(.*)/);
        if (!nested) break;
        neg[nested[1]] = nested[2].trim();
        i++;
      }
      if (neg.phase && neg.type) {
        meta.negative = { phase: neg.phase, type: neg.type };
      }
      continue;
    }

    // Inline list: [item1, item2]
    if (value.startsWith('[') && value.endsWith(']')) {
      const items = value.slice(1, -1).split(',').map(s => s.trim()).filter(Boolean);
      setListField(meta, key, items);
      i++;
      continue;
    }

    // Block list
    if (value === '') {
      const items: string[] = [];
      i++;
      while (i < lines.length) {
        const itemMatch = lines[i].match(/^\s+-\s+(.*)/);
        if (!itemMatch) break;
        items.push(itemMatch[1].trim());
        i++;
      }
      if (items.length > 0) {
        setListField(meta, key, items);
      }
      continue;
    }

    // Scalar
    if (key === 'description') {
      meta.description = value;
    }

    // Some scalar fields may also be list fields with a single entry
    if (['features', 'flags', 'includes', 'locale'].includes(key)) {
      setListField(meta, key, [value]);
    }

    i++;
  }

  return meta;
}

function setListField(meta: TestMetadata, key: string, items: string[]) {
  switch (key) {
    case 'features':
      meta.features = items;
      break;
    case 'flags':
      meta.flags = items;
      break;
    case 'includes':
      meta.includes = items;
      break;
    case 'locale':
      meta.locale = items;
      break;
  }
}
