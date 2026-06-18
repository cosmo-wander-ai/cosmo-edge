// Audio DTO definitions (extracted from MessageAudioHandler.h)

#pragma once

#include <system_error>

#include "service/media/dto/DetectMsgTypes.h"
#include "util/dto/EventMsgTypes.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo {

namespace Audio {
    // Audio file query
    struct MsgQueryAudioFileRecv : public MsgRecvHead {
        int pageNum{1};
        util::RangeInt<-1, 1000> pageSize{10};
        std::string fileName;
        std::vector<std::string> keepFileTypes;
    };

    void to_json(nlohmann::json& j, const MsgQueryAudioFileRecv& v);
    void from_json(const nlohmann::json& j, MsgQueryAudioFileRecv& v);

    // Audio file query response
    struct MsgQueryAudioFileSend : public MsgSendHead {
        struct AudioFile {
            std::string fileId;    // (Display name)
            std::string fileName;  // (File name) UUID generated on save
            std::string filePath;  // (File path) HTTP playback path
            int64_t timestamp{0};  // Timestamp (ms)
            friend void to_json(nlohmann::json& j, const AudioFile& v);
            friend void from_json(const nlohmann::json& j, AudioFile& v);
        };

        struct ResData {
            int totalCount{0};
            std::vector<AudioFile> audioFileList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgQueryAudioFileSend& v);
    void from_json(const nlohmann::json& j, MsgQueryAudioFileSend& v);

    // Audio file delete
    struct MsgDeleteAudioFileRecv : public MsgRecvHead {
        std::vector<std::string> fileIdList;
    };

    void to_json(nlohmann::json& j, const MsgDeleteAudioFileRecv& v);
    void from_json(const nlohmann::json& j, MsgDeleteAudioFileRecv& v);

    // Audio file delete response
    struct MsgDeleteAudioFileSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgResultInfo> failedList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgDeleteAudioFileSend& v);
    void from_json(const nlohmann::json& j, MsgDeleteAudioFileSend& v);
    // Basic camera info
    struct MsgBaseAudioDeviceInfo {
        std::string devId;
        std::string name;
        std::string ip;
        std::string ethName;
        friend void to_json(nlohmann::json& j, const MsgBaseAudioDeviceInfo& v);
        friend void from_json(const nlohmann::json& j, MsgBaseAudioDeviceInfo& v);
    };
    // Audio device modify
    struct MsgModifyAudioDeviceRecv : public MsgRecvHead {
        util::RangeInt<1, 2> devOperation{0};
        MsgBaseAudioDeviceInfo audioDev;
    };

    void to_json(nlohmann::json& j, const MsgModifyAudioDeviceRecv& v);
    void from_json(const nlohmann::json& j, MsgModifyAudioDeviceRecv& v);

    // Audio device modify response
    struct MsgModifyAudioDeviceSend : public MsgSendHead {
        struct ResData {
            std::string devId;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgModifyAudioDeviceSend& v);
    void from_json(const nlohmann::json& j, MsgModifyAudioDeviceSend& v);
    // Audio device query
    struct MsgQueryAudioDeviceRecv : public MsgRecvHead {
        int pageNum{1};
        util::RangeInt<-1, 1000> pageSize{10};
        std::string name;
        bool checkAlive{true};
    };

    void to_json(nlohmann::json& j, const MsgQueryAudioDeviceRecv& v);
    void from_json(const nlohmann::json& j, MsgQueryAudioDeviceRecv& v);

    // Audio file query response
    struct MsgQueryAudioDeviceSend : public MsgSendHead {
        struct AudioDevice {
            std::string devId;
            std::string name;
            std::string ip;
            int64_t timestamp{0};  // Timestamp (ms)
            bool online{false};
            std::string ethName;
            friend void to_json(nlohmann::json& j, const AudioDevice& v);
            friend void from_json(const nlohmann::json& j, AudioDevice& v);
        };

        struct ResData {
            int totalCount{0};
            std::vector<AudioDevice> audioDevList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgQueryAudioDeviceSend& v);
    void from_json(const nlohmann::json& j, MsgQueryAudioDeviceSend& v);

    // Audio device delete
    struct MsgDeleteAudioDeviceRecv : public MsgRecvHead {
        std::vector<std::string> devIdList;
    };

    void to_json(nlohmann::json& j, const MsgDeleteAudioDeviceRecv& v);
    void from_json(const nlohmann::json& j, MsgDeleteAudioDeviceRecv& v);

    // Audio device test response
    struct MsgTestAudioDeviceSend : public MsgSendHead {};

    // Audio device test
    struct MsgTestAudioDeviceRecv : public MsgRecvHead {
        util::RangeInt<1, 2> operation{0};      // 1: File test, 2: Text test
        std::string devSn;                      // Audio device ID
        std::string data;                       // Text or file ID
        util::RangeInt<0, 100> volume{50};      // Volume
        util::RangeInt<60, 600> duration{600};  // Loop duration (seconds)
        util::RangeInt<1, 100> times{20};       // Loop count
        util::RangeInt<1, 60> gap{2};           // Gap between loops (seconds)
        util::RangeInt<0, 100> speed{50};       // Pronunciation speed
        util::RangeInt<0, 1> tone{0};           // Tone 0: Male, 1: Female
    };

    void to_json(nlohmann::json& j, const MsgTestAudioDeviceRecv& v);
    void from_json(const nlohmann::json& j, MsgTestAudioDeviceRecv& v);

    // Audio device delete response
    struct MsgDeleteAudioDeviceSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgResultInfo> failedList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgDeleteAudioDeviceSend& v);
    void from_json(const nlohmann::json& j, MsgDeleteAudioDeviceSend& v);
}  // namespace Audio
}  // namespace cosmo
