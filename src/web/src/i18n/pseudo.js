const ACCENT_MAP = {
  A: 'Å', B: 'Ɓ', C: 'Ç', D: 'Ð', E: 'É', F: 'Ƒ', G: 'Ğ', H: 'Ħ', I: 'Ï', J: 'Ĵ', K: 'Ķ', L: 'Ŀ', M: 'Ḿ',
  N: 'Ń', O: 'Ø', P: 'Ƥ', Q: 'Ǫ', R: 'Ŕ', S: 'Ş', T: 'Ŧ', U: 'Û', V: 'Ṽ', W: 'Ŵ', X: 'Ẋ', Y: 'Ý', Z: 'Ž',
  a: 'å', b: 'ƀ', c: 'ç', d: 'ð', e: 'é', f: 'ƒ', g: 'ğ', h: 'ħ', i: 'ï', j: 'ĵ', k: 'ķ', l: 'ŀ', m: 'ḿ',
  n: 'ñ', o: 'ø', p: 'ƥ', q: 'ǫ', r: 'ŕ', s: 'ş', t: 'ŧ', u: 'û', v: 'ṽ', w: 'ŵ', x: 'ẋ', y: 'ý', z: 'ž'
}

const PROTECTED_PATTERN = /(\{[^}]*\}|&[a-z]+;)/gi
const PROTECTED_SEGMENT_PATTERN = /^(\{[^}]*\}|&[a-z]+;)$/i
const FILLER = 'ḓƒ'

// TODO(i18n): before ICU plural lands in Phase 2, replace this simple
// brace matcher with an ICU-aware segmenter that handles nested braces.
const splitProtected = (value) => String(value).split(PROTECTED_PATTERN)

const stableRatio = (value) => {
  let hash = 0
  for (const char of String(value)) {
    hash = (hash * 31 + char.charCodeAt(0)) >>> 0
  }
  return 0.3 + (hash % 21) / 100
}

const isProtectedSegment = (value) => PROTECTED_SEGMENT_PATTERN.test(value)

const accent = (value) => value.replace(/[A-Za-z]/g, char => ACCENT_MAP[char] || char)

const expand = (value, source) => {
  const targetExtra = Math.ceil(String(source).length * stableRatio(source))
  return value + FILLER.repeat(Math.ceil(targetExtra / FILLER.length)).slice(0, targetExtra)
}

const pseudoString = (value) => {
  const transformed = splitProtected(value)
    .map(part => isProtectedSegment(part) ? part : accent(part))
    .join('')
  return `[${expand(transformed, value)}]`
}

export const createPseudoMessages = (messages) => {
  if (typeof messages === 'string') {
    return pseudoString(messages)
  }
  if (Array.isArray(messages)) {
    return messages.map(createPseudoMessages)
  }
  if (messages && typeof messages === 'object') {
    return Object.fromEntries(
      Object.entries(messages).map(([key, value]) => [key, createPseudoMessages(value)])
    )
  }
  return messages
}
