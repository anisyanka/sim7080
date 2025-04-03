#ifndef SIM7080_SETTINGS_H__
#define SIM7080_SETTINGS_H__

#include <stdint.h>
#include <stddef.h>

#include "sim7080.h"

#ifdef __cplusplus
extern "C" {
#endif

sim7080_at_cmd_table_t bootup_table[] = {
    { "", "\r\n+CPIN: READY\r\n\r\nSMS Ready\r\n", 20000, 0 } /* Waiting boot up time */
};

sim7080_at_cmd_table_t base_init_table[] = {
    { "AT+CREBOOT", "AT", 20000, 0 },
    { "AT",         "OK", 4000, 0 },     /* Just ping */
    { "AT+CSCLK=0", "OK", 1000, 0 },     /* Disable entering sleep mode */
    { "AT+CFUN=0",  "OK", 10000, 5000 }, /* Disable RF */
    { "AT+CNMP=2",  "OK", 1000, 0 },     /* Phisical layer (GSM or LTE) is defined automatically */
    { "AT+CMNB=2",  "OK", 1000, 0 },     /* Set preferred network (f.e. NB-Iot) */
};

sim7080_at_cmd_table_t net_mts_nbiot_table[] = {
    { "AT+CGDCONT=1,\"IP\",\"iot\"", "OK", 1000, 0 },
    { "AT+CNCFG=0,1,\"iot\"",        "OK", 1000, 0 },
    { "AT+CFUN=1",                   "OK", 10000, 5000 },
    { "AT+CNACT=0,1",                "ACTIVE", 60000, 10000 },
};

sim7080_at_cmd_table_t protocol_yandex_mqtt_table[] = {
    { "", "", 1000 },
};

#ifdef __cplusplus
}
#endif

#endif /* SIM7080_SETTINGS_H__ */
