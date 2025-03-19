#ifndef SIM7080_SETTINGS_H__
#define SIM7080_SETTINGS_H__

#include <stdint.h>
#include <stddef.h>

#include "sim7080.h"

#ifdef __cplusplus
extern "C" {
#endif

sim7080_network_settings_t net_mts_nbiot = {
    .network_type = SIM7080_NET_NBIOT,
    .u.nbiot.apn = "iot"
};

sim7080_protocol_settings_t protocol_yandex_mqtt = {
    .protocol_type = SIM7080_PROTOCOL_MQTT,
    .u.mqtt.server = "mqtt.cloud.yandex.net",
    .u.mqtt.port = 8883,
    .u.mqtt.keeptime = 60,
    .u.mqtt.cleanss = 1,
    .u.mqtt.qos = 1,
};

#ifdef __cplusplus
}
#endif

#endif /* SIM7080_SETTINGS_H__ */