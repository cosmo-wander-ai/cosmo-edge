import { request } from '@/utils/request'

/**
 * Onboarding Guide Wizard API module
 *
 * All endpoints avoid exposing local filesystem paths.
 * Demo sandbox construction is handled internally by the backend.
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
     * One-click demo sandbox: backend creates a demo camera channel
     * pointing to the built-in demo video.
     * @returns {Promise} { resData: { cameraId, cameraName, algorithmCode } }
     */
    startDemo() {
        return request({
            url: '/gtw/cwai/Onboarding/StartDemo',
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

    /**
     * Reset the demo sandbox (delete demo camera and associated tasks).
     * Requires authentication (kAuth).
     * @returns {Promise}
     */
    resetDemo() {
        return request({
            url: '/gtw/cwai/Onboarding/ResetDemo',
            method: 'post',
        })
    },
}

export default onboarding
