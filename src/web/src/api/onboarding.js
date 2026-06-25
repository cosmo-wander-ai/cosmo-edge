import { request } from '@/utils/request'

/**
 * Onboarding Guide Wizard API module
 */
const onboarding = {
    /**
     * Query onboarding completion status.
     * @returns {Promise} { resData: { onboardingCompleted: boolean } }
     */
    queryOnboardingStatus() {
        return request({
            url: '/gtw/cwai/Onboarding/Status',
            method: 'post',
        })
    },

    /**
     * Mark onboarding as completed (persisted across reboots).
     * @returns {Promise}
     */
    completeOnboarding() {
        return request({
            url: '/gtw/cwai/Onboarding/Complete',
            method: 'post',
        })
    },
}

export default onboarding
