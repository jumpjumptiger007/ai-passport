export function normalizeMessageId(value: number): number {
  const normalized = value & 0xffff;
  return normalized === 0 ? 1 : normalized;
}

export function nextMessageId(current: number): number {
  return normalizeMessageId((normalizeMessageId(current) + 1) & 0xffff);
}

export function randomMessageId(random = crypto.getRandomValues(new Uint16Array(1))[0]): number {
  return normalizeMessageId(random);
}
