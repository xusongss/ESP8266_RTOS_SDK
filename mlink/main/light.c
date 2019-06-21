#include "light.h"
#define TAG "light"
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
static uint32_t open_light = 1;
static uint32_t close_light = 0;
#define ENCRYPT_CMD 0xfa
#define LED_R_CMD   0xfb
#define LED_G_CMD   0xfc
#define LED_B_CMD   0xfd
#define LED_Y_CMD   0xfe
static int as569_i2c_init();

static int AS569_write_reg(uint8_t regAdd, uint8_t *data);

mdf_err_t mlink_set_value(uint16_t cid, void *arg){
	//as569_i2c_init();
	int value = *((int *)arg);
	MDF_LOGW("mlink_set_value: cid: %d, value: %d", cid, value);
	
	
	return MDF_OK;
}

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
 

mdf_err_t mlink_get_value(uint16_t cid, void *arg){
	as569_i2c_init();

	int *value = (int *)arg;
	switch (cid) {
		case LIGHT_CID_STATUS:
			*value = 0;
			break;

		case LIGHT_CID_HUE:
			*value = 180;
			break;

		case LIGHT_CID_SATURATION:
			*value = 99;
			break;

		case LIGHT_CID_VALUE:
			*value = 99;
			break;

		case LIGHT_CID_COLOR_TEMPERATURE:
			*value = 99;
			break;

		case LIGHT_CID_BRIGHTNESS:
			*value = 99;
			break;

		case LIGHT_CID_MODE:
			*value = 2;
			break;

		default:
			MDF_LOGE("No support cid: %d", cid);
			return MDF_FAIL;
	}
	
	MDF_LOGV("mlink_get_value cid: %d, value: %d", cid, *value);
	return MDF_OK;
}
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

mdf_err_t light_show_layer(mlink_handle_data_t *handle_data){
	switch (/*esp_mesh_get_layer()*/1) {
        case 1:
           MDF_LOGW("light_show_layer %d", __LINE__);   /**< red */
            break;

        case 2:
            MDF_LOGW("light_show_layer %d", __LINE__);    /**< orange */
            break;

        case 3:
            MDF_LOGW("light_show_layer %d", __LINE__);    /**< yellow */
            break;

        case 4:
            MDF_LOGW("light_show_layer %d", __LINE__);     /**< green */
            break;

        case 5:
            MDF_LOGW("light_show_layer %d", __LINE__);    /**< cyan */
            break;

        case 6:
            MDF_LOGW("light_show_layer %d", __LINE__);     /**< blue */
            break;

        case 7:
            MDF_LOGW("light_show_layer %d", __LINE__);   /**< purple */
            break;

        default:
            MDF_LOGW("light_show_layer %d", __LINE__);    /**< white */
            break;
    }

    return MDF_OK;
}

#if 0
#define AS569_ADDR 0x01
#define AS569_TAG "AS569_DRIVER"
#define WR_BIT                          I2C_MASTER_WRITE
#define RD_BIT                           I2C_MASTER_READ
#define LOG_AS569(fmt, ...)   ESP_LOGW(AS569_TAG, fmt, ##__VA_ARGS__)

#define AS569_ASSERT(a, format, b, ...) \
    if ((a) != 0) { \
        ESP_LOGE(AS569_TAG, format, ##__VA_ARGS__); \
        return b;\
    }

static int as569_write_reg(uint8_t slaveAdd, uint8_t regAdd, uint8_t *data)
{
    int res = 0;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	
    res = i2c_master_start(cmd);
	AS569_ASSERT(res, "file line [%d] as569_write_reg error[%d]", -1, __LINE__,  res);
	
    res = i2c_master_write_byte(cmd, (slaveAdd<<1)|WR_BIT, 1 /*ACK_CHECK_EN*/);
	AS569_ASSERT(res, "file line [%d] as569_write_reg error[%d]", -1, __LINE__,  res);
	
    res = i2c_master_write_byte(cmd, regAdd, 1 /*ACK_CHECK_EN*/);
	AS569_ASSERT(res, "file line [%d] as569_write_reg error[%d]",  -1,__LINE__,  res);
	
    res = i2c_master_write(cmd, data, 4, 1);//write 4 bytes
    AS569_ASSERT(res, "file line [%d] as569_write_reg error[%d]",  -1,__LINE__,  res);
    
    //res = i2c_master_write_byte(cmd, *data, 1 /*ACK_CHECK_EN*/);
    //AS569_ASSERT(res, "file line [%d] as569_write_reg error[%d]",  -1,__LINE__,  res);
    
    res = i2c_master_stop(cmd);
	AS569_ASSERT(res, "file line [%d] as569_write_reg error[%d]",  -1,__LINE__,  res);
	
    res = i2c_master_cmd_begin(0, cmd, 1000 / portTICK_RATE_MS);
	AS569_ASSERT(res, "file line [%d] as569_write_reg error[%d]",  -1, __LINE__,  res);
	
    i2c_cmd_link_delete(cmd);
	
    AS569_ASSERT(res, "as569_write_reg error", -1);
    return res;
}

static int as569_read_reg(uint8_t slaveAdd, uint8_t *pData, size_t data_size)
{
    int res = 0;
    
    if (data_size <= 0){
        LOG_AS569("size less than 0,not read\n");
        return -1;
    }
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    res |= i2c_master_start(cmd);
    res |= i2c_master_write_byte(cmd, (slaveAdd<<1)|WR_BIT, 1 /*ACK_CHECK_EN*/);
    res |= i2c_master_stop(cmd);
    res |= i2c_master_cmd_begin(0, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    cmd = i2c_cmd_link_create();
    res |= i2c_master_start(cmd);
    res |= i2c_master_write_byte(cmd, (slaveAdd<<1)|RD_BIT, 1 /*ACK_CHECK_EN*/);
    if (data_size > 1) {
        i2c_master_read(cmd, pData, data_size-1, 0x0);/*ACK_VAL*/
    }
    i2c_master_read_byte(cmd, pData+data_size-1, 1);/*NACK_VAL*/
    
    res |= i2c_master_stop(cmd);
    res |= i2c_master_cmd_begin(0, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    //AS569_ASSERT(res, "AS569ReadReg error", -1);
    return res;
}

static int AS569_write_reg(uint8_t regAdd, uint8_t *data)
{
    return as569_write_reg(AS569_ADDR, regAdd, data);
}

static int AS569_read_reg(uint8_t *data, size_t data_size)
{
  uint8_t res =0;

  if(as569_read_reg(AS569_ADDR,  data, data_size) == 0) {
      return res;
  }
  else
  {
      LOG_AS569("Read Register Failed!");
      res = -1;
      return res;
  }
}

static int as569_i2c_init()
{
	static int first = 1;
	static const i2c_config_t as569_i2c_cfg = {
	    .mode = I2C_MODE_MASTER,
	    .sda_io_num = 4,
	    .scl_io_num = 21,
	    .sda_pullup_en = GPIO_PULLUP_ENABLE,
	    .scl_pullup_en = GPIO_PULLUP_ENABLE,
	    .master.clk_speed = 100000
	};
	
	if(!first){
		return MDF_OK;
	}
	int ret = 0;
	ret = i2c_param_config(I2C_NUM_0, &as569_i2c_cfg);
	if(ret != 0){
		ESP_LOGE(AS569_TAG,"i2c_param_config");
	}else{
		printf("i2c_param_config success\n");
	}
	ret = i2c_driver_install(I2C_NUM_0, as569_i2c_cfg.mode, 0, 0, 0);
	if(ret != 0){
		ESP_LOGE(AS569_TAG,"i2c_driver_install");
	}else
	printf("i2c_driver_install success\n");
	
	MDF_ERROR_ASSERT(ret);
	MDF_ERROR_ASSERT(ret);
	
	first = 0;
	return MDF_OK;
}
#endif

