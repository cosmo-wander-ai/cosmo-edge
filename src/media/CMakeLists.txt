# Common (backend-agnostic) source files
set(MEDIA_COMMON_SRC
    Color.h
    EncodedImageInfo.cc           EncodedImageInfo.h
    FileDemuxStrategy.cc         FileDemuxStrategy.h
    IDemuxStrategy.h
    NetworkDemuxStrategy.cc      NetworkDemuxStrategy.h
    IOsdTextRenderer.h
    IVideoFrameProc.h
    OsdTextRenderer.cc           OsdTextRenderer.h
    PixelFormat.h
    PixelFormatUtils.cc          PixelFormatUtils.h
    PreviewPipelineMetrics.cc    PreviewPipelineMetrics.h
    RtspDemuxStrategy.cc         RtspDemuxStrategy.h
    UsbDemuxStrategy.cc          UsbDemuxStrategy.h
    VideoCodecType.h
    VideoDecoder.cc              VideoDecoder.h
    VideoDemuxer.cc              VideoDemuxer.h
    VideoDemuxerStream.cc
    VideoEncoder.cc              VideoEncoder.h
    VideoFrame.cc                VideoFrame.h
    VideoFrameProcFactory.h
    VideoPacket.cc               VideoPacket.h
    VideoUtil.cc                 VideoUtil.h
)

# Sophon-specific source files (require bmlib/bmcv/vpudec/vpuenc SDK)
# Includes VideoFrameProcSophon and its split-out method files (Codec/Drawing/Osd)
set(MEDIA_SOPHON_SRC
    VideoDecoderCreateSophon.cc
    VideoDecoderSophon.cc        VideoDecoderSophon.h
    VideoEncoderCreateSophon.cc
    VideoEncoderSophon.cc        VideoEncoderSophon.h
    VideoFrameCodec.cc
    VideoFrameDrawing.cc
    VideoFrameOsd.cc
    VideoFrameProcFactorySophon.cc
    VideoFrameProcSophon.cc      VideoFrameProcSophon.h
)

# CPU-specific source files (FFmpeg software codec + stb image processing)
set(MEDIA_CPU_SRC
    VideoDecoderCreateCpu.cc
    VideoDecoderCpu.cc           VideoDecoderCpu.h
    VideoEncoderCreateCpu.cc
    VideoEncoderCpu.cc           VideoEncoderCpu.h
    VideoFrameProcFactoryCpu.cc
    VideoFrameProcCpu.cc         VideoFrameProcCpu.h
)

# Rockchip hardware backend. MPP handles H.264/H.265 decode and H.264 encode,
# RGA handles admitted color-conversion/resize operations, and the RKNN fast
# path can bind MPP/RGA DMA-BUF inputs directly. FFmpeg and CPU transforms
# remain explicit fault-tolerance fallbacks.
set(MEDIA_ROCKCHIP_SRC
    RockchipRgaBuffer.h
    VideoDecoderCpu.cc           VideoDecoderCpu.h
    VideoDecoderCreateRockchip.cc
    VideoDecoderRockchip.cc      VideoDecoderRockchip.h
    VideoEncoderCpu.cc           VideoEncoderCpu.h
    VideoEncoderCreateRockchip.cc
    VideoEncoderRockchip.cc      VideoEncoderRockchip.h
    VideoFrameProcCpu.cc         VideoFrameProcCpu.h
    VideoFrameProcFactoryRockchip.cc
    VideoFrameProcRockchip.cc    VideoFrameProcRockchip.h
)

if(COSMO_MEDIA_USE_SOPHON_BACKEND)
    add_library(cosmo_media OBJECT ${MEDIA_COMMON_SRC} ${MEDIA_SOPHON_SRC})
elseif(COSMO_MEDIA_USE_CPU_BACKEND)
    add_library(cosmo_media OBJECT ${MEDIA_COMMON_SRC} ${MEDIA_CPU_SRC})
elseif(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
    add_library(cosmo_media OBJECT ${MEDIA_COMMON_SRC} ${MEDIA_ROCKCHIP_SRC})
else()
    message(FATAL_ERROR "No media backend selected")
endif()

# Project headers (media/, mem/, util/ via src/)
target_include_directories(cosmo_media PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cosmo_media PRIVATE cosmo_common
    ffmpeg_avcodec ffmpeg_avdevice ffmpeg_avformat ffmpeg_avutil ffmpeg_swscale
    stb mp4v2
)
if(COSMO_MEDIA_USE_SOPHON_BACKEND)
    target_link_libraries(cosmo_media PRIVATE bmlib bmcv bmvd bmvenc)
elseif(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
    target_link_libraries(cosmo_media PRIVATE rockchip_mpp rockchip_rga)
endif()
