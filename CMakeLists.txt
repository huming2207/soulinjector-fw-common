idf_component_register(
        SRCS
        "comm/http_downloader.cpp" "comm/http_downloader.hpp"
        "comm/mqtt_client.cpp" "comm/mqtt_client.hpp" "comm/mq_defs.hpp"
        "comm/rpc_cmd_packet.hpp" "comm/rpc_report_packet.hpp"

        INCLUDE_DIRS
        "." "reporter" "comm" "misc"

        REQUIRES
        "driver" "esp_event" "esp_http_client" "esp-mqtt" "spi_flash" "efuse" "arduino_json"
)
