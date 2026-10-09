import { readonly, shallowRef } from 'vue'

const controller = window.cosmoAppearance
const state = shallowRef(controller.getState())
controller.subscribe((value) => { state.value = value })

// A single document preference. Changing it never rekeys or remounts a route.
export function useAppearance() {
  return { appearance: readonly(state), setAppearance: controller.setPreference }
}
