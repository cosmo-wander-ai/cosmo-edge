# Common (backend-agnostic) source files
set(MEDIA_COMMON_SRC
    Color.h
    FileDemuxStrategy.cc         FileDemuxStrategy.h
    IDemuxStrategy.h
    IOsdTextRenderer.h
    IVideoFrameProc.h
    OsdTextRenderer.cc           OsdTextRenderer.h
    PixelFormat.h
    PixelFormatUtils.cc          PixelFormatUtils.h
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

if(COSMO_NN_USE_SOPHON_BACKEND)
    add_library(cosmo_media OBJECT ${MEDIA_COMMON_SRC} ${MEDIA_SOPHON_SRC})
else()
    add_library(cosmo_media OBJECT ${MEDIA_COMMON_SRC} ${MEDIA_CPU_SRC})
endif()

# Project headers (media/, mem/, util/ via src/)
target_include_directories(cosmo_media PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cosmo_media PRIVATE cosmo_common
    ffmpeg_avcodec ffmpeg_avdevice ffmpeg_avformat ffmpeg_avutil ffmpeg_swscale
    stb mp4v2
)
if(COSMO_NN_USE_SOPHON_BACKEND)
    target_link_libraries(cosmo_media PRIVATE bmlib bmcv bmvd bmvenc)
endif()
