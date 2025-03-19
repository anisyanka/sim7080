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
    [SIM7080_SUCCESS] = { "Succecc" },
    [SIM7080_COMMON_FAIL] = { "Common unknown fail" },
    [SIM7080_NOT_SUPPORTED] = { "Called functionality is not supported" },
    [SIM7080_INIT_BAD_ARGS] = { "sim7080_init() bad input arguments" },
    [SIM7080_INIT_TIMEOUT] = { "sim7080_init() some AT command's response haven't been obtained" },
};

int sim7080_init(sim7080_dev_t *dev,
                 sim7080_network_settings_t *net_setup,
                 sim7080_protocol_settings_t *prot_setup)
{
    return SIM7080_SUCCESS;
}

int sim7080_net_connect(sim7080_dev_t *dev)
{
    return SIM7080_SUCCESS;
}

int sim7080_proto_connect(sim7080_dev_t *dev)
{
    return SIM7080_SUCCESS;
}

int sim7080_poll(sim7080_dev_t *dev, int *error_code)
{
    return SIM7080_INITIAL;
}

const char *sim7080_err_to_string(sim7080_dev_t *dev, int error_code)
{
    return "NULL";
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
