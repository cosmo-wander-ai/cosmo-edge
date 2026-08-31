export const getLocationParam = (location, name) => {
  const params = new URLSearchParams(
    String(location?.search || '').replace(/^\?/, '')
  )
  const hash = String(location?.hash || '')
  const hashQueryStart = hash.indexOf('?')
  if (hashQueryStart >= 0) {
    const hashParams = new URLSearchParams(hash.slice(hashQueryStart + 1))
    for (const [key, value] of hashParams.entries()) {
      params.set(key, value)
    }
  }

  if (!name) return Object.fromEntries(params.entries())
  return params.has(name) ? params.get(name) : undefined
}
