#include "sim7080.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* -------- Local defines -------- */
/***********************************/
#define SIM7080_DBG_PRINT(dev, format, ...) \
do {\
    if (logger_print)\
    {\
        logger_print(format, __VA_ARGS__);\
    }\
} while (0)

#define POWER_UP_DELAY_MS 1500
#define POWER_DOWN_DELAY_MS 3000


/* -------- Local types -------- */
/*********************************/
typedef enum {
    POWER_UP,
    POWER_DOWN,
} power_state_t;

typedef enum {
    TXRX_SM_SEQ_SEND_AT,
    TXRX_SM_SEQ_WAIT_RESP,
    TXRX_SM_SEQ_PARSE_RESP,
} txrx_sm_t;

typedef enum {
    TXRX_RET_HW_ERROR_OCCURED,
    TXRX_RET_TIMEOUT_ERROR_OCCURED,
    TXRX_RET_ERROR_RESPONSE,
    TXRX_RET_WAITING,
    TXRX_RET_TABLE_SENT,
} txrx_ret_t;


/* -------- Local variables -------- */
/*************************************/
static const uint8_t end_of_at_seq[] = "\r\n";
static const uint8_t end_of_rsp_seq[] = "\r\n";

static uint8_t rx_buffer[512] = { 0 };
static int rx_cnt = 0;
static int rsp_recieved_flag = 0;

static txrx_sm_t txrx_seq_sm = TXRX_SM_SEQ_SEND_AT;
static int txrx_at_indx = 0;
static uint32_t txrx_started_tick_ms = 0;
static uint32_t txrx_curr_tick_ms = 0;

static struct sim7080_error_table {
    char *err_desc;
} error_table[] = {
    [SIM7080_RET_STATUS_SUCCESS] = { "Succecc" },
    [SIM7080_EMPTY_GREETING] = { "Wrong or empty greeting message after sim7080 boot up" },
    [SIM7080_RET_STATUS_BAD_ARGS] = { "Bad input arguments" },
    [SIM7080_RET_STATUS_HW_TX_FAIL] = { "UART TX func returned error or timeout event" },
    [SIM7080_RET_STATUS_HW_RX_FAIL] = { "UART RX func returned error or timeout event" },
    [SIM7080_RET_STATUS_NOT_SUPPORTED] = { "Called functionality is not supported" },
    [SIM7080_RET_STATUS_TIMEOUT] = { "Some AT command's response haven't been obtained" },
    [SIM7080_RET_STATUS_RSP_ERR] = { "SIM7080 reply with wrong message" },
};

static sim7080_at_cmd_table_t base_init_table[] = {
    { "AT",         "OK", 1000 },
    { "AT+CSCLK=0", "OK", 1000 }, /* Disable entering sleep mode */
    { "AT+CFUN=0",  "OK", 1000 }, /* Disable RF */
    { "AT+CNMP=2",  "OK", 1000 }, /* Phisical layer (GSM or LTE) is defined automatically */
    { "AT+CMNB=2",  "OK", 1000 }, /* Set preferred network (f.e. NB-Iot) */
};


/* -------- Function prototypes -------- */
/*****************************************/
static int txrx_send_at_cmd_table(sim7080_dev_t *dev, sim7080_at_cmd_table_t *table, size_t table_size);
static int txrx_return_op_status(int table_seq_err, sim7080_dev_t *dev, int *error_code);
static void txrx_reset_sm(void);
static int is_pattern_exist_in_data(uint8_t *data, size_t data_len,
                                    const char *pattern, size_t pattern_len); /* 1 - exists */
static void power_up(sim7080_dev_t *dev);
static void power_down(sim7080_dev_t *dev);

/* Driver API */
/**************/
int sim7080_init(sim7080_dev_t *dev, sim7080_ll_t *ll)
{
    int greeting_attempts = 5;
    int success_flag = 0;
    const char *expected_greeting = "+CPIN: READY\r\n\r\nSMS Ready\r\n";

    if (!dev || !ll || !ll->delay_ms || !ll->get_tick_ms || \
        !ll->pwrkey_pin_set || !ll->pwrkey_pin_reset || \
        !ll->transmit_data_polling_mode) {

        if (dev) {
            dev->state = SIM7080_SM_UNKNOWN;
        }

        return SIM7080_RET_STATUS_BAD_ARGS;
    }

    memset(dev, 0, sizeof(sim7080_dev_t));
    txrx_reset_sm();

    dev->ll = ll;
    dev->state = SIM7080_SM_BOOT_IN_PROGRESS;
    dev->power_state = POWER_DOWN;

    while (greeting_attempts--) {
        power_up(dev);

        /* Try to obtain greeting after module boot up */
        dev->ll->receive_data_polling_mode(rx_buffer, sizeof(rx_buffer), 5000);

        if (is_pattern_exist_in_data(rx_buffer, sizeof(rx_buffer),
                                     expected_greeting, 27)) {
            success_flag = 1;
            break;
        } else {
            power_down(dev);
            dev->ll->delay_ms(200);
        }
    }

    if (!success_flag) {
        dev->state = SIM7080_SM_BOOT_FAILED;
        return SIM7080_EMPTY_GREETING;
    }

    dev->state = SIM7080_SM_BOOT_DONE;
    dev->ll->receive_in_async_mode_start(rx_buffer + rx_cnt, 1);

    return SIM7080_RET_STATUS_SUCCESS;
}

void sim7080_debug_mode(sim7080_dev_t *dev,
                        void (*logger_p)(const char *format, ...))
{
    dev->logger_p = logger_p;
}

int sim7080_net_register(sim7080_dev_t *dev,
                         sim7080_network_settings_t *net_setup)
{
    if (!net_setup) {
        return SIM7080_RET_STATUS_BAD_ARGS;
    }

    dev->net_settings = net_setup;
    dev->state = SIM7080_SM_NET_REGISTRATION_IN_PROGRESS;
    txrx_reset_sm();

    return SIM7080_RET_STATUS_SUCCESS;
}

int sim7080_proto_connect(sim7080_dev_t *dev,
                          sim7080_protocol_settings_t *prot_setup)
{
    if (!prot_setup) {
        return SIM7080_RET_STATUS_BAD_ARGS;
    }

    dev->prot_settings = prot_setup;
    dev->state = SIM7080_SM_PROTO_CONNECT_IN_PROGRESS;
    txrx_reset_sm();

    return SIM7080_RET_STATUS_SUCCESS;
}

int sim7080_poll(sim7080_dev_t *dev, int *error_code)
{
    int rv = 0;

    switch (dev->state) {
    // case SIM7080_SM_BOOT_IN_PROGRESS:
    //     break;
    // case SIM7080_SM_INIT_IN_PROGRESS:
    //     rv = txrx_send_at_cmd_table(dev, base_init_table, sizeof(base_init_table)/sizeof(base_init_table[0]));
    //     return txrx_return_op_status(rv, dev, error_code);
    default:
        break;
    }

    return rv;
}

const char *sim7080_err_to_string(int error_code)
{
    if ((error_code < 0 )|| (error_code > (sizeof(error_table)/sizeof(error_table[0]) - 1))) {
        return "<null>. Wrong error code";
    }

    return error_table[error_code].err_desc;
}

void sim7080_rx_byte_isr(sim7080_dev_t *dev)
{
    ;
}

int sim7080_enter_sleep_mode(sim7080_dev_t *dev)
{
    power_down(dev);
    return SIM7080_RET_STATUS_SUCCESS;
}

int sim7080_exit_sleep_mode(sim7080_dev_t *dev)
{
    power_up(dev);
    return SIM7080_RET_STATUS_SUCCESS;
}

/* Implement local functions */
/*****************************/
static int txrx_send_at_cmd_table(sim7080_dev_t *dev, sim7080_at_cmd_table_t *table, size_t table_size)
{
    int ret = TXRX_RET_WAITING;

    if (txrx_at_indx < table_size) {

        /* Sending AT command from the table... */
        if (txrx_seq_sm == TXRX_SM_SEQ_SEND_AT) {
            if (dev->ll->transmit_data_polling_mode(
                    (uint8_t *)table[txrx_at_indx].at,
                    strlen(table[txrx_at_indx].at),
                    100) != SIM7080_RET_STATUS_SUCCESS) {
                return TXRX_RET_HW_ERROR_OCCURED;
            }

            if (dev->ll->transmit_data_polling_mode(
                    (uint8_t *)end_of_at_seq,
                    sizeof(end_of_at_seq) - 1,
                    100) != SIM7080_RET_STATUS_SUCCESS) {
                return TXRX_RET_HW_ERROR_OCCURED;
            }

            txrx_started_tick_ms = dev->ll->get_tick_ms(); /* save current ticks */
            txrx_seq_sm = TXRX_SM_SEQ_WAIT_RESP;

        /* Waititig for the module's response... */
        } else if (txrx_seq_sm == TXRX_SM_SEQ_WAIT_RESP) {
            if (rsp_recieved_flag) {
                txrx_seq_sm = TXRX_SM_SEQ_PARSE_RESP; /* Response received, start parsing */
            } else {
                txrx_curr_tick_ms = dev->ll->get_tick_ms();
                if ((txrx_curr_tick_ms - txrx_started_tick_ms) >=
                                table[txrx_at_indx].at_rsp_timeout_ms) {
                    ret = TXRX_RET_TIMEOUT_ERROR_OCCURED;
                }
            }

        /* Response obtained, parse it */
        } else if (txrx_seq_sm == TXRX_SM_SEQ_PARSE_RESP) {
            // if (memcmp((const void *)rx_buffer,
            //             table[txrx_at_indx].expected_good_pattern,
            //             strlen(table[txrx_at_indx].expected_good_pattern)) == 0) {
            //     ++txrx_at_indx;
            //     txrx_seq_sm = TXRX_SM_SEQ_SEND_AT;
            // } else {
            //     ret = TXRX_RET_ERROR_RESPONSE;
            // }
        }
    } else {
        ret = TXRX_RET_TABLE_SENT;
    }

    return ret;
}

static int txrx_return_op_status(int table_seq_err, sim7080_dev_t *dev, int *error_code)
{
    // if (table_seq_err == TXRX_RET_TABLE_SENT) {
    //     dev->state = SIM7080_SM_INIT_DONE;
    // } else if (table_seq_err == TXRX_RET_HW_ERROR_OCCURED) {
    //     if (error_code) {
    //         dev->state = SIM7080_SM_SOME_ERR_HAPPENED;
    //         *error_code = SIM7080_RET_STATUS_HW_TX_FAIL;
    //     }
    // } else if (table_seq_err == TXRX_RET_TIMEOUT_ERROR_OCCURED) {
    //     if (error_code) {
    //         dev->state = SIM7080_SM_SOME_ERR_HAPPENED;
    //         *error_code = SIM7080_RET_STATUS_TIMEOUT;
    //     }
    // } else if (table_seq_err == TXRX_RET_ERROR_RESPONSE) {
    //     if (error_code) {
    //         dev->state = SIM7080_SM_SOME_ERR_HAPPENED;
    //         *error_code = SIM7080_RET_STATUS_RSP_ERR;
    //     }
    // }

    return dev->state;
}

static void txrx_reset_sm(void)
{
    txrx_seq_sm = TXRX_SM_SEQ_SEND_AT;
    txrx_at_indx = 0;
    txrx_started_tick_ms = 0;
    txrx_curr_tick_ms = 0;

    rx_cnt = 0;
    rsp_recieved_flag = 0;
}

static int is_pattern_exist_in_data(uint8_t *data, size_t data_len,
                                    const char *pattern, size_t pattern_len)
{
    int k = 0;

    if (data_len < pattern_len) {
        return 0;
    }

    for (int i = 0; i < data_len; ++i) {
        if (data[i] == pattern[k]) {
            ++k;
        } else {
            k = 0;
        }

        if ((data_len - i) < (pattern_len - k)) {
            return 0;
        }
        if (k == pattern_len) {
            return 1;
        }
    }

    return 0;
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
