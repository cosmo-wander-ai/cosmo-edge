add_library(cosmo_infer OBJECT
    AiClassifierForward.cc
    AiClassifierUnify.cc            AiClassifierUnify.h
    AiCommon.cc                     AiCommon.h
    AiCommonHelper.cc
    AiComponment.cc                 AiComponment.h
    AiDetectInterface.cc            AiDetectInterface.h
    AiDetectorUnify.cc              AiDetectorUnify.h
    AiLandmarkInterface.cc          AiLandmarkInterface.h
    AiLandmarkerUnify.cc            AiLandmarkerUnify.h
    AiOcrWordClassifierUnify.cc     AiOcrWordClassifierUnify.h
    AiRecognizerInterface.cc        AiRecognizerInterface.h
    AiRecognizerUnify.cc            AiRecognizerUnify.h
    AiTrackerUnify.cc               AiTrackerUnify.h
    AiVideoQualityUnify.cc          AiVideoQualityUnify.h
    BmodelTool.cc                   BmodelTool.h
    DinoDetectorUnify.cc            DinoDetectorUnify.h
    InferPoolTypes.h
    Qwen3VLUnify.cc                 Qwen3VLUnify.h
    Sam2SegmenterUnify.cc           Sam2SegmenterUnify.h
)

# Project headers
target_include_directories(cosmo_infer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cosmo_infer PRIVATE cosmo_common)

# Backend configure (libraries and definitions used by AiComponment.cc and BmodelTool.cc)
if(COSMO_NN_USE_SOPHON_BACKEND)
    target_link_libraries(cosmo_infer PRIVATE bmlib bmcv)
    target_compile_definitions(cosmo_infer PRIVATE COSMO_NN_USE_SOPHON_BACKEND)
endif()

if(COSMO_NN_USE_CPU_BACKEND)
    target_link_libraries(cosmo_infer PRIVATE onnxruntime)
    target_compile_definitions(cosmo_infer PRIVATE COSMO_NN_USE_CPU_BACKEND)
endif()
