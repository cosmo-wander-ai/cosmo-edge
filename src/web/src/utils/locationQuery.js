const currentLocation = () => {
  return typeof window === 'undefined' ? {} : window.location
}

const mergeSearchParams = (target, search) => {
  const query = String(search || '').replace(/^\?/, '')
  if (!query) return

  for (const [key, value] of new URLSearchParams(query).entries()) {
    target[key] = value
  }
}

export const parseLocationQuery = (location = currentLocation()) => {
  const params = {}
  mergeSearchParams(params, location?.search)

  const hash = String(location?.hash || '')
  const queryIndex = hash.indexOf('?')
  if (queryIndex >= 0) {
    // Hash-route parameters describe the active page and must override outer
    // cache-busting parameters such as ?upgrade=<timestamp>.
    mergeSearchParams(params, hash.slice(queryIndex + 1))
  }

  return params
}

export const getLocationQueryParam = (name, location = currentLocation()) => {
  const params = parseLocationQuery(location)
  return name ? params[name] : params
}
