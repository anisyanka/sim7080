#include "sim7080.h"
#include "sim7080_config_tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* -------- Local defines -------- */
/***********************************/
#define SIM7080_DBG_PRINT(dev, format, ...) \
do {\
    if (dev->logger_p)\
    {\
        dev->logger_p(format, __VA_ARGS__);\
    }\
} while (0)

#define POWER_UP_DELAY_MS       (1100U)
#define POWER_DOWN_DELAY_MS     (3000U)


/* -------- Local types -------- */
/*********************************/
typedef enum {
    POWER_UP,
    POWER_DOWN,
} power_state_t;

typedef enum {
    TXRX_SM_SEQ_SEND_AT,
    TXRX_SM_SEQ_WAIT_RESP,
    TXRX_SM_SEQ_WAIT_AFTER_RESP,
} txrx_sm_t;

typedef enum {
    TXRX_RET_HW_ERROR_OCCURED,
    TXRX_RET_TIMEOUT_ERROR_OCCURED,
    TXRX_RET_ERROR_RESPONSE,
    TXRX_RET_WAITING,
    TXRX_RET_TABLE_SENT,
} txrx_ret_t;

typedef enum {
    /* Waiting for module boot up after power supplied */
    SIM7080_SM_BOOT_IN_PROGRESS,
    SIM7080_SM_BOOT_FAILED,
    SIM7080_SM_BOOT_DONE,

    /* Initial setup: disalbe sleep mode, choose net prefences etc */
    SIM7080_SM_INITIAL_SETUP_IN_PROGRESS,
    SIM7080_SM_INITIAL_SETUP_FAILED,
    SIM7080_SM_INITIAL_SETUP_DONE,

    /* Register on the network by given APN */
    SIM7080_SM_NET_REGISTRATION_IN_PROGRESS,
    SIM7080_SM_NET_REGISTRATION_FAILED,
    SIM7080_SM_NET_REGISTRATION_DONE,

    /* Send ssl keys */
    SIM7080_SM_SSL_TX_IN_PROGRESS,
    SIM7080_SM_SSL_TX_FAILED,
    SIM7080_SM_SSL_TX_DONE,

    /* Connect to the given protocol server (MQTT for now) */
    SIM7080_SM_PROTO_CONNECT_IN_PROGRESS,
    SIM7080_SM_PROTO_CONNECT_FAILED,
    SIM7080_SM_PROTO_CONNECT_DONE,

    SIM7080_SM_MQTT_READY_TO_WORK,

    /* Sending data */
    SIM7080_SM_TRANSMIT_USER_DATA_IN_PROGRESS,
    SIM7080_SM_TRANSMIT_USER_DATA_FAILED,
    SIM7080_SM_TRANSMIT_USER_DATA_DONE,

    SIM7080_SM_RECEIVE_NEW_USER_DATA_DONE,
} sim7080_sm_t;


/* -------- Local variables -------- */
/*************************************/
static char sm_state_string[32] = { '[', 'n', 'u', 'l', 'l', ']', '\0'};

static uint8_t rx_buffer[512] = { 0 };
static int common_rx_cnt = 0;
static int rsp_recieved_flag = 0;

static char *expected_at_reply = "OK";
static volatile int expected_at_min_reply_len = 0;
static volatile int expected_at_reply_idx = 0;
static char *current_at = "AT";

static txrx_sm_t txrx_seq_sm = TXRX_SM_SEQ_SEND_AT;
static int txrx_at_indx = 0;
static uint32_t txrx_started_tick_ms = 0;
static uint32_t txrx_curr_tick_ms = 0;

static struct sim7080_error_table {
    char *err_desc;
} error_table[] = {
    [SIM7080_RET_STATUS_SUCCESS] = { "Succecc" },
    [SIM7080_RET_STATUS_BAD_ARGS] = { "Bad input arguments" },
    [SIM7080_RET_STATUS_HW_TX_FAIL] = { "UART TX func returned error or timeout event" },
    [SIM7080_RET_STATUS_HW_RX_FAIL] = { "UART RX starting returned error or timeout event" },
    [SIM7080_RET_STATUS_NOT_SUPPORTED] = { "Called functionality is not supported" },
    [SIM7080_RET_STATUS_TIMEOUT] = { "Some AT command's response haven't been obtained" },
    [SIM7080_RET_STATUS_RSP_ERR] = { "SIM7080 reply with unexpected message" },
    [SIM7080_RET_STATUS_MODULE_NOT_READY] = { "SIM7080 is not ready to send data. Might be initialization in progress." },
};


/* -------- Function prototypes -------- */
/*****************************************/
static int txrx_send_at_cmd_table(sim7080_dev_t *dev,
                                  sim7080_at_cmd_table_t *table,
                                  size_t table_size);
static void txrx_hanle_rv(sim7080_dev_t *dev, const char* sm_state, int rv,
                          int *app_err, int next_good_sm, int next_bad_sm);
static void txrx_reset_sm(void);
static void power_up(sim7080_dev_t *dev);
static void power_down(sim7080_dev_t *dev);


/* Driver API */
/**************/
int sim7080_init(sim7080_dev_t *dev, sim7080_ll_t *ll)
{
    if (!dev || !ll || !ll->delay_ms || !ll->get_tick_ms || \
        !ll->pwrkey_pin_set || !ll->pwrkey_pin_reset || \
        !ll->transmit_data_polling_mode) {
        return SIM7080_RET_STATUS_BAD_ARGS;
    }

    memset(dev, 0, sizeof(sim7080_dev_t));
    txrx_reset_sm();

    dev->ll = ll;
    dev->state = SIM7080_SM_BOOT_IN_PROGRESS;
    dev->power_state = POWER_DOWN;

    power_up(dev);
    return SIM7080_RET_STATUS_SUCCESS;
}

void sim7080_reset(sim7080_dev_t *dev)
{
    dev->ll->pwrkey_pin_reset();
    dev->ll->delay_ms(16000);
    dev->ll->pwrkey_pin_set();

    txrx_reset_sm();
    dev->state = SIM7080_SM_BOOT_IN_PROGRESS;
    dev->power_state = POWER_UP;
}

int sim7080_setup_app_cb(sim7080_dev_t *dev, sim7080_app_func_t *app)
{
    if (!dev || !app) {
        return SIM7080_RET_STATUS_BAD_ARGS;
    }

    dev->app = app;
    return SIM7080_RET_STATUS_SUCCESS;
}

void sim7080_debug_mode(sim7080_dev_t *dev,
                        void (*logger_p)(const char *format, ...))
{
    dev->logger_p = logger_p;
}

void sim7080_poll(sim7080_dev_t *dev)
{
    static int txrx_rv = 0;
    static int app_error = 0;

    switch (dev->state) {
    /* State when fw started and power key has been toggled */
    /*------------------------------------------------------*/
    case SIM7080_SM_BOOT_IN_PROGRESS:
        txrx_rv = txrx_send_at_cmd_table(dev, bootup_table,
                                sizeof(bootup_table)/sizeof(bootup_table[0]));
        txrx_hanle_rv(dev, "boot up", txrx_rv, &app_error,
                      SIM7080_SM_BOOT_DONE,
                      SIM7080_SM_BOOT_FAILED);
        break;
    case SIM7080_SM_BOOT_DONE:
        txrx_reset_sm();
        if (dev->app->bootup_done) {
            dev->app->bootup_done();
        }
        dev->state = SIM7080_SM_INITIAL_SETUP_IN_PROGRESS;
        break;

    /* Base module init sequence has been started */
    /*--------------------------------------------*/
    case SIM7080_SM_INITIAL_SETUP_IN_PROGRESS:
        txrx_rv = txrx_send_at_cmd_table(dev, base_init_table,
                            sizeof(base_init_table)/sizeof(base_init_table[0]));
        txrx_hanle_rv(dev, "initial setup", txrx_rv, &app_error,
                      SIM7080_SM_INITIAL_SETUP_DONE,
                      SIM7080_SM_INITIAL_SETUP_FAILED);
        break;
    case SIM7080_SM_INITIAL_SETUP_DONE:
        txrx_reset_sm();
        if (dev->app->initial_setup_done) {
            dev->app->initial_setup_done();
        }
        dev->state = SIM7080_SM_NET_REGISTRATION_IN_PROGRESS;
        break;

    /* Base init done. Setup NB-Iot connection has been started */
    /*----------------------------------------------------------*/
    case SIM7080_SM_NET_REGISTRATION_IN_PROGRESS:
        txrx_rv = txrx_send_at_cmd_table(dev, net_mts_nbiot_table,
                    sizeof(net_mts_nbiot_table)/sizeof(net_mts_nbiot_table[0]));
        txrx_hanle_rv(dev, "net registration", txrx_rv, &app_error,
                      SIM7080_SM_NET_REGISTRATION_DONE,
                      SIM7080_SM_NET_REGISTRATION_FAILED);
        break;
    case SIM7080_SM_NET_REGISTRATION_DONE:
        txrx_reset_sm();
        if (dev->app->net_registration_done) {
            dev->app->net_registration_done();
        }
        dev->state = SIM7080_SM_SSL_TX_IN_PROGRESS;
        break;

    /* Setup SSL device keys */
    /*-----------------------*/
    case SIM7080_SM_SSL_TX_IN_PROGRESS:
        txrx_rv = txrx_send_at_cmd_table(dev, setup_ssl_keys,
                    sizeof(setup_ssl_keys)/sizeof(setup_ssl_keys[0]));
        txrx_hanle_rv(dev, "Set SSL keys", txrx_rv, &app_error,
                      SIM7080_SM_SSL_TX_DONE,
                      SIM7080_SM_SSL_TX_FAILED);
        break;
    case SIM7080_SM_SSL_TX_DONE:
        txrx_reset_sm();
        if (dev->app->setup_device_keys_done) {
            dev->app->setup_device_keys_done();
        }
        dev->state = SIM7080_SM_PROTO_CONNECT_IN_PROGRESS;
        break;

    /* Connect to MQTT server */
    /*------------------------*/
    case SIM7080_SM_PROTO_CONNECT_IN_PROGRESS:
        txrx_rv = txrx_send_at_cmd_table(dev, protocol_yandex_mqtt_table,
                sizeof(protocol_yandex_mqtt_table)/sizeof(protocol_yandex_mqtt_table[0]));
        txrx_hanle_rv(dev, "connect mqtt server", txrx_rv, &app_error,
                      SIM7080_SM_PROTO_CONNECT_DONE,
                      SIM7080_SM_PROTO_CONNECT_FAILED);
        break;
    case SIM7080_SM_PROTO_CONNECT_DONE:
        txrx_reset_sm();
        if (dev->app->mqtt_server_connection_done) {
            dev->app->mqtt_server_connection_done();
        }
        dev->state = SIM7080_SM_MQTT_READY_TO_WORK;
        break;

    /* Full initialization and connections DONE */
    /*------------------------------------------*/
    case SIM7080_SM_MQTT_READY_TO_WORK:
        break;

    /* Some error uccured */
    /*--------------------*/
    case SIM7080_SM_BOOT_FAILED:
    case SIM7080_SM_INITIAL_SETUP_FAILED:
    case SIM7080_SM_NET_REGISTRATION_FAILED:
    case SIM7080_SM_PROTO_CONNECT_FAILED:
    case SIM7080_SM_SSL_TX_FAILED:
        if (dev->app->error_occured) {
            dev->app->error_occured(app_error);
        }
        break;

    default:
        break;
    }
}

const char *sim7080_err_to_string(int error_code)
{
    static char ret_error_string[512] = { 0 };

    if ((error_code < 0 ) || \
        (error_code > (sizeof(error_table)/sizeof(error_table[0]) - 1))) {
        return "<null>. Wrong error code";
    } else {
        (void)snprintf(ret_error_string, sizeof(ret_error_string),
                       "[%s] Tried to send AT=\"%s\". But got \"%s\"",
                       sm_state_string, current_at,
                       error_table[error_code].err_desc);
    }

    return ret_error_string;
}

void sim7080_rx_byte_isr(sim7080_dev_t *dev, uint8_t new_byte)
{
    rx_buffer[common_rx_cnt++] = new_byte;
    if (common_rx_cnt >= sizeof(rx_buffer)) {
        common_rx_cnt = 0;
    }

    if (rsp_recieved_flag == 0) {
        if (new_byte == expected_at_reply[expected_at_reply_idx]) {
            ++expected_at_reply_idx;

            if (expected_at_reply_idx == expected_at_min_reply_len) {
                rsp_recieved_flag = 1;
            }
        } else {
            /* Start comparing again to find "substring" */
            expected_at_reply_idx = 0;

            if (new_byte == expected_at_reply[expected_at_reply_idx]) {
                ++expected_at_reply_idx;
            }
        }
    }
}

void sim7080_enter_sleep_mode(sim7080_dev_t *dev)
{
    power_down(dev);
}

void sim7080_exit_sleep_mode(sim7080_dev_t *dev)
{
    power_up(dev);
}

int sim7080_publish_data(sim7080_dev_t *dev, const char *data, size_t len)
{
    if (dev->state != SIM7080_SM_MQTT_READY_TO_WORK) {
        return SIM7080_RET_STATUS_MODULE_NOT_READY;
    }

    // TODO: put at variable out of this code.
    // Might be we need to implement a script to generate register ID (or topic ID) during fw flashing
    // and compose this char *at there before calling sim7080_publish_data();

    char *at = "AT+SMPUB=\"$registries/are6phis3t903qjfrje3/events\",93,0,1\r\n";

    dev->ll->transmit_data_polling_mode((uint8_t *)at, strlen(at), 1000);
    dev->ll->delay_ms(10);
    dev->ll->transmit_data_polling_mode((uint8_t *)data, len, 1000);

    return SIM7080_RET_STATUS_SUCCESS;
}

/* Implement local functions */
/*****************************/
static void txrx_hanle_rv(sim7080_dev_t *dev, const char* sm_state, int rv,
                          int *app_err, int next_good_sm, int next_bad_sm)
{
    *app_err = SIM7080_RET_STATUS_SUCCESS;
    strcpy(sm_state_string, sm_state);

    switch (rv) {
    case TXRX_RET_HW_ERROR_OCCURED:
        dev->state = next_bad_sm;
        *app_err = SIM7080_RET_STATUS_HW_TX_FAIL;
        break;

    case TXRX_RET_TIMEOUT_ERROR_OCCURED:
        dev->state = next_bad_sm;
        *app_err = SIM7080_RET_STATUS_TIMEOUT;
        break;

    case TXRX_RET_ERROR_RESPONSE:
        dev->state = next_bad_sm;
        *app_err = SIM7080_RET_STATUS_RSP_ERR;
        break;

    case TXRX_RET_WAITING:
        break;

    case TXRX_RET_TABLE_SENT:
        dev->state = next_good_sm;
        break;

    default:
        break;
    }
}

static int txrx_send_at_cmd_table(sim7080_dev_t *dev, sim7080_at_cmd_table_t *table, size_t table_size)
{
    int ret = TXRX_RET_WAITING;

    if (txrx_at_indx < table_size) {
        if (txrx_seq_sm == TXRX_SM_SEQ_SEND_AT) { /* Sending AT command from the table... */

            dev->ll->delay_ms(100);

            common_rx_cnt = 0;
            memset(rx_buffer, 0, sizeof(rx_buffer));

            current_at = (char *)table[txrx_at_indx].at;

            expected_at_reply = (char *)table[txrx_at_indx].expected_good_pattern;
            expected_at_min_reply_len = strlen(table[txrx_at_indx].expected_good_pattern);
            expected_at_reply_idx = 0;
            rsp_recieved_flag = 0;

            size_t at_len = strlen(table[txrx_at_indx].at);
            if (at_len) {
                if (dev->ll->transmit_data_polling_mode(
                        (uint8_t *)table[txrx_at_indx].at, at_len, 2000) \
                                    != SIM7080_RET_STATUS_SUCCESS) {
                    return TXRX_RET_HW_ERROR_OCCURED;
                }
            }

            txrx_started_tick_ms = dev->ll->get_tick_ms();

            if (expected_at_min_reply_len) {
                txrx_seq_sm = TXRX_SM_SEQ_WAIT_RESP;
            } else {
                txrx_seq_sm = TXRX_SM_SEQ_WAIT_AFTER_RESP;
            }
        } else if (txrx_seq_sm == TXRX_SM_SEQ_WAIT_RESP) { /* Waititig for the module's response... */
            if (rsp_recieved_flag) {
                txrx_started_tick_ms = dev->ll->get_tick_ms();
                txrx_seq_sm = TXRX_SM_SEQ_WAIT_AFTER_RESP;
            } else {
                txrx_curr_tick_ms = dev->ll->get_tick_ms();
                if ((txrx_curr_tick_ms - txrx_started_tick_ms) >=
                                table[txrx_at_indx].at_rsp_timeout_ms) {
                    if (common_rx_cnt == 0) {
                        ret = TXRX_RET_TIMEOUT_ERROR_OCCURED;
                    } else {
                        ret = TXRX_RET_ERROR_RESPONSE;
                    }
                }
            }
        } else if (txrx_seq_sm == TXRX_SM_SEQ_WAIT_AFTER_RESP) { /* Wait a bit after getting success response */
            txrx_curr_tick_ms = dev->ll->get_tick_ms();
            if ((txrx_curr_tick_ms - txrx_started_tick_ms) >=
                            table[txrx_at_indx].at_after_rsp_timeout_ms) {
                ++txrx_at_indx;
                txrx_seq_sm = TXRX_SM_SEQ_SEND_AT;
            }
        }
    } else {
        ret = TXRX_RET_TABLE_SENT;
    }

    return ret;
}

static void txrx_reset_sm(void)
{
    txrx_seq_sm = TXRX_SM_SEQ_SEND_AT;
    txrx_at_indx = 0;
    txrx_started_tick_ms = 0;
    txrx_curr_tick_ms = 0;
}

static void power_up(sim7080_dev_t *dev)
{
    if (dev->power_state == POWER_UP) {
        return;
    }

    /* power cycle */
    dev->ll->pwrkey_pin_set();
    dev->ll->delay_ms(100);

    dev->ll->pwrkey_pin_reset();
    dev->ll->delay_ms(POWER_UP_DELAY_MS);
    dev->ll->pwrkey_pin_set();

    dev->power_state = POWER_UP;
}

static void power_down(sim7080_dev_t *dev)
{
    if (dev->power_state == POWER_DOWN) {
        return;
    }

    /* power cycle */
    dev->ll->pwrkey_pin_set();
    dev->ll->delay_ms(100);

    dev->ll->pwrkey_pin_reset();
    dev->ll->delay_ms(POWER_DOWN_DELAY_MS);
    dev->ll->pwrkey_pin_set();
    dev->ll->delay_ms(100);

    dev->power_state = POWER_DOWN;
}
