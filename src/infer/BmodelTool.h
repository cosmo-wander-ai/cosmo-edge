// BmodelTool — Utility for retrieving bmodel info and converting to nn format.

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace cosmo {

// Input/output node info
struct BmodelNodeInfo {
    std::string name;        // Node name
    std::vector<int> shape;  // Shape
    int data_type{0};        // Data type (0=float32, 1=float16, etc.)
};

// Single network info
struct BmodelNetworkInfo {
    std::string name;  // Network name
    int max_batch{1};  // Max batch size
    std::vector<BmodelNodeInfo> inputs;
    std::vector<BmodelNodeInfo> outputs;
};

// Complete bmodel file info
struct BmodelInfo {
    std::string file_path;
    std::vector<BmodelNetworkInfo> networks;
    bool valid{false};
    std::string error_msg;
};

class BmodelTool {
public:
    BmodelTool()  = default;
    ~BmodelTool() = default;

    // Get bmodel file info.
    static BmodelInfo GetBmodelInfo(const std::string& bmodelPath);

    // Convert single or multiple bmodel files to nn format.
    // Returns empty string on success; error message on failure.
    static std::string ConvertToNn(const std::vector<std::string>& bmodelPaths,
                                   const std::string& outputPath);

    // Clean up temporary files.
    static void CleanupTempFiles(const std::vector<std::string>& filePaths);

    // Log model info for debugging.
    static void LogBmodelInfo(const BmodelInfo& info, const std::string& logPrefix = "[BmodelTool]");

private:
    static int ConvertDataType(int bmDataType);
};

}  // namespace cosmo
