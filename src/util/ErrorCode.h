#pragma once
#include <cstdint>
#include <system_error>
#include <type_traits>

namespace cosmo::util {

// Segmented error codes. E.g. generic error codes are 0x0-0x10000. Other types use an offset, e.g.
// 0x20000-0x30000
enum class ErrorEnum : uint32_t {
    Success = 0,        // Success
    NotImplement,       // Interface not implemented
    Created,            // Target has already been created
    NotCreated,         // Task not created
    NotInit,            // Called before function is started/initialized
    Failed,             // General error
    TimeSyncFailed,     // Time synchronization failed
    TaskCreateFailed,   // Task creation failed
    ResourceLimit,      // Insufficient resources
    CreateToManyTimes,  // Task recreation failed
    SysErr,  // System error, usually indicates a system function call failed with unknown reason or multiple
             // system function calls failed
    NoMem,   // Not enough memory
    InValidMem,               // Invalid memory
    InvalidParam,             // Invalid parameter
    MandatoryParamMiss,       // Mandatory parameter missing
    FileOpenFailed,           // File opening failed
    FileNotOpened,            // Reading before file is opened
    FileNotExist,             // File does not exist
    FileNotSupport,           // File not supported
    FileSizeSmall,            // File size too small
    FileSizeBig,              // File size too large
    FileAnalysisFailed,       // File analysis failed
    FileDecryptFailed,        // File decryption failed
    LicenseDeviceNotMatch,    // Authorization file does not match
    ParameterException,       // Parameter exception
    ParameterLenError,        // Invalid parameter length
    AlgProcessException,      // Process not supported
    InvalidImage,             // Invalid or empty input image
    FlowDataNull,             // Action flow data is null
    FlowDataInvalid,          // Flow data is abnormal
    FrameDataInvalid,         // Frame data is abnormal
    FlowDataInputInvalid,     // Action flow input data is abnormal
    ServiceNotInit,           // Service not initialized
    DependServiceNotInit,     // Dependent service not initialized
    DependLibEmpty,           // Dependent face/work clothes library is empty
    PersonLibNotEmpty,        // Work clothes library deletion failed, library is bound to work clothes photos
    NotSupport,               // System support issue
    FileMoveFailed,           // File move failed
    UpgradeFileVerifyFailed,  // Installation package verification error
    UpgradeFileChipType,      // Installation package chip type error
    UpgradeFileNotMatch,      // Installation package information does not match
    UpgradeFileVersion,       // Installation package version does not match
    UpgradeHaveTasks,         // Upgrade tasks exist
    UpLoadDataEmpty,          // Uploaded data is empty
    FileTypeParamEmpty,       // Uploaded file type is empty
    IpProbeFailed,            // IP probe error
    MaxCountLimit,            // Maximum count limit
    NoSuchId,                 // ID not found
    ExistedId,                // Duplicate ID
    ExistedName,              // Duplicate name
    FaceLibNotEmpty,          // Face library not empty
    FaceLibCountOverFlow,     // Face library count reached the limit
    DebugQuit,                // DEBUG QUIT - testing exit
    InterfaceNotSupport,      // Interface not supported

    ArticlesReidLibNotEmpty,  // Article library deletion failed, library is bound to article photos
    PersonLibOverflow,        // Work clothes library single-library photo limit reached
    PersonInAllLibOverflow,   // Total work clothes photos across all libraries reached the limit
    PersonPicTargetOverflow,  // Work clothes count in a single photo exceeds 1
    PersonElementNotEmpty,    // Human element cannot be empty
    PersonNotDetected,        // No human work clothes detected in the photo
    PersonPicQualityIllegal,  // Work clothes photo quality is substandard
    PersonPicSizeIllegal,     // Work clothes photo resolution is substandard
    DecodeFailed,             // Decoding failed
    EncodeFailed,             // Encoding channels exceeded the limit

    UnknownOperation,  // Unknown operation

    AuthFailed = 0x2715,     // Authentication failed (10005) Same as algorithm repository
    LoginFailed,             // Login failed
    NotLogin,                // Not logged in
    OldPasswdWrong,          // Old password incorrect
    LoginFrequence,          // Login too frequent
    UnZipFileFailed,         // Failed to unzip file
    ModelFileName,           // Incorrect model file name
    ModelFileLack,           // Model file missing
    ModelFileAnalysis,       // Error analyzing model file
    ModelFileNotMatch,       // Model file mismatch
    ModelFilePlatform,       // Model file does not adapt to this platform
    IDNotExist,              // Configuration does not exist
    IDExist,                 // Configuration already exists, duplicate addition not allowed
    TimeTemplateNotExist,    // Time template does not exist
    TimeTemplateInUse,       // Time template is in use and cannot be deleted
    TimeTemplateCountLimit,  // Time template count exceeds limit
    DefaultCantBeDelete,     // Default cannot be deleted
    DefaultCantBeUpdate,     // Default cannot be modified
    CameraNotExist,          // Camera does not exist
    CameraNotOnline,         // Camera is not online
    CameraCountLimit,        // Camera count reached the limit
    TaskNotExist,            // Task does not exist
    AlgorithmNotExist,       // Algorithm does not exist
    AlgorithmInUse,          // Algorithm is in use and cannot be deleted
    TaskTooMuch,             // Too many tasks under the channel
    TooMuchAreas,            // Too many areas
    LackArea,                // Missing area
    DatabaseFailed,          // Database operation failed
    MvDebugModel,            // DEBUG

    ActionReady = 0x3000,  // Action flow startup phase: instance initialization
    ActionStart,           // Action flow startup phase: action start
    ActionStop,            // Action flow startup phase: action stop

    VideoResolutionNotSupport,  // Video resolution not supported
    VideoFormatNotSupport,      // Video format not supported, e.g. non-H.264/H.265 video stream

    DemuxOpenInvalidUrl,          // Stream fetching open failed - avformat_open_input
    DemuxOpenStreamFail,          // Stream fetching open failed - avformat_open_input
    DemuxOpenStreamUnauthorized,  // Stream fetching validation failed - avformat_open_input
    DemuxFindStreamFail,          // Stream fetching stream finding failed - avformat_find_stream_info
    DemuxFindVideoStreamFail,     // Stream fetching stream finding failed - av_find_best_stream
    DemuxInitBsfcFail,            // Stream fetching stream finding failed - av_bsf_init
    DemuxGetStreamFail,           // Stream fetching open failed
    DemuxStreamStart,             // Stream fetching start
    DemuxStreamClosed,            // Stream fetching end
    DemuxReadStreamFail,          // Stream fetching open failed
    DemuxNoData,                  // Stream fetching no data

    DecoderColorConvertFailed,   // Image conversion failed
    DecoderResizePaddingFailed,  // Image padding failed
    DecoderFrameFailed,          // Frame decoding failed

    ImageDecodeFailed,             // Image decoding failed
    ImageDownloadFailed,           // Image downloading failed
    ImageResolutionInvalid,        // Image resolution invalid
    ImageContentSizeInvalid,       // Image content size invalid
    ImageContentDecryptionFailed,  // Image content decryption failed
    ImageChannelCountInvalid,      // Image channel count invalid
    ImageReadFailed,               // Image read failed
    ImageCatchFailed,              // Channel image capture failed

    ImageEncodeFailed,  // Image encoding failed

    FaceIsNotInTheMiddle = 0x4000,  // Face is not in the middle
    FaceIsTooSmall,                 // Face is incomplete
    FaceIsIncomplete,               // Face is incomplete
    QualityNotEnough,               // Quality is not enough
    GetFeatureFailed,               // Feature value extraction failed
    InternalError,                  // Internal error
    NoFaceDetected,                 // No face detected

    ArticlesReidLibCountOverFlow = 0x5000,  // Article library count reached the limit
    ArticlesReidLibOverflow,                // Article library single-library photo limit reached
    ArticlesReidPicSizeIllegal,             // Article photo resolution is substandard
    ArticlesReidInAllLibOverflow,           // Total article photos across all libraries reached the limit
    ArticlesReidElementNotEmpty,            // Article element cannot be empty

    StrategyNotExist = 0x6000,  // Peripheral orchestration strategy does not exist

    NoAuthService = 0x9980E1,       // 0x9980E1 Service not authorized
    AuthExpiredService,             // Service authorization expired
    AuthServiceCountLimit,          // Insufficient authorization count
    AuthServiceFileAnalysisFailed,  // Authorization file analysis failed

    AI_FAILED = 0x1000000,   // AI error code
    AI_INST_CREATED,         // AI instance has been created
    AI_INST_CREATEFAILED,    // AI instance creation failed
    AI_INST_NOTCREATED,      // AI instance not created
    AI_INIT_PARAMERR,        // AI initialization parameter error
    AI_DETECT_FAILED,        // AI detection error
    AI_CLASSIFY_FAILED,      // AI classification error
    AI_LANDMARK_FAILED,      // AI landmark error
    AI_FEATURE_FAILED,       // AI feature extraction error
    AI_TRACK_FAILED,         // AI tracking error
    AI_TRACK_PARAM_INVALID,  // AI tracking parameter error
    AI_FORWARD_FAILED,       // AI inference error
    AI_PARSE_OUTPUT_FAILED,  // AI result parsing error

    // Error codes not yet public
    OperationNotSupport = 0x1312D01,  // Operation not supported (Original box 20000001)
    ExistedPersonSerialNumber,        // Duplicate personnel serial number
    PolygonIntersection,              // Polygon intersection
    VcpCheckFailed,                   // Cloud upgrade version check failed
    AuthApiCallFailed,                // Authorization platform interface call failed
    AuthSignVerifyFailed,             // Signature verification failed
    AuthQueryFailed,                  // Authorization info query failed
    AuthAlgorithmFailed,              // Algorithm authorization failed
    AuthServiceFailed,                // Service authorization failed
    AuthPlatFailed,  // Authorization failed (wrong authorization code or insufficient balance, etc.)
    AuthEmpty,       // Authorization service is empty

    ActionAlgNotExist       = 0x2000000,  // Orchestration algorithm does not exist
    ActionAlgDownLoadFailed = 0x2000001,  // Orchestration algorithm download failed
    ActionAlgGetAtomicCodeList,           // Orchestration algorithm failed to get atomic algorithm list
    ActionAlgCreateFailed,                // Orchestration algorithm creation failed
    ActionAlgLoadFailed,                  // Orchestration algorithm loading failed
    ActionFailed,                         // Orchestration failed
    ActionAlgArrangeConfigFail,           // Algorithm configuration reading failed
};

std::error_condition make_error_condition(ErrorEnum code);

}  // namespace cosmo::util

namespace std {
template <>
struct is_error_condition_enum<cosmo::util::ErrorEnum> : true_type {};

}  // namespace std
