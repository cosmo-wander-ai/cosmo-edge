// LlmInferServiceImpl — LLM inference service implementation — wraps the Qwen3VL model

#include "service/ai/impl/LlmInferServiceImpl.h"

#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
#include "service/system/IAppInfoService.h"
#include "util/Log.h"

static constexpr const char* kTag = "LlmInferService ";

namespace cosmo::service {

bool LlmInferServiceImpl::EnsureInit(const std::string& atomic_code) {
#ifdef COSMO_NN_USE_CPU_BACKEND
    // LLM inference (Qwen3VL/Qwen3.5) is not supported on x86 platform
    LOG_WARN("{}EnsureInit: LLM inference is not supported on x86 platform (atomicCode: {})", kTag,
             atomic_code);
    return false;
#else
    std::lock_guard<std::mutex> lk(lifecycle_mtx_);

    // Already initialized
    if (inst_)
        return true;

    // Permanent failure until Reset()
    if (init_failed_)
        return false;

    if (atomic_code.empty()) {
        LOG_WARN("{}EnsureInit: atomicCode is empty", kTag);
        init_failed_ = true;
        return false;
    }

    std::string cfg_path, model_path;
    if (!ServiceRegistry::Instance().Get<IModelService>().GetModelCfg(atomic_code, cfg_path, model_path)) {
        LOG_WARN("{}EnsureInit: GetModelCfg failed for {}", kTag, atomic_code);
        init_failed_ = true;
        return false;
    }

    std::string model_dir      = model_path.substr(0, model_path.find_last_of("/\\"));
    std::string tokenizer_path = model_dir + "/tokenizer.json";

    LOG_INFO("{}EnsureInit: atomicCode:{} cfgPath:{} modelPath:{} tokenizerPath:{}", kTag, atomic_code,
             cfg_path, model_path, tokenizer_path);

    auto inst = std::make_shared<cosmo::Qwen3VLUnify>(atomic_code, cfg_path, model_path, tokenizer_path);
    auto ret  = inst->Init();
    if (ret != cosmo::util::ErrorEnum::Success) {
        LOG_WARN("{}EnsureInit: Init failed, error:{}", kTag, static_cast<int>(ret));
        init_failed_ = true;
        return false;
    }

    inst_        = inst;
    atomic_code_ = atomic_code;
    LOG_INFO("{}EnsureInit: Qwen3VL shared instance created. AtomicCode:{}", kTag, atomic_code);
    return true;
#endif  // COSMO_NN_USE_CPU_BACKEND
}

bool LlmInferServiceImpl::IsInitialized() const {
    std::lock_guard<std::mutex> lk(lifecycle_mtx_);
    return inst_ != nullptr;
}

cosmo::util::ErrorEnum LlmInferServiceImpl::Generate(const std::vector<VideoFramePtr>& images,
                                                     const std::vector<std::string>& prompts,
                                                     const cosmo::Qwen3VLGenerationParam& gen_param,
                                                     std::vector<cosmo::Qwen3VLResult>& results) {
    // Lock ordering: infer_mtx_ first, then lifecycle_mtx_.
    // This ensures Reset() (which also locks infer_mtx_ first) cannot destroy the
    // model instance while inference is in progress. By holding infer_mtx_ across
    // the entire call, we no longer need to copy the shared_ptr — the instance
    // is guaranteed to stay alive.
    std::lock_guard<std::mutex> infer_lk(infer_mtx_);
    cosmo::Qwen3VLUnify* raw = nullptr;
    {
        std::lock_guard<std::mutex> lk(lifecycle_mtx_);
        raw = inst_.get();
    }
    if (!raw) {
        LOG_WARN("{}Generate: instance not initialized", kTag);
        return cosmo::util::ErrorEnum::NotInit;
    }
    return raw->Generate(images, prompts, gen_param, results);
}

cosmo::util::ErrorEnum LlmInferServiceImpl::GetMaxBatchSize(size_t& value) const {
    // Same lock ordering as Generate() for consistency.
    std::lock_guard<std::mutex> infer_lk(infer_mtx_);
    cosmo::Qwen3VLUnify* raw = nullptr;
    {
        std::lock_guard<std::mutex> lk(lifecycle_mtx_);
        raw = inst_.get();
    }
    if (!raw) {
        LOG_WARN("{}GetMaxBatchSize: instance not initialized", kTag);
        return cosmo::util::ErrorEnum::NotInit;
    }
    return raw->GetMaxBatchSize(&value);
}

void LlmInferServiceImpl::Reset() {
    // Lock ordering: infer_mtx_ first, then lifecycle_mtx_.
    // Acquiring infer_mtx_ guarantees that any in-flight Generate() call has
    // completed before we destroy the model instance. This prevents the
    // shared_ptr copy race where Generate() held a copy that delayed
    // destruction past the Reset() call.
    std::lock_guard<std::mutex> infer_lk(infer_mtx_);
    std::lock_guard<std::mutex> lk(lifecycle_mtx_);
    if (inst_) {
        LOG_INFO("{}Reset: releasing Qwen3VL shared instance. use_count:{}", kTag, inst_.use_count());
        inst_.reset();
    }
    init_failed_ = false;
    atomic_code_.clear();
    LOG_INFO("{}Reset: done, ready for re-initialization", kTag);
}

void LlmInferServiceImpl::NotifyWorkerStart() {
    int count = active_worker_count_.fetch_add(1) + 1;
    LOG_INFO("{}NotifyWorkerStart: active video workers: {}", kTag, count);
}

void LlmInferServiceImpl::NotifyWorkerStop() {
    int current = active_worker_count_.load();
    while (current > 0 && !active_worker_count_.compare_exchange_weak(current, current - 1)) {
    }
    if (current <= 0) {
        LOG_ERRO("{}NotifyWorkerStop called with no active video workers. count: {}", kTag, current);
        active_worker_count_.store(0);
        return;
    }
    int remaining = current - 1;
    LOG_INFO("{}NotifyWorkerStop: active video workers remaining: {}", kTag, remaining);
    if (remaining <= 0) {
        LOG_INFO("{}All Qwen3VL video workers stopped, releasing shared model to free VRAM", kTag);
        Reset();
    }
}

}  // namespace cosmo::service
