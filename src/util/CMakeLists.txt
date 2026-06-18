add_library(cosmo_util OBJECT
    CipherUtil.cc        CipherUtil.h
    DateTimeFormat.cc    DateTimeFormat.h
    DurationLogger.cc    DurationLogger.h
    DurationStat.cc      DurationStat.h
    ErrorCode.cc         ErrorCode.h
    Exception.cc         Exception.h
    Exec.cc              Exec.h
    FileUtil.cc          FileUtil.h
    FpsCalc.cc           FpsCalc.h
    FpsCtrl.cc           FpsCtrl.h
    GeometricPos.cc      GeometricPos.h
    JsonFileUtil.cc      JsonFileUtil.h
    Log.cc               Log.h
    NToL.cc              NToL.h
    PeriodicTimer.cc     PeriodicTimer.h
    ScoreCalc.cc         ScoreCalc.h
    StringUtil.cc        StringUtil.h
    Thread.cc            Thread.h
    ThreadRegistry.cc    ThreadRegistry.h
    TimeUtil.cc          TimeUtil.h
    Transcode.cc         Transcode.h
    UuidUtil.cc          UuidUtil.h
    PathUtil.cc          PathUtil.h
    VehicleDict.cc       VehicleDict.h
    Version.cc           Version.h
    # Header-only modules
    AiTypes.h
    AsyncQueue.h
    FormatString.h
    GeometricCalculation.h
    InferConstants.h
    JsonStructUtil.h
    Keys.h
    LimitedType.h
    LogicCalc.h
    MsgBaseTypes.h
    MsgDynamicElement.h
    PaginationHelper.h
    SafeParse.h
    TimingConstants.h
    VideoInfo.h
    Point.h
    Rect.h

    MsgBaseTypes.cc
    MsgDynamicElement.cc
    LogicCalc.cc
    dto/FilterTypes.cc
    dto/ServerMsgTypes.cc
    dto/EventMsgTypes.cc
    dto/ClientMsgEvent.cc
    dto/CameraMsgTypes.cc
    dto/TaskMsgTypes.cc
    dto/TaskMsgTypes_TaskCreate.cc
    dto/TaskMsgTypes_PicTask.cc
    dto/OverviewTypes.cc
    dto/AlgorithmPacketDto.cc
    dto/AlgorithmMsgTypes.cc
)

# Project headers
target_include_directories(cosmo_util PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cosmo_util PRIVATE cosmo_common cryptopp uuid)
