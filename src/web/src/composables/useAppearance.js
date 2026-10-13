import { readonly, shallowRef } from 'vue'

const controller = window.cosmoAppearance
const state = shallowRef(controller.getState())
controller.subscribe((value) => { state.value = value })

// Document-wide appearance. Changing either choice never rekeys or remounts a route.
export function useAppearance() {
  return { appearance: readonly(state), setAppearance: controller.setPreference, setPalette: controller.setPalette }
}
