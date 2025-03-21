#include "sim7080.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

static const uint8_t end_of_at_seq[] = "\r\n";
static const uint8_t end_of_rsp_seq[] = "\r\n";
static uint8_t rx_buffer[512] = { 0 };
static int rx_cnt = 0;
static int rsp_recieved_flag = 0;

/* AT response parsers */
/***********************/
static int default_pars(uint8_t *rsp, size_t rsplen,
    const char *pattern, size_t patternlen)
{
    return SIM7080_RET_STATUS_SUCCESS;
}

/* Local variables */
/*******************/
static struct sim7080_error_table {
    char *err_desc;
} error_table[] = {
    [SIM7080_RET_STATUS_SUCCESS] = { "Succecc" },
    [SIM7080_RET_STATUS_BAD_ARGS] = { "Bad input arguments" },
    [SIM7080_RET_STATUS_HW_TX_FAIL] = { "UART TX func returned error" },
    [SIM7080_RET_STATUS_NOT_SUPPORTED] = { "Called functionality is not supported" },
    [SIM7080_RET_STATUS_TIMEOUT] = { "Some AT command's response haven't been obtained" },
    [SIM7080_RET_STATUS_RSP_ERR] = { "SIM7080 response with error message" }, /* TODO: pass on which AT command */
};

static sim7080_at_cmd_table_t base_init_table[] = {
    { "AT+CREBOOT", 60000, SIM7080_COMPLEX_RSP, NULL, "OK", default_pars },
    { "AT",         6000,  SIM7080_SIMPLE_RSP, "OK", NULL, NULL },
    { "AT+CSCLK=0", 2000,  SIM7080_SIMPLE_RSP, "OK", NULL, NULL },
    { "AT+CPIN?",   2000,  SIM7080_COMPLEX_RSP, NULL, "+CPIN: READY", default_pars },
    { "AT+CFUN=0",  2000,  SIM7080_SIMPLE_RSP, "OK", NULL, NULL },
    { "AT+CNMP=2",  2000,  SIM7080_SIMPLE_RSP, "OK", NULL, NULL },
    { "AT+CMNB=2",  2000,  SIM7080_SIMPLE_RSP, "OK", NULL, NULL },
};

/* Transmit AT, receive response according to cmd tables */
/*********************************************************/
typedef enum {
    TXRX_SM_SEQ_SEND_AT,
    TXRX_SM_SEQ_WAIT_RESP,
    TXRX_SM_SEQ_PARSE_RESP,
} txrx_sm_sequence_t;

typedef enum {
    TXRX_RET_HW_ERROR_OCCURED,
    TXRX_RET_TIMEOUT_ERROR_OCCURED,
    TXRX_RET_ERROR_RESPONSE,
    TXRX_RET_WAITING,
    TXRX_RET_TABLE_SENT,
} txrx_ret_t;

static txrx_sm_sequence_t txrx_seq_sm = TXRX_SM_SEQ_SEND_AT;
static int txrx_at_indx = 0;
static uint32_t txrx_started_tick_ms = 0;
static uint32_t txrx_curr_tick_ms = 0;

static int txrx_table(sim7080_dev_t *dev, sim7080_at_cmd_table_t *table, size_t table_size)
{
    int ret = TXRX_RET_WAITING;

    if (txrx_at_indx < table_size) {

        /* Sending AT command from the table... */
        if (txrx_seq_sm == TXRX_SM_SEQ_SEND_AT) {
            if (dev->ll_funcs->transmit_data(
                    (uint8_t *)table[txrx_at_indx].at,
                    strlen(table[txrx_at_indx].at)) != SIM7080_RET_STATUS_SUCCESS) {
                return TXRX_RET_HW_ERROR_OCCURED;
            }

            if (dev->ll_funcs->transmit_data(
                    (uint8_t *)end_of_at_seq,
                    sizeof(end_of_at_seq) - 1) != SIM7080_RET_STATUS_SUCCESS) {
                return TXRX_RET_HW_ERROR_OCCURED;
            }

            txrx_started_tick_ms = dev->ll_funcs->get_tick_ms(); /* save current ticks */
            txrx_seq_sm = TXRX_SM_SEQ_WAIT_RESP;

        /* Waititig for the module's response... */
        } else if (txrx_seq_sm == TXRX_SM_SEQ_WAIT_RESP) {
            if (rsp_recieved_flag) {
                txrx_seq_sm = TXRX_SM_SEQ_PARSE_RESP; /* Response received, start parsing */
            } else {
                txrx_curr_tick_ms = dev->ll_funcs->get_tick_ms();
                if ((txrx_curr_tick_ms - txrx_started_tick_ms) >=
                                table[txrx_at_indx].at_rsp_timeout_ms) {
                    ret = TXRX_RET_TIMEOUT_ERROR_OCCURED;
                }
            }

        /* Response obtained, parse it */
        } else if (txrx_seq_sm == TXRX_SM_SEQ_PARSE_RESP) {
            if (table[txrx_at_indx].rsp_type == SIM7080_SIMPLE_RSP) {
                if (memcmp((const void *)rx_buffer,
                           table[txrx_at_indx].expected_good_rsp,
                           strlen(table[txrx_at_indx].expected_good_rsp)) == 0) {
                    ++txrx_at_indx;
                    txrx_seq_sm = TXRX_SM_SEQ_SEND_AT;
                } else {
                    ret = TXRX_RET_ERROR_RESPONSE;
                }
            } else if (table[txrx_at_indx].rsp_type == SIM7080_COMPLEX_RSP) {
                if (table[txrx_at_indx].rsp_parser) {

                    if (table[txrx_at_indx].rsp_parser(rx_buffer, rx_cnt,
                                    table[txrx_at_indx].expected_good_pattern,
                                    strlen(table[txrx_at_indx].expected_good_pattern)) == SIM7080_RET_STATUS_SUCCESS) {
                        ++txrx_at_indx;
                        txrx_seq_sm = TXRX_SM_SEQ_SEND_AT;
                    } else {
                        ret = TXRX_RET_ERROR_RESPONSE;
                    }
                }
            }
        }
    } else {
        ret = TXRX_RET_TABLE_SENT;
    }

    return ret;
}

static void txrx_table_reset(void)
{
    txrx_seq_sm = TXRX_SM_SEQ_SEND_AT;
    txrx_at_indx = 0;
    txrx_started_tick_ms = 0;
    txrx_curr_tick_ms = 0;
}

/* Driver API */
/**************/
int sim7080_init_hw_and_net_params(sim7080_dev_t *dev,
                                   sim7080_network_settings_t *net_setup,
                                   sim7080_protocol_settings_t *prot_setup)
{
    if (!dev || \
        !net_setup || \
        !prot_setup || \
        !dev->ll_funcs || \
        !dev->ll_funcs->delay_ms || \
        !dev->ll_funcs->get_tick_ms || \
        !dev->ll_funcs->pwrkey_pin_set || \
        !dev->ll_funcs->pwrkey_pin_reset || \
        !dev->ll_funcs->transmit_data) {

        if (dev) {
            dev->state = SIM7080_SM_SOME_ERR_HAPPENED;
        }

        return SIM7080_RET_STATUS_BAD_ARGS;
    }

    dev->net_settings = net_setup;
    dev->prot_settings = prot_setup;
    dev->state = SIM7080_SM_INITIAL;

    dev->ll_funcs->pwrkey_pin_reset();
    dev->ll_funcs->delay_ms(2000);
    dev->ll_funcs->pwrkey_pin_set();
    dev->ll_funcs->delay_ms(2000);

    return SIM7080_RET_STATUS_SUCCESS;
}

int sim7080_init(sim7080_dev_t *dev)
{
    dev->state = SIM7080_SM_INIT_IN_PROGRESS;
    txrx_table_reset();

    return SIM7080_RET_STATUS_SUCCESS;
}

int sim7080_net_connect(sim7080_dev_t *dev)
{
    dev->state = SIM7080_SM_NET_CONNECT_IN_PROGRESS;
    txrx_table_reset();

    return SIM7080_RET_STATUS_SUCCESS;
}

int sim7080_proto_connect(sim7080_dev_t *dev)
{
    dev->state = SIM7080_SM_PROTO_CONNECT_IN_PROGRESS;
    txrx_table_reset();

    return SIM7080_RET_STATUS_SUCCESS;
}

int sim7080_poll(sim7080_dev_t *dev, int *error_code)
{
    int rv = 0;

    if (error_code) {
        *error_code = SIM7080_RET_STATUS_SUCCESS;
    }

    switch (dev->state) {
    case SIM7080_SM_INIT_IN_PROGRESS:
        rv = txrx_table(dev, base_init_table, sizeof(base_init_table)/sizeof(base_init_table[0]));
        if (rv == TXRX_RET_TABLE_SENT) {
            dev->state = SIM7080_SM_INIT_DONE;
        } else if (rv == TXRX_RET_HW_ERROR_OCCURED) {
            if (error_code) {
                dev->state = SIM7080_SM_SOME_ERR_HAPPENED;
                *error_code = SIM7080_RET_STATUS_HW_TX_FAIL;
            }
        } else if (rv == TXRX_RET_TIMEOUT_ERROR_OCCURED) {
            if (error_code) {
                dev->state = SIM7080_SM_SOME_ERR_HAPPENED;
                *error_code = SIM7080_RET_STATUS_TIMEOUT;
            }
        } else if (rv == TXRX_RET_ERROR_RESPONSE) {
            if (error_code) {
                dev->state = SIM7080_SM_SOME_ERR_HAPPENED;
                *error_code = SIM7080_RET_STATUS_RSP_ERR;
            }
        }
        break;

    default:
        break;
    }
    return dev->state;
}

const char *sim7080_err_to_string(int error_code)
{
    if ((error_code < 0 )|| (error_code > (sizeof(error_table)/sizeof(error_table[0]) - 1))) {
        return "<null>. Wrong error code";
    }

    return error_table[error_code].err_desc;
}

void sim7080_rx_byte_isr(sim7080_dev_t *dev, uint8_t new_byte)
{
    static int wait_last_byte = 0;

    if (!rsp_recieved_flag) {
        if (end_of_rsp_seq[0] == new_byte) {
            wait_last_byte = 1;
            return;
        }

        if (wait_last_byte == 1) {
            if (end_of_rsp_seq[1] == new_byte) {
                rsp_recieved_flag = 1;
            } else {

                rx_buffer[rx_cnt++] = end_of_rsp_seq[0];
                if (rx_cnt >= sizeof(rx_buffer)) {
                    rx_cnt = 0; /* Too long answer. Abort it */
                }

                rx_buffer[rx_cnt++] = new_byte;
                if (rx_cnt >= sizeof(rx_buffer)) {
                    rx_cnt = 0; /* Too long answer. Abort it */
                }
            }

            wait_last_byte = 0;
        }

        rx_buffer[rx_cnt++] = new_byte;
        if (rx_cnt >= sizeof(rx_buffer)) {
            /* Too long answer. Abort it */
            rx_cnt = 0;
        }
    } else {
        wait_last_byte = 0;
    }
}

int sim7080_enter_sleep_mode(sim7080_dev_t *dev)
{
    return SIM7080_RET_STATUS_NOT_SUPPORTED;
}

int sim7080_exit_sleep_mode(sim7080_dev_t *dev)
{
    return SIM7080_RET_STATUS_NOT_SUPPORTED;
}
