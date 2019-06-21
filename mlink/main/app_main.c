#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "rom/ets_sys.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "mlink.h"

static const char *TAG = "MLINK_DEVICE";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    /* For accessing reason codes in case of disconnection */
    system_event_info_t *info = &event->event_info;
    
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGE(TAG, "Disconnect reason : %d", info->disconnected.reason);
        if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCAL_11B | WIFI_PROTOCAL_11G | WIFI_PROTOCAL_11N);
        }
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void wifi_init_sta()
{
    wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
}
static void app_mdns_init(void){
	//initialize mDNS
    ESP_ERROR_CHECK( mdns_init() );
    //set mDNS hostname (required if you want to advertise services)
    ESP_ERROR_CHECK( mdns_hostname_set(CONFIG_MDNS_HOSTNAME) );
    //set default mDNS instance name
    ESP_ERROR_CHECK( mdns_instance_name_set(CONFIG_MDNS_INSTANCE) );

	//initialize service
	uint8_t sta_mac[6]              = {0};
	
	ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac));
	mdns_txt_item_t meshTxtData[1]={
		{"mac", (char * )sta_mac}
	};
    ESP_ERROR_CHECK( mdns_service_add(CONFIG_MDNS_INSTANCE, "_mesh-http", "_tcp", 80, meshTxtData, 1) );
}
static void app_link_init();
void app_main()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

	wifi_init_sta();

	app_mdns_init();

	app_link_init();
	
}
#include "light.h"
#include "esp_http_server.h"
#include "esp_http_client.h"

static httpd_handle_t g_httpd_handle   = NULL;
static mdf_err_t mlink_get_mesh_info(httpd_req_t *req);
static esp_err_t mlink_device_request(httpd_req_t *req);


static void app_link_init(){
#if 0
	 /**
	 * @brief Configure MLink (LAN communication module)
	 *          1.add device
	 *          2.add characteristic of device
	 *          3.add characteristic handle for get/set value of characteristic.
	 */
	char name[32]                   = {0};    
	uint8_t sta_mac[6]              = {0};
	MDF_ERROR_ASSERT(esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac));
	snprintf(name, sizeof(name), "%s_%02x%02x", CONFIG_MESH_DEVICE_NAME, sta_mac[4], sta_mac[5]);
	
	MDF_ERROR_ASSERT(mlink_add_device(CONFIG_MESH_DEVICE_TID, name, CONFIG_MESH_DEVICE_SOFTWARE_VERSION));
	MDF_ERROR_ASSERT(mlink_add_characteristic(LIGHT_CID_STATUS, "on", CHARACTERISTIC_FORMAT_INT, CHARACTERISTIC_PERMS_RWT, 0, 3, 1));
	MDF_ERROR_ASSERT(mlink_add_characteristic(LIGHT_CID_HUE, "hue", CHARACTERISTIC_FORMAT_INT, CHARACTERISTIC_PERMS_RWT, 0, 360, 1));
	MDF_ERROR_ASSERT(mlink_add_characteristic(LIGHT_CID_SATURATION, "saturation", CHARACTERISTIC_FORMAT_INT, CHARACTERISTIC_PERMS_RWT, 0, 100, 1));
	MDF_ERROR_ASSERT(mlink_add_characteristic(LIGHT_CID_VALUE, "value", CHARACTERISTIC_FORMAT_INT, CHARACTERISTIC_PERMS_RWT, 0, 100, 1));
	MDF_ERROR_ASSERT(mlink_add_characteristic(LIGHT_CID_COLOR_TEMPERATURE, "color_temperature", CHARACTERISTIC_FORMAT_INT, CHARACTERISTIC_PERMS_RWT, 0, 100, 1));
	MDF_ERROR_ASSERT(mlink_add_characteristic(LIGHT_CID_BRIGHTNESS, "brightness", CHARACTERISTIC_FORMAT_INT, CHARACTERISTIC_PERMS_RWT, 0, 100, 1));
	MDF_ERROR_ASSERT(mlink_add_characteristic(LIGHT_CID_MODE, "mode", CHARACTERISTIC_FORMAT_INT, CHARACTERISTIC_PERMS_RW, 1, 3, 1));
	MDF_ERROR_ASSERT(mlink_add_characteristic_handle(mlink_get_value, mlink_set_value));

	/**     
	* @brief Add a request handler, handling request for devices on the LAN.    
	*/    
	MDF_ERROR_ASSERT(mlink_set_handle("show_layer", light_show_layer));
	/**
	 * @brief start mlink http server for handle data between device and moblie phone.
	 */
#endif
	mdf_err_t ret         = MDF_OK;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();

	/**
	* @brief This check should be a part of http_server
	*/
	config.max_open_sockets = (CONFIG_LWIP_MAX_SOCKETS - 3);

	MDF_LOGI("Starting server");
	MDF_LOGD("HTTP port         : %d", config.server_port);
	MDF_LOGD("Max URI handlers  : %d", config.max_uri_handlers);
	MDF_LOGD("Max open sessions : %d", config.max_open_sockets);
	MDF_LOGD("Max header length : %d", HTTPD_MAX_REQ_HDR_LEN);
	MDF_LOGD("Max URI length    : %d", HTTPD_MAX_URI_LEN);
	MDF_LOGD("Max stack size    : %d", config.stack_size);

	static const httpd_uri_t basic_handlers[] = {
	    {
	        .uri      = "/mesh_info",
	        .method   = HTTP_GET,
	        .handler  = mlink_get_mesh_info,
	        .user_ctx = NULL,
	    },
	    {
	        .uri      = "/device_request",
	        .method   = HTTP_POST,
	        .handler  = mlink_device_request,
	        .user_ctx = NULL,
	    },
	};
	ret = httpd_start(&g_httpd_handle, &config);
	
    MDF_ERROR_CHECK(ret != MDF_OK, ret, "Starts the web server");

    for (int i = 0, handlers_no = sizeof(basic_handlers) / sizeof(httpd_uri_t);
            i < handlers_no; i++) {
        ret = httpd_register_uri_handler(g_httpd_handle, basic_handlers + i);
        MDF_ERROR_CONTINUE(ret != ESP_OK, "Register uri failed for %d", i);
    }

    return MDF_OK;
	
}
static ssize_t mlink_httpd_get_hdr(httpd_req_t *req, const char *field, char **value)
{
    size_t size = httpd_req_get_hdr_value_len(req, field) + 1;
    MDF_ERROR_CHECK(size <= 0, MDF_FAIL, "Search for %s in request headers", field);

    *value = MDF_CALLOC(1, size + 1);

    if (httpd_req_get_hdr_value_str(req, field, *value, size) != MDF_OK) {
        MDF_LOGD("Get the value string of %s from the request headers", field);
        return MDF_FAIL;
    }

    MDF_LOGV("field: %s, value: %s", field, *value);

    return size;
}
static esp_err_t mlink_httpd_resp(httpd_req_t *req, const char *status_code, const char *message)
{
    char *data    = NULL;
    size_t size   = 0;
    mdf_err_t ret = MDF_FAIL;

    size = asprintf(&data, "{\"status_code\":-1,\"status_msg\":\"%s\"}", message);

    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "Set the HTTP content type");

    ret = httpd_resp_set_status(req, status_code);
    MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "Set the HTTP status code");

    ret = httpd_resp_send(req, data, size);
    MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "Send a complete HTTP response");

EXIT:
    MDF_FREE(data);
    return ret;
}

static mdf_err_t mlink_get_mesh_info(httpd_req_t *req){
	mdf_err_t ret           = ESP_OK;
   
    const char *status_code = "{\"status_code\":0}";
    const char *routing_table_size_str = "1";
	char 		routing_table_str[16];
	uint8_t 	sta_mac[6]              = {0};
	MDF_ERROR_ASSERT(esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac));
	sprintf(routing_table_str, MACSTR, MAC2STR(sta_mac));
	
	MDF_LOGI("%s %d", __FUNCTION__, __LINE__);
	
    /**
     * @brief Set the HTTP status code
     */
    ret = httpd_resp_set_status(req, HTTPD_200);
    MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "Set the HTTP status code");

    /**
     * @brief Set the HTTP content type
     */
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "Set the HTTP content type");

    /**
     * @brief Set some custom headers
     */
    ret = httpd_resp_set_hdr(req, "Mesh-Node-Num", routing_table_size_str);
    MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "Append any additional headers");
    ret = httpd_resp_set_hdr(req, "Mesh-Node-Mac", routing_table_str);
    MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "Append any additional headers");

    ret = httpd_resp_send(req, status_code, strlen(status_code));
    MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "Send a complete HTTP response");
	MDF_LOGI("%s %d", __FUNCTION__, __LINE__);
EXIT:

    if (ret != ESP_OK) {
        ret = httpd_resp_send_500(req);
        MDF_ERROR_CHECK(ret != MDF_OK, ret, "Helper function for HTTP 500");
    }

    return ret;
}
static esp_err_t mlink_device_request(httpd_req_t *req){
	esp_err_t ret               = MDF_FAIL;
	char *httpd_hdr_value       = NULL;
	ssize_t httpd_hdr_value_len = 0;
	
	httpd_hdr_value_len = mlink_httpd_get_hdr(req, "Content-Type", &httpd_hdr_value);
    MDF_ERROR_GOTO(httpd_hdr_value_len <= 0, EXIT, "Get 'Content-Type' from the request headers");

	if (strcasecmp(HTTPD_TYPE_JSON, httpd_hdr_value) /*not eq*/) {
		MDF_ERROR_GOTO(1, EXIT, "Content-Type Not "HTTPD_TYPE_JSON);
	}

	MDF_FREE(httpd_hdr_value);

    httpd_hdr_value_len = mlink_httpd_get_hdr(req, "Mesh-Node-Mac", &httpd_hdr_value);

	if (httpd_hdr_value_len <= 0) {
        MDF_LOGW("Get 'Mesh-Node-Mac' from the request headers");
        mlink_httpd_resp(req, HTTPD_400, "Must contain the 'Mesh-Node-Mac' field");
        goto EXIT;
    }

	size_t addrs_num = 0;        /**< Number of addresses */
	uint8_t *addrs_list = NULL;     /**< List of addresses */
	addrs_list = MDF_MALLOC(httpd_hdr_value_len / 2);

    for (char *tmp = httpd_hdr_value;; tmp++) {
        if (*tmp == ',' || *tmp == '\0') {
            mlink_mac_str2hex(tmp - 12, addrs_list + (addrs_num * /*MWIFI_ADDR_LEN*/6));
            addrs_num++;

            if (*tmp == '\0') {
                break;
            }
        }
    }

    MDF_FREE(httpd_hdr_value);
    MDF_LOGD("dest_addrs_num: %d", addrs_num);

    if (addrs_num == 0) {
        MDF_LOGW("Destination address format error");
        mlink_httpd_resp(req, HTTPD_400, "Destination address format error");
        goto EXIT;
    }

	size_t size  = req->content_len;;             /**< Length of data */
    char *data = MDF_MALLOC(req->content_len);              /**< Pointer of Data */

	for (int i = 0, recv_size = 0; i < 5 && recv_size < req->content_len; ++i, recv_size += ret) {
        ret = httpd_req_recv(req, data + recv_size, req->content_len - recv_size);
        MDF_ERROR_CONTINUE(ret == HTTPD_SOCK_ERR_TIMEOUT, "<HTTPD_SOCK_ERR_TIMEOUT> Read content data from the HTTP request");
        MDF_ERROR_GOTO(ret <= 0, EXIT, "<%s> Read content data from the HTTP request", mdf_err_to_name(ret));
    }
	//ESP_LOGW("mlink_httpd", "context: '%s'", httpd_data->data);
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        ret = httpd_resp_send_408(req);
        MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "Helper function for HTTP 408");
    }
	
	return MDF_OK;
EXIT:
	MDF_FREE(httpd_hdr_value);
	return ret;

}




