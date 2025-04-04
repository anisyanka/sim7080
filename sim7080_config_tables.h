#ifndef SIM7080_SETTINGS_H__
#define SIM7080_SETTINGS_H__

#include <stdint.h>
#include <stddef.h>

#include "sim7080.h"
#include "sim7080_certs.h"

#ifdef __cplusplus
extern "C" {
#endif

sim7080_at_cmd_table_t bootup_table[] = {
    { "", "\r\n+CPIN: READY\r\n\r\nSMS Ready\r\n", 20000, 0 } /* Waiting boot up time */
};

sim7080_at_cmd_table_t base_init_table[] = {
    { "AT+CREBOOT\r\n", "SMS Ready", 20000, 1000 },
    { "AT\r\n",         "OK", 4000, 0 },     /* Just ping */
    { "AT+CSCLK=0\r\n", "OK", 1000, 0 },     /* Disable entering sleep mode */
    { "AT+CFUN=0\r\n",  "OK", 10000, 2000 }, /* Disable RF */
    { "AT+CNMP=2\r\n",  "OK", 1000, 0 },     /* Phisical layer (GSM or LTE) is defined automatically */
    { "AT+CMNB=2\r\n",  "OK", 1000, 0 },     /* Set preferred network (f.e. NB-Iot) */
};

sim7080_at_cmd_table_t net_mts_nbiot_table[] = {
    { "AT+CGDCONT=1,\"IP\",\"iot\"\r\n", "OK", 1000, 0 },
    { "AT+CNCFG=0,1,\"iot\"\r\n",        "OK", 5000, 0 },
    { "AT+CFUN=1\r\n",                   "OK", 10000, 10000 },
    { "AT+CNACT=0,1\r\n",                "ACTIVE", 60000, 10000 },
};

sim7080_at_cmd_table_t setup_ssl_keys[] = {
    { "AT+CFSTERM\r\n",                                "",   1000, 2000 },

    /* Send root CA */
    { "AT+CFSINIT\r\n",                                "OK", 1000, 0 },
    { "AT+CFSWFILE=3,\"rootCA.crt\",0,1800,10000\r\n", "DOWNLOAD", 5000, 0 },
    { root_ca,                                         "",   5000, 0 },
    { "AT+CFSTERM\r\n",                                "OK", 1000, 0 },

    /* Send device public key */
    { "AT+CFSINIT\r\n",                                    "OK", 1000, 0 },
    { "AT+CFSWFILE=3,\"deviceCert.pem\",0,1776,10000\r\n", "DOWNLOAD", 5000, 0 },
    { device_pub_cert,                                     "",   5000, 0 },
    { "AT+CFSTERM\r\n",                                    "OK", 1000, 0 },

    /* Send device private key */
    { "AT+CFSINIT\r\n",                                          "OK", 1000, 0 },
    { "AT+CFSWFILE=3,\"devicePrivateKey.pem\",0,3220,10000\r\n", "DOWNLOAD", 5000, 0 },
    { device_privete_key,                                        "",   5000, 0 },
    { "AT+CFSTERM\r\n",                                          "OK", 1000, 0 },
};

sim7080_at_cmd_table_t protocol_yandex_mqtt_table[] = {
    /* Configure SSL parameters of a context identifier */
    { "AT+SMDISC\r\n", "", 1000, 2000 },
    { "AT+CSSLCFG=\"SNI\",0,\"mqtt.cloud.yandex.net\"\r\n", "OK", 2000, 0 },

    /* Set MQTT Parameter */
    { "AT+SMCONF=\"URL\",\"mqtt.cloud.yandex.net\",8883\r\n", "OK", 1000, 0 },
    { "AT+SMCONF=\"CLIENTID\",0\r\n",                         "OK", 1000, 0 },
    { "AT+SMCONF=\"KEEPTIME\",60\r\n",                        "OK", 1000, 0 },
    { "AT+SMCONF=\"CLEANSS\",1\r\n",                          "OK", 1000, 0 },
    { "AT+SMCONF=\"QOS\",1\r\n",                              "OK", 1000, 0 },

    /* Convert certs */
    { "AT+CSSLCFG=\"SSLVERSION\",0,3\r\n", "OK", 1000, 0 },
    { "AT+CSSLCFG=\"CONVERT\",2,\"rootCA.crt\"\r\n", "OK", 1000, 0 },
    { "AT+CSSLCFG=\"CONVERT\",1,\"deviceCert.pem\",\"devicePrivateKey.pem\"\r\n", "OK", 1000, 0 },
    { "AT+SMSSL=1,\"rootCA.crt\",\"deviceCert.pem\"\r\n",  "OK", 1000, 0 },

    /* Connect to broker */
    { "AT+SMCONN\r\n", "OK", 60000, 2000 },
};

#ifdef __cplusplus
}
#endif

#endif /* SIM7080_SETTINGS_H__ */
