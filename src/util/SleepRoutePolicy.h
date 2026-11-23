#pragma once
// Independent opt-in gate; does not register a route or authorize a task.
#if defined(COSMO_AXERA_SLEEP_ROUTE_CANDIDATE) && !defined(COSMO_AXERA_HELMET_ROUTE_CANDIDATE)
#error "sleep candidate requires native cascade transport and lifetime support"
#endif
#if defined(COSMO_AXERA_SLEEP_ROUTE_CANDIDATE) &&                                                            \
    (defined(COSMO_AXERA_SLEEP_ROUTE_TEST_ONLY) == defined(COSMO_AXERA_SLEEP_ROUTE_PRODUCTION))
#error "sleep candidate requires exactly one explicit test-only or production gate"
#endif
#if (defined(COSMO_AXERA_SLEEP_ROUTE_TEST_ONLY) || defined(COSMO_AXERA_SLEEP_ROUTE_PRODUCTION)) &&           \
    !defined(COSMO_AXERA_SLEEP_ROUTE_CANDIDATE)
#error "sleep gate requires sleep candidate"
#endif
#if defined(COSMO_AXERA_SLEEP_ROUTE_PRODUCTION) && defined(COSMO_AXERA_SLEEP_DIAGNOSTIC_ONLY)
#error "sleep production cannot include private diagnostic instrumentation"
#endif
