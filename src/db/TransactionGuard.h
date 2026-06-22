// RAII transaction guard — ensures Rollback on exception.
// Works with any type that has Begin()/Commit()/Rollback() methods.
#pragma once

#include "util/Log.h"

namespace cosmo::db {

template <typename T>
class TransactionGuard {
public:
    explicit TransactionGuard(T& svc) : svc_(svc), committed_(false) {
        svc_.Begin();
    }

    ~TransactionGuard() {
        if (!committed_) {
            try {
                svc_.Rollback();
            } catch (const std::exception& e) {
                LOG_WARN("TransactionGuard rollback failed: {}", e.what());
            }
        }
    }

    void Commit() {
        svc_.Commit();
        committed_ = true;
    }

    TransactionGuard(const TransactionGuard&)            = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;

private:
    T& svc_;
    bool committed_;
};

}  // namespace cosmo::db
