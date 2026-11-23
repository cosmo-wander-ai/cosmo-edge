import { reactive } from 'vue'
import { request } from '@/utils/request'

// Keep stored event paths intact for operations such as adding a face to a library.
const cache = reactive({})
const pending = new Map()
const transparent = 'data:image/gif;base64,R0lGODlhAQABAAD/ACwAAAAAAQABAAACADs='
let currentToken = null

export function eventImageUrl(path) {
  if (typeof path !== 'string' || (!path.startsWith('/event/') && !path.startsWith('/weblibPic/facePicture/'))) return path || ''
  const token = localStorage.getItem('mtk')
  if (token !== currentToken) {
    currentToken = token
    pending.clear()
    Object.keys(cache).forEach(key => delete cache[key])
  }
  const cached = cache[path]
  if (cached !== undefined) return cached
  if (!pending.has(path)) {
    const route = path.startsWith('/weblibPic/facePicture/') ? '/gtw/cwai/Library/ReadFaceImage' : '/gtw/cwai/Event/ReadImage'
    const loading = request({ url: route, method: 'post', data: { path },
      silentError: true, suppressAuthRedirect: true })
      .then(data => {
        const image = data.resData?.dataUrl || ''
        if (currentToken === token) cache[path] = image
        return currentToken === token ? image : ''
      })
      .catch(() => {
        if (currentToken === token) cache[path] = ''
        return ''
      })
      .finally(() => {
        if (pending.get(path) === loading) pending.delete(path)
      })
    pending.set(path, loading)
  }
  return transparent
}

// Image viewers need the completed URL, rather than the temporary placeholder.
export function loadEventImageUrl(path) {
  const image = eventImageUrl(path)
  return pending.get(path) || Promise.resolve(image)
}
