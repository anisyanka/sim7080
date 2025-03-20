#include "sim7080.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

static const char end_of_at_seq[] = "\r\n";
static const char end_of_rsp_seq[] = "\r\n";

struct sim7080_error_table {
    char *err_desc;
} error_table[] = {
    [SIM7080_STATUS_SUCCESS] = { "Succecc" },
    [SIM7080_STATUS_COMMON_FAIL] = { "Common unknown fail" },
    [SIM7080_STATUS_HW_TX_FAIL] = { "uart TX func returned error" },
    [SIM7080_STATUS_NOT_SUPPORTED] = { "Called functionality is not supported" },
    [SIM7080_STATUS_INIT_BAD_ARGS] = { "bad input arguments" },
    [SIM7080_STATUS_INIT_TIMEOUT] = { "some AT command's response haven't been obtained" },
};

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

        return SIM7080_STATUS_INIT_BAD_ARGS;
    }

    dev->net_settings = net_setup;
    dev->prot_settings = prot_setup;
    dev->state = SIM7080_SM_INITIAL;

    dev->ll_funcs->pwrkey_pin_reset();
    dev->ll_funcs->delay_ms(2000);
    dev->ll_funcs->pwrkey_pin_set();
    dev->ll_funcs->delay_ms(2000);

    return SIM7080_STATUS_SUCCESS;
}

int sim7080_init(sim7080_dev_t *dev)
{
    dev->state = SIM7080_SM_INIT_IN_PROGRESS;
    return SIM7080_STATUS_SUCCESS;
}

int sim7080_net_connect(sim7080_dev_t *dev)
{
    dev->state = SIM7080_SM_NET_CONNECT_IN_PROGRESS;
    return SIM7080_STATUS_SUCCESS;
}

int sim7080_proto_connect(sim7080_dev_t *dev)
{
    dev->state = SIM7080_SM_PROTO_CONNECT_IN_PROGRESS;
    return SIM7080_STATUS_SUCCESS;
}

int sim7080_poll(sim7080_dev_t *dev, int *error_code)
{
    return dev->state;
}

const char *sim7080_err_to_string(int error_code)
{
    if ((error_code < 0 )|| (error_code > (sizeof(error_table)/sizeof(error_table[0]) -1))) {
        return "<null>. Wrong error code";
    }

    return error_table[error_code].err_desc;
}

void sim7080_rx_byte_isr(sim7080_dev_t *dev, uint8_t new_byte)
{
    ;
}

void sim7080_abort_in_progress_receiving(sim7080_dev_t *dev)
{
    ;
}

void sim7080_enter_sleep_mode(sim7080_dev_t *dev)
{
    ;
}

void sim7080_exit_sleep_mode(sim7080_dev_t *dev)
{
    ;
}
