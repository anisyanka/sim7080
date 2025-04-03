#ifndef SIM7080_H__
#define SIM7080_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * All possible errors.
 */
typedef enum {
    SIM7080_RET_STATUS_SUCCESS,
    SIM7080_RET_STATUS_BAD_ARGS,
    SIM7080_RET_STATUS_HW_TX_FAIL,
    SIM7080_RET_STATUS_HW_RX_FAIL,
    SIM7080_RET_STATUS_NOT_SUPPORTED,
    SIM7080_RET_STATUS_TIMEOUT,
    SIM7080_RET_STATUS_RSP_ERR,
} sim7080_err_t;

/*
 * Structure to use by the driver or by user to point set of
 * AT-commands should be transmitted to sim7080 module in async manner.
 */
typedef struct {
    /* AT command string with NO \r\n at the end */
    const char *at;

    /* Good answer from the module. */
    const char *expected_good_pattern;

    /*
     * Wait for the module response during that time.
     * If there is no response during this time,
     * app error handler will be triggered
     * with SIM7080_RET_STATUS_TIMEOUT error code
     */
    uint32_t at_rsp_timeout_ms;

    /*
     * Some time just to delay after obtaining the good response pattern.
     * Used to allow the module to finish sending its answer.
     * It might be usefull in some commands.
     */
    uint32_t at_after_rsp_timeout_ms;
} sim7080_at_cmd_table_t;

/*
 * Low lovel functions implemented by user
 */
typedef struct {
    /* time related functions */
    void (*delay_ms)(uint32_t ms);
    uint32_t (*get_tick_ms)(void);

    /* PWRKEY pin control */
    void (*pwrkey_pin_set)(void);
    void (*pwrkey_pin_reset)(void);

    /* UART related functions. Polling mode. Returns SIM7080_RET_STATUS_SUCCESS in case of success */
    int (*transmit_data_polling_mode)(uint8_t *data, size_t len, uint32_t timeout_ms);
} sim7080_ll_t;

/*
 * User's callbacks
 */
typedef struct {
    void (*net_registration_done)(void);
    void (*mqtt_server_connection_done)(void);
    void (*mqtt_transmission_done)(void);
    void (*error_occured)(int error);
} sim7080_app_func_t;

/*
 * Commom device struct
 */
typedef struct {
    sim7080_ll_t *ll;
    sim7080_app_func_t *app;
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
int sim7080_init(sim7080_dev_t *dev,
                 sim7080_ll_t *ll);

/*
 * Reset internal state machine. Call it in error handler.
 */
void sim7080_reset(sim7080_dev_t *dev);

/*
 * Setup application callbacks
 */
int sim7080_setup_app_cb(sim7080_dev_t *dev, sim7080_app_func_t *app);

/*
 * Enable debug prints from the lib.
 */
void sim7080_debug_mode(sim7080_dev_t *dev,
                        void (*logger_p)(const char *format, ...));

/*
 * Process function to make the transmittion state machine alive.
 * Call in separate a thread or in the main loop.
 */
void sim7080_poll(sim7080_dev_t *dev);

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
 * Sleep mode control. To make it works, sim7080 DTR pin must be used in the schematic.
 * Retutns sim7080_err_t.
 */
void sim7080_enter_sleep_mode(sim7080_dev_t *dev);
void sim7080_exit_sleep_mode(sim7080_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* SIM7080_H__ */
