add_library(cosmo_network OBJECT
    # http/
    http/HttpCommon.h
    http/HttpPost.cc              http/HttpPost.h
    http/HttpRequest.cc           http/HttpRequest.h
    http/HttpRequestHandler.cc    http/HttpRequestHandler.h
    http/HttpServer.cc            http/HttpServer.h
    http/HttpServerThread.cc      http/HttpServerThread.h
    http/HttpServerThreadPool.cc  http/HttpServerThreadPool.h
    http/MultipartStreamParser.cc http/MultipartStreamParser.h
    # mqtt/
    mqtt/MqttClient.cc            mqtt/MqttClient.h
    mqtt/MqttClientCallback.cc
    mqtt/MqttClientInternal.h
    mqtt/MqttClientUtil.cc
    mqtt/MqttProtocol.h
    mqtt/MqttTypes.h
    # msg/
    msg/MsgEnvelope.h
    msg/MsgTask.h
    msg/MsgThread.cc              msg/MsgThread.h

    mqtt/MqttProtocol.cc
)

# Project headers
target_include_directories(cosmo_network PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cosmo_network PRIVATE cosmo_common
    event_core event_extra event
    openssl_ssl openssl_crypto curl
    mqtt_c mqtt_cs
)
