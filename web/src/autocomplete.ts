export type CompletionKind = 'keyword' | 'function' | 'label' | 'relationship' | 'property';

export type CompletionSchema = {
  labels: string[];
  relationships: string[];
  properties: string[];
};

export type CompletionItem = {
  text: string;
  insert: string;
  kind: CompletionKind;
  detail: string;
  /** Caret offset from the start of the inserted text. */
  caretInside?: number;
};

export type CompletionResult = {
  start: number;
  end: number;
  items: CompletionItem[];
  context: 'general' | 'label' | 'relationship' | 'property';
};

const KEYWORDS: Array<[string, string]> = [
  ['MATCH', '匹配模式'],
  ['OPTIONAL MATCH', '可选匹配'],
  ['WHERE', '过滤条件'],
  ['RETURN', '返回结果'],
  ['WITH', '传递结果'],
  ['CREATE', '创建节点/关系'],
  ['MERGE', '匹配或创建'],
  ['DELETE', '删除'],
  ['DETACH DELETE', '删除节点及关系'],
  ['SET', '设置属性/标签'],
  ['REMOVE', '移除属性/标签'],
  ['UNWIND', '展开列表'],
  ['CALL', '调用过程'],
  ['YIELD', '接收过程结果'],
  ['ORDER BY', '排序'],
  ['SKIP', '跳过行'],
  ['LIMIT', '限制行数'],
  ['DISTINCT', '去重'],
  ['AS', '别名'],
  ['UNION', '合并结果'],
  ['UNION ALL', '合并结果并保留重复'],
  ['CASE', '条件表达式'],
  ['WHEN', '条件分支'],
  ['THEN', '分支结果'],
  ['ELSE', '默认分支'],
  ['END', '结束 CASE'],
  ['AND', '逻辑与'],
  ['OR', '逻辑或'],
  ['XOR', '逻辑异或'],
  ['NOT', '逻辑非'],
  ['IN', '列表包含'],
  ['IS NULL', '为空'],
  ['IS NOT NULL', '非空'],
  ['STARTS WITH', '前缀匹配'],
  ['ENDS WITH', '后缀匹配'],
  ['CONTAINS', '包含文本'],
  ['TRUE', '布尔真'],
  ['FALSE', '布尔假'],
  ['NULL', '空值'],
];

const FUNCTIONS: Array<[string, string]> = [
  ['count', '计数'],
  ['sum', '求和'],
  ['avg', '平均值'],
  ['min', '最小值'],
  ['max', '最大值'],
  ['collect', '收集为列表'],
  ['size', '列表/字符串长度'],
  ['length', '路径/字符串长度'],
  ['toUpper', '转大写'],
  ['toLower', '转小写'],
  ['trim', '去空白'],
  ['substring', '截取字符串'],
  ['replace', '替换字符串'],
  ['split', '拆分字符串'],
  ['coalesce', '取第一个非空值'],
  ['head', '列表首项'],
  ['last', '列表末项'],
  ['nodes', '路径节点'],
  ['relationships', '路径关系'],
  ['type', '关系类型'],
  ['id', '内部 id'],
  ['elementId', '元素 id'],
  ['labels', '节点标签'],
  ['keys', '属性键'],
  ['properties', '属性映射'],
  ['exists', '是否存在'],
  ['timestamp', '当前时间戳'],
  ['date', '日期'],
  ['datetime', '日期时间'],
  ['toString', '转字符串'],
  ['toInteger', '转整数'],
  ['toFloat', '转浮点数'],
  ['abs', '绝对值'],
  ['round', '四舍五入'],
  ['ceil', '向上取整'],
  ['floor', '向下取整'],
  ['sqrt', '平方根'],
  ['rand', '随机数'],
  ['range', '生成范围'],
];

const KEYWORD_SET = new Set(KEYWORDS.map(([word]) => word));
const FUNCTION_SET = new Set(FUNCTIONS.map(([name]) => name));

function normalize(value: string): string {
  return value.toLowerCase().replaceAll('`', '');
}

function quoteIdentifier(value: string): string {
  return /^[A-Za-z_][A-Za-z0-9_]*$/.test(value) ? value : `\`${value.replaceAll('`', '``')}\``;
}

function tokenAt(input: string, caret: number): { start: number; end: number; prefix: string } {
  const before = input.slice(0, caret);
  const match = /([A-Za-z_$][A-Za-z0-9_$]*)$/.exec(before);
  if (!match) return { start: caret, end: caret, prefix: '' };
  return { start: caret - match[0].length, end: caret, prefix: match[0] };
}

function contextAt(
  input: string,
  start: number,
  prefix: string
): { context: CompletionResult['context']; forcePrefix: boolean } {
  const before = input.slice(0, start);
  const previous = before.at(-1);
  if (previous === '.') return { context: 'property', forcePrefix: true };
  if (previous === ':') {
    const bracket = before.lastIndexOf('[');
    const paren = before.lastIndexOf('(');
    return {
      context: bracket > paren ? 'relationship' : 'label',
      forcePrefix: true,
    };
  }
  return { context: 'general', forcePrefix: prefix.length > 0 };
}

function itemRank(item: CompletionItem, prefix: string, context: CompletionResult['context']): number {
  const text = normalize(item.text);
  const normalizedPrefix = normalize(prefix);
  let rank = text.startsWith(normalizedPrefix) ? 0 : text.includes(normalizedPrefix) ? 100 : 1000;
  if (context === 'general') {
    if (item.kind === 'keyword') rank += 0;
    else if (item.kind === 'function') rank += 10;
    else if (item.kind === 'label') rank += 40;
    else if (item.kind === 'relationship') rank += 50;
    else rank += 60;
  } else {
    rank += item.kind === 'label' || item.kind === 'relationship' || item.kind === 'property' ? 0 : 100;
  }
  return rank + (item.text.length > 40 ? 5 : 0);
}

export function completionsFor(
  input: string,
  caret: number,
  schema: CompletionSchema,
  force = false
): CompletionResult {
  const boundedCaret = Math.max(0, Math.min(caret, input.length));
  const { start, end, prefix } = tokenAt(input, boundedCaret);
  const { context, forcePrefix } = contextAt(input, start, prefix);
  if (prefix.length === 0 && !forcePrefix && !force) {
    return { start, end, items: [], context };
  }

  const items: CompletionItem[] = [];
  const push = (item: CompletionItem): void => {
    if (prefix.length > 0 && !normalize(item.text).includes(normalize(prefix))) return;
    if (prefix.length === 0 && context !== 'general' && !force) {
      items.push(item);
      return;
    }
    items.push(item);
  };

  if (context === 'label') {
    for (const label of schema.labels) {
      push({ text: label, insert: quoteIdentifier(label), kind: 'label', detail: 'label' });
    }
  } else if (context === 'relationship') {
    for (const relationship of schema.relationships) {
      push({
        text: relationship,
        insert: quoteIdentifier(relationship),
        kind: 'relationship',
        detail: 'relationship type',
      });
    }
  } else if (context === 'property') {
    for (const property of schema.properties) {
      push({ text: property, insert: quoteIdentifier(property), kind: 'property', detail: 'property' });
    }
  } else {
    for (const [keyword, detail] of KEYWORDS) {
      push({ text: keyword, insert: `${keyword} `, kind: 'keyword', detail });
    }
    for (const [name, detail] of FUNCTIONS) {
      push({
        text: name,
        insert: `${name}()`,
        kind: 'function',
        detail,
        caretInside: name.length + 1,
      });
    }
    for (const label of schema.labels) {
      push({ text: label, insert: quoteIdentifier(label), kind: 'label', detail: 'label' });
    }
    for (const relationship of schema.relationships) {
      push({
        text: relationship,
        insert: quoteIdentifier(relationship),
        kind: 'relationship',
        detail: 'relationship type',
      });
    }
    for (const property of schema.properties) {
      push({ text: property, insert: quoteIdentifier(property), kind: 'property', detail: 'property' });
    }
  }

  if (prefix.length === 0 && !force) {
    return { start, end, items: items.slice(0, 12), context };
  }

  const deduped = new Map<string, CompletionItem>();
  for (const item of items) {
    const key = `${item.kind}:${item.text}`;
    if (!deduped.has(key)) deduped.set(key, item);
  }

  const sorted = [...deduped.values()].sort((a, b) => {
    const rank = itemRank(a, prefix, context) - itemRank(b, prefix, context);
    if (rank !== 0) return rank;
    return a.text.localeCompare(b.text);
  });
  return { start, end, items: sorted.slice(0, 12), context };
}

export function completionKeywords(): string[] {
  return [...KEYWORD_SET];
}

export function completionFunctions(): string[] {
  return [...FUNCTION_SET];
}
