add_library(cosmo_api OBJECT
    ApiRouter.cc                    ApiRouter.h
    ApiRouterInternal.h
    ApiRouterRoutes.cc
    MessageAlgorithmHandler.cc      MessageAlgorithmHandler.h
    MessageAudioHandler.cc          MessageAudioHandler.h
    MessageAuthHandler.cc           MessageAuthHandler.h
    MessageBodyLibHandler.cc        MessageBodyLibHandler.h
    MessageCameraHandler.cc         MessageCameraHandler.h
    MessageEventHandler.cc          MessageEventHandler.h
    MessageFaceLibHandler.cc        MessageFaceLibHandler.h
    MessageFaceLibHandler_ModifyLib.cc
    MessageFaceLibHandler_ModifyPic.cc
    MessageFaceLibHandler_Query.cc
    MessageHandler.cc               MessageHandler.h
    MessageImportFileHandler.cc     MessageImportFileHandler.h
    MessageLinkageHandler.cc        MessageLinkageHandler.h
    MessageLiveStreamHandler.cc     MessageLiveStreamHandler.h
    MessageModelHandler.cc          MessageModelHandler.h
    MessageOnboardingHandler.cc     MessageOnboardingHandler.h
    MessageNetworkHandler.cc        MessageNetworkHandler.h
    MessageScheduleHandler.cc       MessageScheduleHandler.h
    MessageSystemHandler.cc         MessageSystemHandler.h
    MessageSystemHandlerOps.cc
    MessageThingsLibHandler.cc      MessageThingsLibHandler.h
    MessageVideoTaskHandler.cc      MessageVideoTaskHandler.h
)

# Project headers
target_include_directories(cosmo_api PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cosmo_api PRIVATE cosmo_common)
