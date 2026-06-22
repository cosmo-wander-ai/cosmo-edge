/// @file IFaceImport.h
/// @brief Person batch import interface.
///        ISP split from IFaceLibService.
///        Consumed by MessageImportFileHandler.
#pragma once

#include <string>
#include <utility>

namespace cosmo::service {

/// Manages batch import of person records from archive files into a face library.
///
/// Supports asynchronous import with progress tracking and failure reporting.
class IFaceImport {
public:
    virtual ~IFaceImport() = default;

    /// Start importing persons from an archive file into a face library.
    /// @param filePath  Path to the import archive (ZIP with images and metadata).
    /// @param faceLibId Target face library identifier.
    virtual void ImportFile(const std::string& filePath, const std::string& faceLibId) = 0;

    /// Get the current import progress.
    /// @return Pair of (completed count, total count).
    virtual std::pair<int, int> GetImportStatus() const = 0;

    /// Check whether the current import operation has finished.
    /// @return true if import is complete (or no import is running).
    virtual bool ImportComplete() const = 0;

    /// Get the total number of records in the current import batch.
    /// @return Total record count.
    virtual int GetImportTotalCount() const = 0;

    /// Get the URL to the import failure report file.
    /// @return Web-accessible URL to the CSV failure report, or empty if none.
    virtual std::string GetImportFailedUrl() const = 0;
};

}  // namespace cosmo::service
