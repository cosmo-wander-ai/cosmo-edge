#pragma once
#include <string_view>
#if defined(COSMO_AXERA_PHONE_ROUTE_CANDIDATE) && !defined(COSMO_AXERA_HELMET_ROUTE_CANDIDATE)
#error "phone candidate requires the native cascade lifetime/transport implementation"
#endif
#include "util/SleepRoutePolicy.h"
#if defined(COSMO_AXERA_FALL_ROUTE_CANDIDATE) && !defined(COSMO_AXERA_HELMET_ROUTE_CANDIDATE)
#error "fall candidate requires the native cascade lifetime/transport implementation"
#endif
#if defined(COSMO_AXERA_FALL_ROUTE_CANDIDATE) &&                                                             \
    (defined(COSMO_AXERA_FALL_TEST_ONLY) == defined(COSMO_AXERA_FALL_ROUTE_PRODUCTION))
#error "fall route requires exactly one explicit test-only or production gate"
#endif
#if defined(COSMO_AXERA_FALL_ROUTE_PRODUCTION) && !defined(COSMO_AXERA_FALL_ROUTE_CANDIDATE)
#error "fall production gate requires the native fall route"
#endif
#if defined(COSMO_AXERA_FALL_ROUTE_PRODUCTION) && defined(COSMO_AXERA_FALL_DIAGNOSTIC_ONLY)
#error "fall production route cannot enable private diagnostic instrumentation"
#endif
namespace cosmo::native_cascade {
static inline bool ProtectedScene(std::string_view id) noexcept {
    return id == "15"
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
           || id == "930007463"
#endif
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
           || id == "88118" || id == "88120" || id == "58" || id == "32"
#endif
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
           || id == "88117"
#endif
#ifdef COSMO_AXERA_FACEQUALITY_ROUTE_CANDIDATE
           || id == "88119"
#endif
#ifdef COSMO_AXERA_MASK_ROUTE_CANDIDATE
           || id == "88110"
#endif
#ifdef COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE
           || id == "88112"
#endif
#ifdef COSMO_AXERA_ELECTRIC_ROUTE_CANDIDATE
           || id == "88111"
#endif
#ifdef COSMO_AXERA_VEHICLECOLOR_ROUTE_CANDIDATE
           || id == "88113" || id == "88114"
#endif
#ifdef COSMO_AXERA_GARMENT_ROUTE_CANDIDATE
           || id == "88115" || id == "88116"
#endif
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
           || id == "11" || id == "12" || id == "22"
#endif
#ifdef COSMO_AXERA_FALL_ROUTE_CANDIDATE
           || id == "6"
#endif
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
           || id == "65"
#endif
        ;
}
}  // namespace cosmo::native_cascade
