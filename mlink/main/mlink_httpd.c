/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "mdf_common.h"
#include "mlink.h"
#if  0
#include "mupgrade.h"
#endif
#define MLINK_HTTPD_FIRMWARE_URL_LEN (128)
#define MLINK_HTTPD_RESP_TIMEROUT_MS (15000)


#define MWIFI_PAYLOAD_LEN       (1456) /**< Max payload size(in bytes) */

#define MWIFI_ADDR_LEN          (6) /**< Length of MAC address */
#define MWIFI_ADDR_NONE         {0x0, 0x0, 0x0, 0x0, 0x0, 0x0}
#define MWIFI_ADDR_ROOT         {0xFF, 0x0, 0x0, 0x1, 0x0, 0x0}
#define MWIFI_ADDR_ANY          {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF} /**< All node in the mesh network */
#define MWIFI_ADDR_BROADCAST    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0XFE} /**< Other node except the root */
#define MWIFI_ADDR_IS_EMPTY(addr) (((addr)[0] | (addr)[1] | (addr)[2] | (addr)[3] | (addr)[4] | (addr)[5]) == 0x0)
#define MWIFI_ADDR_IS_ANY(addr)   (((addr)[0] & (addr)[1] & (addr)[2] & (addr)[3] & (addr)[4] & (addr)[5]) == 0xFF)
#define MWIFI_ADDR_IS_BROADCAST(addr) (((addr)[0] & (addr)[1] & (addr)[2] & (addr)[3] & (addr)[4]) == 0xFF && (addr)[5] == 0xFE)

/**
 * @brief Protocol of transmitted application data
 */
typedef enum {
    MESH_PROTO_BIN,     /**< binary */
    MESH_PROTO_HTTP,    /**< HTTP protocol */
    MESH_PROTO_JSON,    /**< JSON format */
    MESH_PROTO_MQTT,    /**< MQTT protocol */
} mesh_proto_t;


/**
 * @brief The flag of http chunks
 */
enum {
    MLINK_HTTPD_CHUNKS_NONE = 0, /**< Invalid connection */
    MLINK_HTTPD_CHUNKS_DATA,     /**< Data only */
    MLINK_HTTPD_CHUNKS_HEADER,   /**< Add the header of http chunks */
    MLINK_HTTPD_CHUNKS_BODY,     /**< Add the body of http chunks */
    MLINK_HTTPD_CHUNKS_FOOTER,   /**< Add the body of http chunks */
};

/**
 * @brief Record the connected structure
 */
typedef struct {
    httpd_handle_t handle; /**< Every instance of the server will have a unique handle. */
    TimerHandle_t timer;   /**< Waiting for response timeout */
    uint16_t sockfd;       /**< Socket descriptor for sending data */
    uint16_t num;          /**< Number of destination addresses */
    uint8_t flag;          /**< The flag of http chunks */
} mlink_connection_t;

static const char *TAG                 = "mlink_httpd";
static httpd_handle_t g_httpd_handle   = NULL;
static QueueHandle_t g_mlink_queue     = NULL;
static mlink_connection_t *g_conn_list = NULL;

static mdf_err_t mlink_get_mesh_info(httpd_req_t *req);
static esp_err_t mlink_device_request(httpd_req_t *req);


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

static int httpd_default_send(httpd_handle_t hd, int sockfd, const char *buf, size_t buf_len, int flags)
{
    MDF_PARAM_CHECK(buf);

    int ret = send(sockfd, buf, buf_len, flags);
    MDF_ERROR_CHECK(ret < 0, ret, "socket send, sockfd: %d, buf_len: %d", sockfd, buf_len);

    return ret;
}

static void mlink_connection_remove(mlink_connection_t *mlink_conn)
{
    if (mlink_conn) {
        xTimerStop(mlink_conn->timer, portMAX_DELAY);
        xTimerDelete(mlink_conn->timer, portMAX_DELAY);
        memset(mlink_conn, 0, sizeof(mlink_connection_t));
    }
}

static void mlink_connection_timeout_cb(void *timer)
{
    char *chunk_footer             = "0\r\n\r\n";
    mlink_connection_t *mlink_conn = (mlink_connection_t *)pvTimerGetTimerID(timer);

    if (mlink_conn->timer == NULL || mlink_conn->flag == MLINK_HTTPD_CHUNKS_NONE) {
        return ;
    }

    if (mlink_conn->flag != MLINK_HTTPD_CHUNKS_BODY) {
        chunk_footer = "HTTP/1.1 400 Bad Request\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: 59\r\n\r\n"
                       "{\"status_code\":-1,\"status_msg\":\"Destination address error\"}";
    }

    mlink_conn->flag = MLINK_HTTPD_CHUNKS_DATA;

    MDF_LOGW("Mlink httpd response timeout");

    if (httpd_default_send(mlink_conn->handle, mlink_conn->sockfd, chunk_footer, strlen(chunk_footer), 0) <= 0) {
        MDF_LOGW("<%s> httpd_default_send, sockfd: %d", strerror(errno), mlink_conn->sockfd);
    }

    mlink_connection_remove(mlink_conn);
}

static mlink_connection_t *mlink_connection_find(uint16_t sockfd)
{
    for (int i = 0; i < CONFIG_LWIP_MAX_SOCKETS; ++i) {
        if (g_conn_list[i].sockfd == sockfd && g_conn_list[i].flag != MLINK_HTTPD_CHUNKS_NONE) {
            return g_conn_list + i;
        }
    }

    MDF_LOGW("Mlink chunks is no find, sockfd: %d", sockfd);
    return NULL;
}

static mdf_err_t mlink_connection_add(httpd_req_t *req, uint16_t chunks_num)
{
    for (int i = 0; i < CONFIG_LWIP_MAX_SOCKETS; ++i) {
        if (g_conn_list[i].flag == MLINK_HTTPD_CHUNKS_NONE) {
            g_conn_list[i].num    = chunks_num;
            g_conn_list[i].flag   = (chunks_num > 1) ? MLINK_HTTPD_CHUNKS_HEADER : MLINK_HTTPD_CHUNKS_DATA;
            g_conn_list[i].handle = req->handle;
            g_conn_list[i].sockfd = httpd_req_to_sockfd(req);
            g_conn_list[i].timer  = xTimerCreate("chunk_timer", MLINK_HTTPD_RESP_TIMEROUT_MS / portTICK_RATE_MS,
                                                 false, g_conn_list + i, mlink_connection_timeout_cb);
            MDF_ERROR_CHECK(!g_conn_list[i].timer, MDF_FAIL, "xTimerCreate mlink_conn fail");
            xTimerStart(g_conn_list[i].timer, portMAX_DELAY);
            return MDF_OK;
        }
    }

    MDF_LOGW("Mlink chunks add");
    return MDF_FAIL;
}

static mdf_err_t mlink_get_mesh_info(httpd_req_t *req)
{
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

static esp_err_t mlink_httpd_resp_200(httpd_req_t *req)
{
    mdf_err_t ret           = MDF_FAIL;
    const char *status_code = "{\"status_code\":0,\"status_msg\":\"MDF_OK\"}";

    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    MDF_ERROR_CHECK(ret != MDF_OK, ret, "Set the HTTP content type");

    ret = httpd_resp_send(req, status_code, strlen(status_code));
    MDF_ERROR_CHECK(ret != MDF_OK, ret, "Send a complete HTTP response");

    return MDF_OK;
}

static esp_err_t mlink_device_request(httpd_req_t *req)
{
    esp_err_t ret               = MDF_FAIL;
    char *httpd_hdr_value       = NULL;
    ssize_t httpd_hdr_value_len = 0;
    mlink_httpd_t *httpd_data   = MDF_CALLOC(1, sizeof(mlink_httpd_t));

    httpd_hdr_value_len = mlink_httpd_get_hdr(req, "Content-Type", &httpd_hdr_value);
    MDF_ERROR_GOTO(httpd_hdr_value_len <= 0, EXIT, "Get 'Content-Type' from the request headers");

    if (!strcasecmp(HTTPD_TYPE_JSON, httpd_hdr_value)) {
        httpd_data->type.format = MLINK_HTTPD_FORMAT_JSON;
    } else if (!strcasecmp(HTTPD_TYPE_TEXT, httpd_hdr_value)) {
        httpd_data->type.format = MLINK_HTTPD_FORMAT_HTML;
    } else {
        httpd_data->type.format = MLINK_HTTPD_FORMAT_HEX;
    }

    MDF_FREE(httpd_hdr_value);

    httpd_data->type.from = MLINK_HTTPD_FROM_SERVER;
    httpd_data->type.resp = true;

    if (mlink_httpd_get_hdr(req, "Root-Response", &httpd_hdr_value) > 0) {
        if ((!strcmp(httpd_hdr_value, "1") || !strcasecmp(httpd_hdr_value, "true"))) {
            httpd_data->type.resp = false;
        }
    }

    MDF_FREE(httpd_hdr_value);

    httpd_hdr_value_len = mlink_httpd_get_hdr(req, "Mesh-Node-Mac", &httpd_hdr_value);

    if (httpd_hdr_value_len <= 0) {
        MDF_LOGW("Get 'Mesh-Node-Mac' from the request headers");
        mlink_httpd_resp(req, HTTPD_400, "Must contain the 'Mesh-Node-Mac' field");
        goto EXIT;
    }

    httpd_data->addrs_list = MDF_MALLOC(httpd_hdr_value_len / 2);

    for (char *tmp = httpd_hdr_value;; tmp++) {
        if (*tmp == ',' || *tmp == '\0') {
            mlink_mac_str2hex(tmp - 12, httpd_data->addrs_list + (httpd_data->addrs_num * MWIFI_ADDR_LEN));
            httpd_data->addrs_num++;

            if (*tmp == '\0') {
                break;
            }
        }
    }

    MDF_FREE(httpd_hdr_value);
    MDF_LOGD("dest_addrs_num: %d", httpd_data->addrs_num);

    if (httpd_data->addrs_num == 0) {
        MDF_LOGW("Destination address format error");
        mlink_httpd_resp(req, HTTPD_400, "Destination address format error");
        goto EXIT;
    }

    httpd_data->size = req->content_len;
    httpd_data->data = MDF_MALLOC(req->content_len);
	//httpd_data->data[httpd_data->size - 1] = 0;
    for (int i = 0, recv_size = 0; i < 5 && recv_size < req->content_len; ++i, recv_size += ret) {
        ret = httpd_req_recv(req, httpd_data->data + recv_size, req->content_len - recv_size);
        MDF_ERROR_CONTINUE(ret == HTTPD_SOCK_ERR_TIMEOUT, "<HTTPD_SOCK_ERR_TIMEOUT> Read content data from the HTTP request");
        MDF_ERROR_GOTO(ret <= 0, EXIT, "<%s> Read content data from the HTTP request",
                       mdf_err_to_name(ret));
    }
	//ESP_LOGW("mlink_httpd", "context: '%s'", httpd_data->data);
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        ret = httpd_resp_send_408(req);
        MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "Helper function for HTTP 408");
    }

    if (!httpd_data->type.resp) {
        mlink_httpd_resp_200(req);
    } else {
        httpd_data->type.sockfd = httpd_req_to_sockfd(req);

        if (httpd_data->addrs_num == 1
                && (MWIFI_ADDR_IS_ANY(httpd_data->addrs_list)
                    || MWIFI_ADDR_IS_BROADCAST(httpd_data->addrs_list))) {
            #if 0
            mlink_connection_add(req, esp_mesh_get_routing_table_size());
			#else
			MDF_LOGE("This is a bug!!!");
			#endif
        } else {
            mlink_connection_add(req, httpd_data->addrs_num);
        }
    }

    /**< If g_mlink_queue is full, delete the front item */
    mlink_httpd_t *q_data = NULL;

    if (!uxQueueSpacesAvailable(g_mlink_queue)
            && xQueueReceive(g_mlink_queue, &q_data, 0)) {
        MDF_LOGW("Mlink data queue is full, delete the front item");
        MDF_FREE(q_data->addrs_list);
        MDF_FREE(q_data->data);
        MDF_FREE(q_data);
    }
	
    if (xQueueSend(g_mlink_queue, &httpd_data, 0) == pdFALSE) {
        MDF_LOGW("xQueueSend failed");
        ret = ESP_FAIL;

        ret = httpd_resp_send_500(req);
        MDF_ERROR_GOTO(ret != MDF_OK, EXIT, "Helper function for HTTP 408");

        goto EXIT;
    }

    return ESP_OK;

EXIT:

    if (httpd_data) {
        MDF_FREE(httpd_data->addrs_list);
        MDF_FREE(httpd_data->data);
        MDF_FREE(httpd_data);
    }

    MDF_FREE(httpd_hdr_value);
    return ret;
}

static mdf_err_t mlink_httpd_resp_set_status(char **resp, const char *status)
{
    *resp = NULL;
    asprintf(resp, "HTTP/1.1 %s\r\n", status);

    return ESP_OK;
}

static mdf_err_t mlink_httpd_resp_set_hdr(char **resp, const char *field, const char *value)
{
    char *tmp_str   = NULL;
    size_t tmp_size = asprintf(&tmp_str, "%s: %s\r\n", field, value);

    *resp = MDF_REALLOC(*resp, strlen(*resp) + tmp_size + 1);
    memcpy(*resp + strlen(*resp), tmp_str, tmp_size + 1);

    MDF_FREE(tmp_str);

    return ESP_OK;
}

static size_t mlink_httpd_resp_set_data(char **resp, const char *data, size_t size)
{
    MDF_PARAM_CHECK(resp);

    char *tmp_str     = NULL;
    size_t tmp_size   = asprintf(&tmp_str, "Content-Length: %d\r\n\r\n", size);
    size_t total_size = strlen(*resp) + tmp_size + size;

    *resp = MDF_REALLOC(*resp, total_size + 8);
    memcpy(*resp + strlen(*resp), tmp_str, tmp_size + 1);

    if (data && size) {
        memcpy(*resp + strlen(*resp), data, size);
    }

    MDF_FREE(tmp_str);
    return total_size;
}

mdf_err_t mlink_httpd_write(const mlink_httpd_t *response, TickType_t wait_ticks)
{
    MDF_PARAM_CHECK(response);
    MDF_ERROR_CHECK(!g_httpd_handle, MDF_ERR_NOT_INIT, "mlink_httpd is stop");

    mdf_err_t ret    = MDF_FAIL;
    char mac_str[13] = {0};
    size_t resp_size = 0;
    char *resp_data  = NULL;

    /**
      * @brief For sending out data in response to an HTTP request.
      */
    mlink_connection_t *mlink_conn = mlink_connection_find(response->type.sockfd);
    MDF_ERROR_CHECK(mlink_conn == NULL, MDF_FAIL, "mlink_connection_find");

    /**
     * @brief Generate a packet for the http response
     */
    mlink_httpd_resp_set_status(&resp_data, response->type.resp == true ? HTTPD_200 : HTTPD_400);
    mlink_httpd_resp_set_hdr(&resp_data, "Content-Type",
                             response->type.format == MESH_PROTO_JSON ? HTTPD_TYPE_JSON : HTTPD_TYPE_TEXT);
    mlink_httpd_resp_set_hdr(&resp_data, "Mesh-Node-Mac", mlink_mac_hex2str(response->addrs_list, mac_str));
    resp_size = mlink_httpd_resp_set_data(&resp_data, response->data, response->size);

    if (wait_ticks != portMAX_DELAY) {
        int send_timeout_ms = wait_ticks * portTICK_PERIOD_MS;
        struct timeval timeout = {
            .tv_usec = (send_timeout_ms % 1000) * 1000,
            .tv_sec = send_timeout_ms / 1000,
        };

        ret = setsockopt(mlink_conn->sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        MDF_ERROR_GOTO(ret < 0, EXIT, "<%s> Set send timeout", strerror(errno));
    }

    if (mlink_conn->flag == MLINK_HTTPD_CHUNKS_HEADER) {
        char *chunk_header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/http\r\n"
            "Transfer-Encoding: chunked\r\n\r\n";

        MDF_LOGD("send chunk_header, sockfd: %d, chunk_header: %s", mlink_conn->sockfd, chunk_header);
        ret = httpd_default_send(mlink_conn->handle, mlink_conn->sockfd, chunk_header, strlen(chunk_header), 0);
        MDF_ERROR_GOTO(ret <= 0, EXIT, "This is the low level default send function of the HTTPD");

        mlink_conn->flag = MLINK_HTTPD_CHUNKS_BODY;
    }

    if (mlink_conn->flag == MLINK_HTTPD_CHUNKS_BODY) {
        char chunk_size[16] = {0};
        sprintf(chunk_size, "%x\r\n", resp_size);

        xTimerReset(mlink_conn->timer, portMAX_DELAY);

        MDF_LOGD("send chunk_size, sockfd: %d, data: %s", mlink_conn->sockfd, chunk_size);
        ret = httpd_default_send(mlink_conn->handle, mlink_conn->sockfd, chunk_size, strlen(chunk_size), 0);
        MDF_ERROR_GOTO(ret <= 0, EXIT, "<%s> This is the low level default send function of the HTTPD", strerror(errno));

        resp_data[resp_size++] = '\r';
        resp_data[resp_size++] = '\n';
    }

    resp_data[resp_size] = '\0';

    MDF_LOGD("size: %d, resp_data: %.*s", resp_size, resp_size, resp_data);
    ret = httpd_default_send(mlink_conn->handle, mlink_conn->sockfd, resp_data, resp_size, 0);
    MDF_ERROR_GOTO(ret <= 0, EXIT, "<%s> httpd_default_send, sockfd: %d",
                   strerror(errno), mlink_conn->sockfd);

    mlink_conn->num--;
    MDF_LOGD("mlink_conn->num: %d", mlink_conn->num);

    if (mlink_conn->num > 0) {
        ret = MDF_OK;
        goto EXIT;
    }

    if (mlink_conn->flag == MLINK_HTTPD_CHUNKS_BODY) {
        char *chunk_footer = "0\r\n\r\n";

        MDF_LOGD("send chunk_footer, sockfd: %d, data: %s", mlink_conn->sockfd, chunk_footer);
        ret = httpd_default_send(mlink_conn->handle, mlink_conn->sockfd, chunk_footer, strlen(chunk_footer), 0);
        MDF_ERROR_GOTO(ret <= 0, EXIT, "This is the low level default send function of the HTTPD");
    }

    mlink_connection_remove(mlink_conn);
    ret = MDF_OK;

EXIT:
    MDF_FREE(resp_data);
    return ret;
}

mdf_err_t mlink_httpd_read(mlink_httpd_t **request, TickType_t wait_ticks)
{
    MDF_PARAM_CHECK(request);

    mdf_err_t ret = MDF_OK;

    if (!g_mlink_queue) {
        g_mlink_queue = xQueueCreate(3, sizeof(mlink_httpd_t *));
    }

    ret = xQueueReceive(g_mlink_queue, request, wait_ticks);

    if (!*request) {
        mlink_httpd_t *q_data = NULL;

        while (xQueueReceive(g_mlink_queue, &q_data, 0)) {
            if (q_data) {
                MDF_FREE(q_data->addrs_list);
                MDF_FREE(q_data->data);
                MDF_FREE(q_data);
            }
        }

        vQueueDelete(g_mlink_queue);
        g_mlink_queue = NULL;

        MDF_LOGW("MLINK HTTPD EXIT");
        return MDF_ERR_NOT_SUPPORTED;
    }

    MDF_ERROR_CHECK(ret != true, MDF_ERR_TIMEOUT, "xQueueSend failed");

    return MDF_OK;
}

mdf_err_t mlink_httpd_start(void)
{
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

    if (!g_mlink_queue) {
        g_mlink_queue = xQueueCreate(3, sizeof(mlink_httpd_t *));
    }

    if (!g_conn_list) {
        g_conn_list = MDF_CALLOC(CONFIG_LWIP_MAX_SOCKETS, sizeof(mlink_connection_t));
    }

    ret = httpd_start(&g_httpd_handle, &config);
    MDF_ERROR_CHECK(ret != MDF_OK, ret, "Starts the web server");

    for (int i = 0, handlers_no = sizeof(basic_handlers) / sizeof(httpd_uri_t);
            i < handlers_no; i++) {
        ret = httpd_register_uri_handler(g_httpd_handle, basic_handlers + i);
        MDF_ERROR_CONTINUE(ret != ESP_OK, "Register uri failed for %d", i);
    }

    return MDF_OK;
}

mdf_err_t mlink_httpd_stop(void)
{
    if (!g_httpd_handle) {
        return MDF_OK;
    }

    mdf_err_t ret = MDF_OK;
    void *mlink_queue_exit = NULL;

    ret = httpd_stop(g_httpd_handle);
    MDF_ERROR_CHECK(ret != ESP_OK, ret, "Stops the web server");
    g_httpd_handle = NULL;
    xQueueSend(g_mlink_queue, &mlink_queue_exit, 0);

    return MDF_OK;
}
