#include <cassert>
#include <cstdlib>

#include "util/BenchmarkIsolation.h"

int main() {
    using cosmo::util::BenchmarkIsolationEnabled;
    using cosmo::util::BenchmarkRouteAllowed;
    assert(BenchmarkRouteAllowed("/gtw/cwai/login/dologin"));
    assert(BenchmarkRouteAllowed("/gtw/cwai/login/logout"));
    assert(BenchmarkRouteAllowed("/gtw/cwai/model/querygraphmodelcandidates"));
    assert(BenchmarkRouteAllowed("/gtw/cwai/task/saveorupdate"));
    assert(!BenchmarkRouteAllowed("/gtw/cwai/system/reboot"));
    assert(!BenchmarkRouteAllowed("/gtw/cwai/model/unknown"));
    assert(::setenv("COSMO_BENCHMARK_ISOLATION", "1", 1) == 0);
#ifdef COSMO_DEV_MODE
    assert(BenchmarkIsolationEnabled());
#else
    assert(!BenchmarkIsolationEnabled());
#endif
    assert(::unsetenv("COSMO_BENCHMARK_ISOLATION") == 0);
    assert(!BenchmarkIsolationEnabled());
}
