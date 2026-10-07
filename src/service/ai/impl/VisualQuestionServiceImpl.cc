#include "service/ai/impl/VisualQuestionServiceImpl.h"

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
            binding.at("manifest_sha256") != manifestSha256)
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
    if (options_.Valid())
        worker_ = std::thread(&VisualQuestionServiceImpl::Work, this);
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
        job->promise.set_value(Failed(reason));
        return future;
    };
    if (!options_.Valid())
        return reject("compiler_not_configured");
    if (!run || !run->Active())
        return reject("stale_task_run");
    if (questions.empty() || questions.size() > 32 || timeout.count() < 1 || timeout.count() > 120000)
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
    if (job->input.size() > visual::kMaxJson)
        return reject("compile_request_too_large");
    job->inputSha256 = visual::Sha256(reinterpret_cast<const uint8_t*>(job->input.data()), job->input.size());
    job->questions   = std::move(questions);
    job->run         = std::move(run);
    job->deadline    = Clock::now() + timeout;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_)
            return reject("service_stopped");
        if (outstanding_ >= 3)
            return reject("compiler_queue_full");
        queue_.push_back(job);
        ++outstanding_;
    }
    ready_.notify_one();
    return future;
}

VisualQuestionPreparation VisualQuestionServiceImpl::Compile(const Job& job) {
    const std::vector<std::string> argv{
        options_.python,     options_.script,         "--manifest", options_.manifestPath,
        "--manifest-sha256", options_.manifestSha256, "--cache",    options_.cacheDirectory};
    auto process = executor_(argv, job.input, job.deadline, [&] { return stopped_ || !job.run->Active(); });
    if (!process.failure.empty())
        return Failed(process.failure);
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

void VisualQuestionServiceImpl::Work() {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [&] { return stopped_ || !queue_.empty(); });
            if (queue_.empty())
                return;
            job = queue_.front();
            queue_.pop_front();
        }
        auto result = Failed("question_compile_failed");
        try {
            if (Clock::now() < job->deadline && job->run->Active() && !stopped_)
                result = Compile(*job);
        } catch (...) {
            result = Failed("invalid_compiler_receipt");
        }
        if (Clock::now() >= job->deadline)
            result = Failed("compile_deadline_exceeded");
        if (!job->run->Active())
            result = Failed("stale_task_run");
        if (stopped_)
            result = Failed("service_stopped");
        job->promise.set_value(std::move(result));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --outstanding_;
        }
    }
}

void VisualQuestionServiceImpl::Stop() {
    std::call_once(stopOnce_, [&] {
        stopped_ = true;
        ready_.notify_all();
        if (worker_.joinable())
            worker_.join();
    });
}
}  // namespace cosmo::service
