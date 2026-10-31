export type CompletionKind =
  | 'keyword'
  | 'function'
  | 'label'
  | 'relationship'
  | 'property'
  | 'procedure';

export type SchemaProperty = {
  name: string;
  type: string;
};

export type CompletionSchema = {
  labels: string[];
  relationships: string[];
  properties: string[];
  labelProperties?: Record<string, SchemaProperty[]>;
  relationshipProperties?: Record<string, SchemaProperty[]>;
  procedures?: Array<{ name: string; detail: string }>;
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
  context: 'general' | 'label' | 'relationship' | 'property' | 'procedure';
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
  ['DESCRIBE', '查看 schema'],
  ['DESC', 'DESCRIBE 别名'],
  ['LABELS', '列出全部标签'],
  ['RELATIONSHIPS', '列出全部关系类型'],
  ['LABEL', '查看标签字段'],
  ['RELATIONSHIP', '查看关系类型字段'],
  ['REL', 'RELATIONSHIP 别名'],
  ['SHOW', '显示元数据'],
  ['INDEXES', '列出索引'],
  ['INDEX', '查看索引'],
  ['PROCEDURES', '列出过程'],
  ['DROP', '删除'],
  ['UNIQUE', '唯一索引'],
  ['FOR', '索引目标'],
  ['ON', '索引字段'],
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
  ['reverse', '反转列表'],
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
  ['time', '时间'],
  ['localdatetime', '本地日期时间'],
  ['localtime', '本地时间'],
  ['duration', '时间段'],
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

function schemaProperties(
  input: string,
  objectName: string,
  schema: CompletionSchema
): SchemaProperty[] {
  const escaped = objectName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const before = input;
  const relationship = new RegExp(`\\[\\s*${escaped}\\s*:\\s*([A-Za-z_][A-Za-z0-9_]*)`).exec(before);
  if (relationship && schema.relationshipProperties?.[relationship[1]]) {
    return schema.relationshipProperties[relationship[1]];
  }
  const node = new RegExp(`\\(\\s*${escaped}\\s*:\\s*([A-Za-z_][A-Za-z0-9_]*)`).exec(before);
  const cast = new RegExp(`${escaped}\\s*::\\s*([A-Za-z_][A-Za-z0-9_]*)`).exec(before);
  const label = cast?.[1] ?? node?.[1];
  if (label && schema.labelProperties?.[label]) return schema.labelProperties[label];
  return [];
}

function allProperties(schema: CompletionSchema): SchemaProperty[] {
  const seen = new Set<string>();
  const result: SchemaProperty[] = [];
  const add = (name: string, type: string): void => {
    if (seen.has(name)) return;
    seen.add(name);
    result.push({ name, type });
  };
  for (const property of schema.properties) add(property, 'ANY');
  for (const list of Object.values(schema.labelProperties ?? {})) {
    for (const property of list) add(property.name, property.type);
  }
  for (const list of Object.values(schema.relationshipProperties ?? {})) {
    for (const property of list) add(property.name, property.type);
  }
  return result;
}

function propertyItems(properties: SchemaProperty[]): CompletionItem[] {
  return properties.map((property) => ({
    text: property.name,
    insert: quoteIdentifier(property.name),
    kind: 'property' as const,
    detail: property.type,
  }));
}

function explicitCastProperties(before: string, schema: CompletionSchema): SchemaProperty[] | null {
  const match = /::\s*([A-Za-z_][A-Za-z0-9_]*)\s*\.\s*$/.exec(before);
  if (!match) return null;
  return schema.labelProperties?.[match[1]] ?? [];
}

function inferPropertyList(
  input: string,
  beforeToken: string,
  schema: CompletionSchema
): SchemaProperty[] {
  const objectMatch = /([A-Za-z_][A-Za-z0-9_]*)\s*\.\s*$/.exec(beforeToken);
  if (objectMatch) {
    const inferred = schemaProperties(input, objectMatch[1], schema);
    if (inferred.length > 0) return inferred;
  }
  return allProperties(schema);
}

function describeContext(before: string): {
  context: CompletionResult['context'];
  prefix: string;
} | null {
  const target = /(?:^|[^\w])(?:DESCRIBE|DESC)\s+(LABEL|RELATIONSHIP|REL)\s+([A-Za-z_$][A-Za-z0-9_$]*)?$/i.exec(
    before
  );
  if (target) {
    return {
      context: target[1].toUpperCase().startsWith('LABEL') ? 'label' : 'relationship',
      prefix: target[2] ?? '',
    };
  }
  return null;
}

export function completionsFor(
  input: string,
  caret: number,
  schema: CompletionSchema,
  force = false
): CompletionResult {
  const boundedCaret = Math.max(0, Math.min(caret, input.length));
  const before = input.slice(0, boundedCaret);

  const call = /(?:^|[^\w])CALL\s+([A-Za-z_][A-Za-z0-9_.]*)$/i.exec(before);
  if (call) {
    const prefix = call[1];
    const start = boundedCaret - prefix.length;
    const procedures = (schema.procedures ?? [])
      .filter((procedure) => normalize(procedure.name).includes(normalize(prefix)))
      .slice(0, 12)
      .map<CompletionItem>((procedure) => ({
        text: procedure.name,
        insert: `${procedure.name}()`,
        kind: 'procedure',
        detail: procedure.detail,
        caretInside: procedure.name.length + 1,
      }));
    return { start, end: boundedCaret, items: procedures, context: 'procedure' };
  }

  const describe = describeContext(before);
  if (describe) {
    const start = boundedCaret - describe.prefix.length;
    const values =
      describe.context === 'label'
        ? schema.labels.map<CompletionItem>((label) => ({
            text: label,
            insert: quoteIdentifier(label),
            kind: 'label',
            detail: 'label',
          }))
        : schema.relationships.map<CompletionItem>((relationship) => ({
            text: relationship,
            insert: quoteIdentifier(relationship),
            kind: 'relationship',
            detail: 'relationship type',
          }));
    return {
      start,
      end: boundedCaret,
      context: describe.context,
      items: values
        .filter((item) => normalize(item.text).includes(normalize(describe.prefix)))
        .slice(0, 12),
    };
  }

  const token = tokenAt(input, boundedCaret);
  const prefix = token.prefix;
  const beforeToken = before.slice(0, token.start);
  const trimmedBeforeToken = beforeToken.replace(/\s+$/, '');
  let context: CompletionResult['context'] = 'general';
  let properties: SchemaProperty[] = [];

  const explicitProperties = explicitCastProperties(trimmedBeforeToken, schema);
  if (trimmedBeforeToken.endsWith('.') || explicitProperties) {
    context = 'property';
    properties =
      explicitProperties ?? inferPropertyList(before, trimmedBeforeToken, schema);
  } else if (trimmedBeforeToken.endsWith('::')) {
    context = 'label';
  } else if (trimmedBeforeToken.endsWith(':')) {
    const bracket = beforeToken.lastIndexOf('[');
    const paren = beforeToken.lastIndexOf('(');
    context = bracket > paren ? 'relationship' : 'label';
  }

  const items: CompletionItem[] = [];
  const push = (item: CompletionItem): void => {
    if (prefix.length > 0 && !normalize(item.text).includes(normalize(prefix))) return;
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
    for (const item of propertyItems(properties)) push(item);
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
    for (const item of propertyItems(allProperties(schema))) push(item);
  }

  if (prefix.length === 0 && !force && !trimmedBeforeToken.endsWith('.') && !trimmedBeforeToken.endsWith(':')) {
    return { start: token.start, end: token.end, items: items.slice(0, 12), context };
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
  return { start: token.start, end: token.end, items: sorted.slice(0, 12), context };
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
    else if (item.kind === 'procedure') rank += 20;
    else rank += 60;
  } else {
    rank += item.kind === 'label' || item.kind === 'relationship' || item.kind === 'property' ? 0 : 100;
  }
  return rank + (item.text.length > 40 ? 5 : 0);
}

export function completionKeywords(): string[] {
  return [...KEYWORD_SET];
}

export function completionFunctions(): string[] {
  return [...FUNCTION_SET];
}
