#ifndef SIM7080_H__
#define SIM7080_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Supported networks to work with.
 */
typedef enum {
    SIM7080_NET_NBIOT,
} sim7080_network_types_t;

/*
 * Supported protocols to work with.
 */
typedef enum {
    SIM7080_PROTOCOL_MQTT,
} sim7080_protocol_types_t;

/*
 * Type of responses from the module.
 */
typedef enum {
    SIM7080_SIMPLE_RSP,   /* Any "oneword" response. F.e. "OK\r\n" */
    SIM7080_COMPLEX_RSP,  /* Responses requred to be parsed */
} sim7080_rsp_type_t;

/*
 * All possible errors.
 */
typedef enum {
    SIM7080_STATUS_SUCCESS,
    SIM7080_STATUS_COMMON_FAIL,
    SIM7080_STATUS_HW_TX_FAIL,
    SIM7080_STATUS_NOT_SUPPORTED,
    SIM7080_STATUS_INIT_BAD_ARGS,
    SIM7080_STATUS_INIT_TIMEOUT,

} sim7080_err_t;

/*
 * SIM7080 module state machine
 */
typedef enum {
    SIM7080_SM_SOME_ERR_HAPPENED = -1,
    SIM7080_SM_INITIAL,
    SIM7080_SM_INIT_IN_PROGRESS,
    SIM7080_SM_INIT_DONE,
    SIM7080_SM_NET_CONNECT_IN_PROGRESS,
    SIM7080_SM_NET_CONNECT_FAILED,
    SIM7080_SM_NET_CONNECTED,
    SIM7080_SM_PROTO_CONNECT_IN_PROGRESS,
    SIM7080_SM_PROTO_CONNECT_FAILED,
    SIM7080_SM_PROTO_CONNECTED,
    SIM7080_SM_MQTT_READY_TO_WORK,
    SIM7080_SM_TRANSMIT_USER_DATA_DONE,
    SIM7080_SM_RECEIVE_NEW_USER_DATA_DONE,
} sim7080_sm_t;

/*
 * Structure to use by the driver or by user to point set of
 * AT-commands should be transmitted to sim7080 module in async manner.
 */
typedef struct {
    char *at;                             /* AT command string with NO \r\n at the end */
    uint32_t at_rsp_timeout_ms;           /* Wait module response that time */
    int rsp_type;                         /* SIM7080_SIMPLE_RESPONSE: the response will be just compared with 'expected_goot_response'.
                                             SIM7080_COMPLEX_RESPONSE - the response required to be parsed to find 'expected_good_pattern' */
    char *expected_good_rsp;              /* Good answer from the module. */
    char *expected_good_pattern;          /* Good pattern in the answer from the module  */

    int (*rsp_parser)(uint8_t *rsp, size_t rsplen); /* Parse input response if needed. Must return SIM7080_SUCCESS in case of success */
} sim7080_at_cmd_table_t;

/*
 * Settings of the supported networks.
 */
typedef struct {
    int network_type;  /* See 'sim7080_network_types_t' */
    union {
        struct {
            char *apn;         /* Access Point Name from mobile operator */
        } nbiot;

        /* Add other supported networks here */
    } u;
} sim7080_network_settings_t;


/*
 * Settings of the supported protocols MQTT broker
 */
typedef struct {
    int protocol_type; /* See 'sim7080_protocol_types_t' */
    union {
        struct {
            char *server;      /* MQTT broker server URL */
            int port;          /* MQTT broker port */
            int keeptime;      /* Hold connect time (60 - 180) */
            int cleanss;       /* Clean session (1 - yes, 0 - no) */
            int qos;           /* QOS level (0, 1). Must be supported by the server, in other case the parameter is ignored */
        } mqtt;

        /* Add other supported protocols here */
    } u;
} sim7080_protocol_settings_t;


/*
 * Low lovel functions implemented by user
 */
typedef struct {
    /* time related functions */
    void (*delay_ms)(uint32_t ms);
    uint32_t (*get_tick_ms)(void);

    /* DTR pin control. Put NULL if not used */
    void (*dtr_pin_set)(void);
    void (*dtr_pin_reset)(void);

    /* PWRKEY pin control */
    void (*pwrkey_pin_set)(void);
    void (*pwrkey_pin_reset)(void);

    /* UART related functions. Returns SIM7080_STATUS_SUCCESS in case of success */
    int (*transmit_data)(uint8_t *data, size_t len);
} sim7080_ll_t;

/*
 * Commom device struct
 */
typedef struct {
    sim7080_ll_t *ll_funcs;
    sim7080_network_settings_t *net_settings;
    sim7080_protocol_settings_t *prot_settings;
    int state;
} sim7080_dev_t;


/*******************************/
/************* API *************/
/*******************************/

/*
 * Assign net and protocol parameters and toggle POWER KEY
 *
 *  Returns SIM7080_SUCCESS in case of init process CAN BE started.
 */
int sim7080_init_hw_and_net_params(sim7080_dev_t *dev,
                                   sim7080_network_settings_t *net_setup,
                                   sim7080_protocol_settings_t *prot_setup);

/*
 * Start base init sequence
 *  - Disable entering sleep mode
 *  - Disable RF
 *  - Phisicayl layer (GSM or LTE) is defined automatically
 *  - Set preferred network (f.e. NB-Iot) or return unsupported error
 *
 * Returns SIM7080_SUCCESS in case of init process has been started.
 */
int sim7080_init(sim7080_dev_t *dev);

/*
 * Connect to <net_setup> network.
 * Status and progress of connection will be returned by sim7080_poll()
 *
 * Returns SIM7080_SUCCESS in case of connection process has been started.
 * sim7080_poll() will return SIM7080_NET_CONNECT_IN_PROGRESS.
 */
int sim7080_net_connect(sim7080_dev_t *dev);

/*
 * Connect to <prot_setup> network.
 * Status and progress of connection will be returned by sim7080_poll()
 *
 * Returns SIM7080_SUCCESS in case of connection process has been started.
 * sim7080_poll() will return SIM7080_PROTO_CONNECT_IN_PROGRESS.
 */
int sim7080_proto_connect(sim7080_dev_t *dev);

/*
 * Process function to make the transmittion state machine alive.
 * Call in separate a thread or in the main loop.
 *
 * Returns one of sim7080_sm_t states.
 * Bussiness logic is based on these returns. See example of using.
 *
 * Returns 'SIM7080_SOME_ERR_HAPPENED' value in case of error.abort
 * Particular error code will be placed in 'error_code' if not NULL.
 * Use it in sim7080_err_to_string() function to obtain human readable string.
 */
int sim7080_poll(sim7080_dev_t *dev, int *error_code);

/*
 * Convert 'error_code' to human string.
 */
const char *sim7080_err_to_string(int error_code);

/*
 * Parser of the input RX byte from the module.
 * Call in UART Rx IRQ handler.
 */
void sim7080_rx_byte_isr(sim7080_dev_t *dev, uint8_t new_byte);

/*
 * Used by high level code to abort internal rx buffer.
 * Might be used if we hadn't received end of rx condition, but already recevied silence on rx line.
 * Of course detecting the silence on the line (like in Modbus) must be implemented somewhere in
 * user code, but not in this driver.
 */
void sim7080_abort_in_progress_receiving(sim7080_dev_t *dev);

/*
 * Sleep mode control. To make it works, sim7080 DTR pin must be used in the schematic.
 */
void sim7080_enter_sleep_mode(sim7080_dev_t *dev);
void sim7080_exit_sleep_mode(sim7080_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* SIM7080_H__ */
