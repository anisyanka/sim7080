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
 * All possible errors.
 */
typedef enum {
    SIM7080_RET_STATUS_SUCCESS,
    SIM7080_EMPTY_GREETING,
    SIM7080_RET_STATUS_BAD_ARGS,
    SIM7080_RET_STATUS_HW_TX_FAIL,
    SIM7080_RET_STATUS_HW_RX_FAIL,
    SIM7080_RET_STATUS_NOT_SUPPORTED,
    SIM7080_RET_STATUS_TIMEOUT,
    SIM7080_RET_STATUS_RSP_ERR,
} sim7080_err_t;

/*
 * SIM7080 module state machine
 */
typedef enum {
    SIM7080_SM_UNKNOWN = -1,

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

    /* Connect to the given protocol server (MQTT for now) */
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
    const char *at;                    /* AT command string with NO \r\n at the end */
    const char *expected_good_pattern; /* Good answer from the module. */
    uint32_t at_rsp_timeout_ms;        /* Wait module response that time */
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

    /* UART related functions. Polling mode. Returns SIM7080_RET_STATUS_SUCCESS in case of success */
    int (*transmit_data_polling_mode)(uint8_t *data, size_t len, uint32_t timeout_ms);
    int (*receive_data_polling_mode)(uint8_t *data, size_t len, uint32_t timeout_ms);

    /* UART related functions. IRQ mode. Returns SIM7080_RET_STATUS_SUCCESS in case of success */
    int (*receive_in_async_mode_start)(uint8_t *rx_data, size_t rx_desired_len);
    int (*receive_in_async_mode_stop)(void);
} sim7080_ll_t;

/*
 * Commom device struct
 */
typedef struct {
    sim7080_ll_t *ll;
    sim7080_network_settings_t *net_settings;
    sim7080_protocol_settings_t *prot_settings;
    void (*logger_p)(const char *format, ...);
    int state;
    int power_state;
} sim7080_dev_t;


/*******************************/
/************* API *************/
/*******************************/

/*
 * Toggle PWRKEY and waiting for the module greeting message.
 * This function must be called before any others.
 *
 * Returns SIM7080_RET_STATUS_SUCCESS in case of success
 * and the module is alive.
 */
int sim7080_init(sim7080_dev_t *dev, sim7080_ll_t *ll);

/*
 * Enable debug prints from the lib.
 */
void sim7080_debug_mode(sim7080_dev_t *dev,
                        void (*logger_p)(const char *format, ...));

/*
 * Register in the <net_setup> network.
 *
 * Returns SIM7080_RET_STATUS_SUCCESS in case of registration process has been started.
 * sim7080_poll() will return SIM7080_SM_NET_REGISTRATION_IN_PROGRESS.
 */
int sim7080_net_register(sim7080_dev_t *dev,
                         sim7080_network_settings_t *net_setup);

/*
 * Connect to the <prot_setup> network.
 *
 * Returns SIM7080_RET_STATUS_SUCCESS in case of connection process has been started.
 * sim7080_poll() will return SIM7080_SM_PROTO_CONNECT_IN_PROGRESS.
 */
int sim7080_proto_connect(sim7080_dev_t *dev,
                          sim7080_protocol_settings_t *prot_setup);

/*
 * Process function to make the transmittion state machine alive.
 * Call in separate a thread or in the main loop.
 *
 * Returns one of sim7080_sm_t states.
 * Bussiness logic is based on these returns. See example of using.
 *
 * Returns 'SIM7080_SOME_ERR_HAPPENED' value in case of error.
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
void sim7080_rx_byte_isr(sim7080_dev_t *dev);

/*
 * Sleep mode control. To make it works, sim7080 DTR pin must be used in the schematic.
 * Retutns sim7080_err_t.
 */
int sim7080_enter_sleep_mode(sim7080_dev_t *dev);
int sim7080_exit_sleep_mode(sim7080_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* SIM7080_H__ */
