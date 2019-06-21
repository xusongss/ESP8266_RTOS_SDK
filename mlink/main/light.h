#ifndef __LIGHT_H__
#define __LIGHT_H__
#include "mlink.h"
#include "mdf_common.h"

/**
 * @brief The value of the cid corresponding to each attribute of the light
 */
enum light_cid {
    LIGHT_CID_STATUS            = 0,
    LIGHT_CID_HUE               = 1,
    LIGHT_CID_SATURATION        = 2,
    LIGHT_CID_VALUE             = 3,
    LIGHT_CID_COLOR_TEMPERATURE = 4,
    LIGHT_CID_BRIGHTNESS        = 5,
    LIGHT_CID_MODE              = 6,
};

enum light_status {
    LIGHT_STATUS_OFF               = 0,
    LIGHT_STATUS_ON                = 1,
    LIGHT_STATUS_SWITCH            = 2,
    LIGHT_STATUS_HUE               = 3,
    LIGHT_STATUS_BRIGHTNESS        = 4,
    LIGHT_STATUS_COLOR_TEMPERATURE = 5,
};
/**
 * @brief Show layer of node by light.
 *
 * @param handle_data pointer of mlink_handle_data_t
 *
 * @return
 *     - MDF_OK
 *     - MDF_ERR_INVALID_ARG
 *     - MDF_FAIL
 */

mdf_err_t light_show_layer(mlink_handle_data_t *handle_data);
/**
 * @brief Set light characteristic
 *
 * @param cid characteristic id
 * @param arg arguments
 *
 * @return
 *     - MDF_OK
 *     - MDF_ERR_INVALID_ARG
 *     - MDF_FAIL
 */

mdf_err_t mlink_set_value(uint16_t cid, void *arg);
/**
 * @brief Get light characteristic
 *
 * @param cid characteristic id
 * @param arg arguments
 *
 * @return
 *     - MDF_OK
 *     - MDF_ERR_INVALID_ARG
 *     - MDF_FAIL
 */

mdf_err_t mlink_get_value(uint16_t cid, void *arg);
#endif
