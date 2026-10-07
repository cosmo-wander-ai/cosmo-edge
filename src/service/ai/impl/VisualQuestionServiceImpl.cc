#include "service/ai/impl/VisualQuestionServiceImpl.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

#include "service/ai/impl/VisualDecisionProtocol.h"

namespace cosmo::service {
namespace {
    using visual::Json;
    using Clock            = std::chrono::steady_clock;
    const char* kSources[] = {"compile_questions.py", "question_compiler.py", "image_frontend.py",
                              "visual_protocol.py", "laya_shadow_worker.py"};

    std::string Env(const char* name) {
        const char* value = std::getenv(name);
        return value ? value : "";
    }
    VisualQuestionPreparation Failed(const std::string& reason) {
        VisualQuestionPreparation result;
        result.reason = reason;
        return result;
    }
    void Require(bool condition) {
        if (!condition)
            throw std::runtime_error("invalid_compiler_receipt");
    }
}  // namespace

bool VisualQuestionCompilerOptions::Valid() const {
    try {
        if (!std::filesystem::path(python).is_absolute() || !std::filesystem::path(script).is_absolute() ||
            !std::filesystem::path(manifestPath).is_absolute() ||
            !std::filesystem::path(cacheDirectory).is_absolute() || !visual::Sha256Identity(manifestSha256) ||
            binding.at("manifest_sha256") != manifestSha256 || maxConfigurations == 0 ||
            maxConfigurations > 256 || maxInputBytes == 0 || maxInputBytes > 64 * 1024 * 1024 ||
            maxAttemptsPerBatch == 0 || maxAttemptsPerBatch > 8 || retryDelay.count() < 0 ||
            retryDelay.count() > 10000)
            return false;
        for (const auto* name : {"tokenizer_sha256", "model_config_sha256"})
            if (!visual::Sha256Identity(binding.at("assets").at(name).get<std::string>()))
                return false;
        for (const auto* name : kSources)
            if (!visual::Sha256Identity(binding.at("implementation").at(name).get<std::string>()))
                return false;
        return true;
    } catch (...) {
        return false;
    }
}

VisualQuestionCompilerOptions VisualQuestionCompilerOptions::FromEnvironment() {
    VisualQuestionCompilerOptions options;
    try {
        options.manifestPath   = Env("COSMO_VISUAL_MANIFEST");
        options.manifestSha256 = Env("COSMO_VISUAL_MANIFEST_SHA256");
        visual::ReadRelease(options.manifestPath, options.manifestSha256);
        std::ifstream stream(options.manifestPath, std::ios::binary);
        std::string encoded(visual::kMaxJson + 1, '\0');
        stream.read(encoded.data(), encoded.size());
        encoded.resize(static_cast<size_t>(stream.gcount()));
        if (visual::Sha256(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size()) !=
            options.manifestSha256)
            return {};
        const auto manifest = visual::Parse(encoded);
        const auto base     = std::filesystem::path(options.manifestPath).parent_path();
        options.script =
            (base / manifest.at("implementation").at("compile_questions.py").at("path").get<std::string>())
                .lexically_normal()
                .string();
        options.binding = {{"manifest_sha256", options.manifestSha256},
                           {"assets",
                            {{"tokenizer_sha256", manifest.at("tokenizer").at("sha256")},
                             {"model_config_sha256", manifest.at("cpu_files").at("rl_agent_config.json")}}},
                           {"implementation", Json::object()}};
        for (const auto* name : kSources)
            options.binding["implementation"][name] = manifest.at("implementation").at(name).at("sha256");
        const auto python = Env("COSMO_VISUAL_PYTHON");
        const auto cache  = Env("COSMO_VISUAL_QUESTION_CACHE");
        if (!python.empty())
            options.python = python;
        if (!cache.empty())
            options.cacheDirectory = cache;
        return options;
    } catch (...) {
        return {};
    }
}

VisualQuestionServiceImpl::VisualQuestionServiceImpl(VisualQuestionCompilerOptions options, Executor executor)
    : options_(std::move(options)),
      executor_(executor ? std::move(executor)
                         : [](const auto& argv, const auto& input, auto deadline, const auto& cancelled) {
                               return util::RunBoundedProcess(argv, input, deadline, cancelled);
                           }) {
    if (options_.Valid()) {
        worker_ = std::thread(&VisualQuestionServiceImpl::Work, this);
        try {
            expiry_ = std::thread(&VisualQuestionServiceImpl::Expire, this);
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopped_ = true;
            }
            ready_.notify_all();
            worker_.join();
            throw;
        }
    }
}

VisualQuestionServiceImpl::~VisualQuestionServiceImpl() {
    Stop();
}

std::shared_future<VisualQuestionPreparation> VisualQuestionServiceImpl::Prepare(
    std::vector<VisualQuestionSpec> questions, std::shared_ptr<VisualDecisionRun> run,
    std::chrono::milliseconds timeout) {
    auto job    = std::make_shared<Job>();
    auto future = job->promise.get_future().share();
    auto reject = [&](const std::string& reason) {
        ++rejected_;
        job->promise.set_value(Failed(reason));
        return future;
    };
    if (!options_.Valid())
        return reject("compiler_not_configured");
    if (!run || !run->Active())
        return reject("stale_task_run");
    if (questions.empty() || questions.size() > 512 || timeout.count() < 1 || timeout.count() > 600000)
        return reject("invalid_compile_request");
    Json request = {{"questions", Json::array()}};
    std::set<std::string> ids;
    try {
        for (const auto& spec : questions) {
            if (!visual::Identity(spec.itemId) || !ids.insert(spec.itemId).second ||
                !spec.question.is_object())
                return reject("invalid_compile_request");
            // An object has already lost its original option insertion order in
            // engine JSON. Require the explicit representation rather than guess.
            if (spec.question.value("type", std::string()) == "choice" &&
                !spec.question.at("criteria").is_array())
                return reject("choice_requires_ordered_criteria");
            request["questions"].push_back({{"question", spec.question}, {"text_state", spec.textState}});
        }
        job->input = request.dump();
    } catch (...) {
        return reject("invalid_compile_request");
    }
    if (job->input.size() > 16 * visual::kMaxJson)
        return reject("compile_request_too_large");
    job->inputSha256 = visual::Sha256(reinterpret_cast<const uint8_t*>(job->input.data()), job->input.size());
    job->questions   = std::move(questions);
    job->run         = std::move(run);
    job->deadline    = Clock::now() + timeout;
    job->eligible    = Clock::now();
    job->combined.manifestSha256 = options_.manifestSha256;
    job->combined.inputSha256    = job->inputSha256;
    job->combined.cacheHit       = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_)
            return reject("service_stopped");
        SweepLocked();
        if (outstanding_ >= options_.maxConfigurations)
            return reject("compiler_queue_full");
        if (job->input.size() > options_.maxInputBytes - inputBytes_)
            return reject("compiler_queue_bytes_full");
        queue_.push_back(job);
        ++outstanding_;
        ++accepted_;
        inputBytes_ += job->input.size();
    }
    ready_.notify_all();
    return future;
}

std::unique_ptr<VisualQuestionServiceImpl::Job> VisualQuestionServiceImpl::Batch(const Job& job) const {
    // One physical compiler allocation at a time, at most 32 questions/64 KiB.
    // Requeue the configuration after each batch so a large class catalog does
    // not monopolize startup. All batches and retries share the original deadline.
    auto part      = std::make_unique<Job>();
    part->run      = job.run;
    part->deadline = job.deadline;
    Json request   = {{"questions", Json::array()}};
    size_t offset  = job.offset.load();
    while (offset < job.questions.size() && part->questions.size() < 32) {
        const auto& spec = job.questions[offset];
        request["questions"].push_back({{"question", spec.question}, {"text_state", spec.textState}});
        auto encoded = request.dump();
        if (encoded.size() > visual::kMaxJson) {
            if (part->questions.empty())
                return {};
            break;
        }
        part->questions.push_back(spec);
        part->input = std::move(encoded);
        ++offset;
    }
    part->inputSha256 =
        visual::Sha256(reinterpret_cast<const uint8_t*>(part->input.data()), part->input.size());
    return part;
}

VisualQuestionPreparation VisualQuestionServiceImpl::Compile(const Job& job, bool& retryable) {
    const std::vector<std::string> argv{
        options_.python,     options_.script,         "--manifest", options_.manifestPath,
        "--manifest-sha256", options_.manifestSha256, "--cache",    options_.cacheDirectory};
    auto process = executor_(argv, job.input, job.deadline, [&] { return stopped_ || !job.run->Active(); });
    if (!process.failure.empty()) {
        retryable = process.failure == "process_spawn_failed" || process.failure == "process_io_failed";
        return Failed(process.failure);
    }
    auto receipt = visual::Parse(process.output);
    if (process.exitCode != 0 || receipt.value("status", std::string()) != "prepared") {
        auto reason = receipt.value("reason", std::string("question_compile_failed"));
        if (!visual::Identity(reason))
            reason = "question_compile_failed";
        return Failed(reason);
    }
    Require(receipt.at("business_qualified").is_boolean() && !receipt.at("business_qualified").get<bool>() &&
            receipt.at("input_sha256") == job.inputSha256 && receipt.at("binding") == options_.binding &&
            receipt.at("cache_hit").is_boolean() && receipt.at("questions").is_array() &&
            receipt.at("questions").size() == job.questions.size());
    VisualQuestionPreparation result;
    result.manifestSha256 = options_.manifestSha256;
    result.inputSha256    = job.inputSha256;
    result.cacheHit       = receipt.at("cache_hit");
    for (size_t i = 0; i < job.questions.size(); ++i) {
        const auto& spec = job.questions[i];
        const auto& ref  = receipt.at("questions")[i];
        Require(ref.at("question_id") == spec.question.at("id") &&
                ref.at("question_version").is_number_integer() && ref.at("qtype").is_number_integer() &&
                ref.at("question_version") == spec.question.at("version") &&
                ref.at("bindings") == options_.binding.at("assets"));
        VisualQuestionRef question{
            spec.itemId,     ref.at("question_id"),     ref.at("question_version"), ref.at("compiled_sha256"),
            ref.at("qtype"), ref.at("ordered_options"), ref.at("temperature")};
        Require(visual::ValidQuestion(question));
        if (spec.question.at("type") == "choice") {
            std::vector<std::string> labels;
            for (const auto& criterion : spec.question.at("criteria"))
                labels.push_back(criterion.at("label"));
            Require(question.qtype == 0 && question.orderedOptions == labels);
        } else
            Require(spec.question.at("type") == "noul" && question.qtype == 2);
        result.questions.push_back(std::move(question));
    }
    result.ready = true;
    return result;
}

void VisualQuestionServiceImpl::SettleLocked(const std::shared_ptr<Job>& job,
                                             VisualQuestionPreparation result) {
    --outstanding_;
    inputBytes_ -= job->input.size();
    if (active_ == job)
        active_.reset();
    if (result.ready)
        ++completed_;
    else
        ++failed_;
    // Release admission before publishing readiness to callers replacing a task.
    job->promise.set_value(std::move(result));
}

void VisualQuestionServiceImpl::SweepLocked() {
    for (auto it = queue_.begin(); it != queue_.end();) {
        const auto job    = *it;
        const auto reason = stopped_                        ? "service_stopped"
                            : !job->run->Active()           ? "stale_task_run"
                            : Clock::now() >= job->deadline ? "compile_deadline_exceeded"
                                                            : "";
        if (*reason) {
            it = queue_.erase(it);
            SettleLocked(job, Failed(reason));
        } else
            ++it;
    }
}

void VisualQuestionServiceImpl::Expire() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        SweepLocked();
        if (stopped_)
            return;
        // Queued deadlines/cancellations must settle while the compiler is busy.
        // This thread never starts a compiler process or touches active results.
        ready_.wait_for(lock, std::chrono::milliseconds(50), [&] { return stopped_.load(); });
    }
}

void VisualQuestionServiceImpl::Work() {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            for (;;) {
                SweepLocked();
                if (stopped_)
                    return;
                const auto now = Clock::now();
                const auto it  = std::find_if(queue_.begin(), queue_.end(),
                                              [&](const auto& queued) { return queued->eligible <= now; });
                if (it != queue_.end()) {
                    job = *it;
                    queue_.erase(it);
                    active_ = job;
                    break;
                }
                if (queue_.empty())
                    ready_.wait(lock);
                else {
                    const auto next = std::min_element(
                        queue_.begin(), queue_.end(),
                        [](const auto& left, const auto& right) { return left->eligible < right->eligible; });
                    const auto wakeAt = (*next)->eligible;
                    ready_.wait_until(lock, wakeAt);
                }
            }
        }
        auto result    = Failed("question_compile_failed");
        bool retryable = false;
        try {
            if (Clock::now() < job->deadline && job->run->Active() && !stopped_) {
                const auto part = Batch(*job);
                if (part) {
                    ++job->attempts;
                    ++job->batchAttempts;
                    result = Compile(*part, retryable);
                } else
                    result = Failed("compile_request_too_large");
            }
        } catch (...) {
            result = Failed("invalid_compiler_receipt");
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (Clock::now() >= job->deadline)
                result = Failed("compile_deadline_exceeded");
            if (!job->run->Active())
                result = Failed("stale_task_run");
            if (stopped_)
                result = Failed("service_stopped");
            if (result.ready) {
                job->combined.cacheHit = job->combined.cacheHit && result.cacheHit;
                job->combined.questions.insert(job->combined.questions.end(), result.questions.begin(),
                                               result.questions.end());
                job->offset += result.questions.size();
                job->batchAttempts = 0;
                if (job->offset == job->questions.size()) {
                    job->combined.ready = true;
                    SettleLocked(job, std::move(job->combined));
                } else {
                    job->eligible = Clock::now();
                    active_.reset();
                    queue_.push_back(job);
                }
            } else if (retryable &&
                       (result.reason == "process_spawn_failed" || result.reason == "process_io_failed") &&
                       job->batchAttempts < options_.maxAttemptsPerBatch) {
                // Never retry invalid receipts, model/sequence errors or expired
                // work. Backoff gives other tasks a turn without extending time.
                job->eligible = Clock::now() + options_.retryDelay * job->batchAttempts;
                ++retried_;
                active_.reset();
                queue_.push_back(job);
            } else
                SettleLocked(job, Failed(result.reason));
        }
        ready_.notify_all();
    }
}

nlohmann::json VisualQuestionServiceImpl::Counters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto state = [](const std::shared_ptr<Job>& job) {
        return Json{
            {"task_id", job->run->taskId},
            {"run_epoch", job->run->runEpoch},
            {"config_revision", job->run->configRevision},
            {"questions", job->questions.size()},
            {"prepared_questions", job->offset.load()},
            {"attempts", job->attempts.load()},
            {"remaining_ms", std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(
                                                      job->deadline - Clock::now())
                                                      .count())}};
    };
    Json queued = Json::array();
    for (const auto& job : queue_)
        queued.push_back(state(job));
    return {{"available", options_.Valid() && !stopped_},
            {"max_configurations", options_.maxConfigurations},
            {"max_input_bytes", options_.maxInputBytes},
            {"input_bytes", inputBytes_},
            {"outstanding", outstanding_},
            {"queued", queue_.size()},
            {"active", active_ ? state(active_) : Json(nullptr)},
            {"pending", queued},
            {"accepted", accepted_},
            {"completed", completed_},
            {"failed", failed_},
            {"retried", retried_},
            {"rejected", rejected_.load()}};
}

void VisualQuestionServiceImpl::Stop() {
    std::call_once(stopOnce_, [&] {
        {
            // Change the wait predicate under the same lock as Work/Expire so
            // an idle worker cannot miss the stop notification before waiting.
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        ready_.notify_all();
        if (worker_.joinable())
            worker_.join();
        if (expiry_.joinable())
            expiry_.join();
    });
}
}  // namespace cosmo::service
