import { request } from '@/utils/request'

const onboarding = {
    queryOnboardingStatus() {
        return request({
            url: '/gtw/cwai/Onboarding/Status',
            method: 'post',
        })
    },

    completeOnboarding() {
        return request({
            url: '/gtw/cwai/Onboarding/Complete',
            method: 'post',
        })
    },

    resetOnboarding() {
        return request({
            url: '/gtw/cwai/Onboarding/Reset',
            method: 'post',
        })
    },
}

export default onboarding
