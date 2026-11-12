#include "service/event/AlarmAttachmentCleaner.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <nlohmann/json.hpp>
#include <set>
#include <utility>

#include "util/PathUtil.h"

namespace cosmo::service {
namespace {

    class Fd {
    public:
        explicit Fd(int value = -1) : value_(value) {}
        ~Fd() {
            if (value_ >= 0)
                close(value_);
        }
        Fd(Fd&& other) noexcept : value_(std::exchange(other.value_, -1)) {}
        Fd& operator=(Fd&& other) noexcept {
            if (this != &other) {
                if (value_ >= 0)
                    close(value_);
                value_ = std::exchange(other.value_, -1);
            }
            return *this;
        }
        Fd(const Fd&)            = delete;
        Fd& operator=(const Fd&) = delete;
        [[nodiscard]] int Get() const {
            return value_;
        }

    private:
        int value_;
    };

    using Date = std::array<std::string, 3>;

    bool DatePart(const std::string& value, int level) {
        const size_t width = level == 0 ? 4 : 2;
        if (value.size() != width)
            return false;
        for (unsigned char ch : value)
            if (!std::isdigit(ch))
                return false;
        if (level == 0)
            return true;
        const int number = (value[0] - '0') * 10 + (value[1] - '0');
        return number >= 1 && number <= (level == 1 ? 12 : 31);
    }

    AlarmAttachmentStatus ScanDates(int parent, int level, Date& date, std::vector<Date>& dates) {
        const int duplicate = dup(parent);
        if (duplicate < 0)
            return AlarmAttachmentStatus::AccessFailed;
        DIR* raw = fdopendir(duplicate);
        if (!raw) {
            close(duplicate);
            return AlarmAttachmentStatus::AccessFailed;
        }
        std::unique_ptr<DIR, decltype(&closedir)> dir(raw, &closedir);
        while (true) {
            errno         = 0;
            dirent* entry = readdir(dir.get());
            if (!entry)
                return errno == 0 ? AlarmAttachmentStatus::Ok : AlarmAttachmentStatus::AccessFailed;
            const std::string name(entry->d_name);
            if (!DatePart(name, level))
                continue;
            struct stat info {};
            if (fstatat(parent, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
                if (errno == ENOENT)
                    continue;
                return AlarmAttachmentStatus::AccessFailed;
            }
            if (S_ISLNK(info.st_mode))
                return AlarmAttachmentStatus::AccessFailed;
            if (!S_ISDIR(info.st_mode))
                continue;
            Fd child(openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            if (child.Get() < 0) {
                if (errno == ENOENT)
                    continue;
                return AlarmAttachmentStatus::AccessFailed;
            }
            date[static_cast<size_t>(level)] = name;
            if (level == 2) {
                dates.push_back(date);
            } else {
                const auto status = ScanDates(child.Get(), level + 1, date, dates);
                if (status != AlarmAttachmentStatus::Ok)
                    return status;
            }
        }
    }

    AlarmAttachmentStatus OpenRoot(const std::string& path, Fd& root, std::vector<Date>& dates) {
        root = Fd(open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        if (root.Get() < 0)
            return errno == ENOENT ? AlarmAttachmentStatus::Ok : AlarmAttachmentStatus::AccessFailed;
        Date date;
        return ScanDates(root.Get(), 0, date, dates);
    }

    // A missing partition is harmless when another cleanup removes it concurrently.
    AlarmAttachmentStatus OpenDate(int root, const Date& date, Fd& day, bool& missing) {
        missing = root < 0;
        if (missing)
            return AlarmAttachmentStatus::Ok;
        Fd year(openat(root, date[0].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (year.Get() < 0) {
            missing = errno == ENOENT;
            return missing ? AlarmAttachmentStatus::Ok : AlarmAttachmentStatus::AccessFailed;
        }
        Fd month(openat(year.Get(), date[1].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (month.Get() < 0) {
            missing = errno == ENOENT;
            return missing ? AlarmAttachmentStatus::Ok : AlarmAttachmentStatus::AccessFailed;
        }
        day = Fd(openat(month.Get(), date[2].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (day.Get() < 0) {
            missing = errno == ENOENT;
            return missing ? AlarmAttachmentStatus::Ok : AlarmAttachmentStatus::AccessFailed;
        }
        return AlarmAttachmentStatus::Ok;
    }

    AlarmAttachmentStatus CollectInRoot(int root, const std::vector<Date>& dates, bool featureRoot,
                                        const std::set<std::string>& names,
                                        std::vector<AlarmAttachment>& attachments) {
        for (const auto& date : dates) {
            Fd day;
            bool missing      = false;
            const auto status = OpenDate(root, date, day, missing);
            if (status != AlarmAttachmentStatus::Ok)
                return status;
            if (missing)
                continue;
            for (const auto& name : names) {
                struct stat info {};
                if (fstatat(day.Get(), name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
                    if (errno == ENOENT)
                        continue;
                    return AlarmAttachmentStatus::AccessFailed;
                }
                if (!S_ISREG(info.st_mode))
                    return AlarmAttachmentStatus::Invalid;
                attachments.push_back({featureRoot, date, name});
            }
        }
        return AlarmAttachmentStatus::Ok;
    }

}  // namespace

struct AlarmAttachmentCleaner::Impl {
    Fd eventRoot;
    Fd featureRoot;
    std::vector<Date> eventDates;
    std::vector<Date> featureDates;
    AlarmAttachmentStatus status{AlarmAttachmentStatus::Ok};
};

AlarmAttachmentCleaner::AlarmAttachmentCleaner() : impl_(std::make_unique<Impl>()) {
    impl_->status = OpenRoot(path::GetEventRootPath(false), impl_->eventRoot, impl_->eventDates);
    if (impl_->status == AlarmAttachmentStatus::Ok)
        impl_->status = OpenRoot(path::GetRecordRootPath(false), impl_->featureRoot, impl_->featureDates);
}

AlarmAttachmentCleaner::~AlarmAttachmentCleaner() = default;

AlarmAttachmentStatus AlarmAttachmentCleaner::Collect(const std::string& id, const std::string& extraFiles,
                                                      std::vector<AlarmAttachment>& attachments) const {
    attachments.clear();
    if (impl_->status != AlarmAttachmentStatus::Ok)
        return impl_->status;
    if (!path::IsSafePathComponent(id))
        return AlarmAttachmentStatus::Invalid;

    std::set<std::string> eventNames{id + "_video.mp4", id + "_video.tmp", id + ".json",
                                     id + "_overview.json"};
    if (!extraFiles.empty()) {
        const auto files = nlohmann::json::parse(extraFiles, nullptr, false);
        if (!files.is_array())
            return AlarmAttachmentStatus::Invalid;
        for (const auto& item : files) {
            if (!item.is_string())
                return AlarmAttachmentStatus::Invalid;
            const auto suffix = item.get<std::string>();
            if (suffix.empty() || suffix.front() != '_' || !path::IsSafePathComponent(suffix, 255) ||
                !path::IsSafePathComponent(id + suffix, 255))
                return AlarmAttachmentStatus::Invalid;
            eventNames.insert(id + suffix);
        }
    }
    const auto eventStatus =
        CollectInRoot(impl_->eventRoot.Get(), impl_->eventDates, false, eventNames, attachments);
    if (eventStatus != AlarmAttachmentStatus::Ok)
        return eventStatus;
    return CollectInRoot(impl_->featureRoot.Get(), impl_->featureDates, true, {id + ".feature"}, attachments);
}

AlarmAttachmentStatus AlarmAttachmentCleaner::Remove(const std::vector<AlarmAttachment>& attachments) const {
    for (const auto& item : attachments) {
        const auto root = item.featureRoot ? impl_->featureRoot.Get() : impl_->eventRoot.Get();
        Fd day;
        bool missing      = false;
        const auto status = OpenDate(root, item.date, day, missing);
        if (status != AlarmAttachmentStatus::Ok)
            return status;
        if (missing)
            continue;
        struct stat info {};
        if (fstatat(day.Get(), item.name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno == ENOENT)
                continue;
            return AlarmAttachmentStatus::AccessFailed;
        }
        if (!S_ISREG(info.st_mode))
            return AlarmAttachmentStatus::Invalid;
        if (unlinkat(day.Get(), item.name.c_str(), 0) != 0 && errno != ENOENT)
            return AlarmAttachmentStatus::DeleteFailed;
        if (fstatat(day.Get(), item.name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
            return AlarmAttachmentStatus::DeleteFailed;
    }
    return AlarmAttachmentStatus::Ok;
}

}  // namespace cosmo::service
