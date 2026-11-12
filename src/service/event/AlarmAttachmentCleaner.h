#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace cosmo::service {

enum class AlarmAttachmentStatus { Ok, AccessFailed, DeleteFailed, Invalid };

struct AlarmAttachment {
    bool featureRoot{false};
    std::array<std::string, 3> date;
    std::string name;
};

// One instance inventories the date directories once for a deletion batch.
class AlarmAttachmentCleaner {
public:
    AlarmAttachmentCleaner();
    ~AlarmAttachmentCleaner();
    AlarmAttachmentCleaner(const AlarmAttachmentCleaner&)            = delete;
    AlarmAttachmentCleaner& operator=(const AlarmAttachmentCleaner&) = delete;

    [[nodiscard]] AlarmAttachmentStatus Collect(const std::string& id, const std::string& extraFiles,
                                                std::vector<AlarmAttachment>& attachments) const;
    [[nodiscard]] AlarmAttachmentStatus Remove(const std::vector<AlarmAttachment>& attachments) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cosmo::service
