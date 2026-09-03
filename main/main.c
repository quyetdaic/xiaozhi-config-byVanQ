#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#define ESP_WIFI_SSID      "Xiaozhi-Config"
#define ESP_WIFI_PASS      "12345678"
#define ESP_WIFI_CHANNEL   1
#define MAX_STA_CONN       4

static const char *TAG = "xiaozhi_wifi";

// Khai báo nhúng file index.html từ thư mục build vào firmware
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

// Hàm lưu giá trị chân GPIO vào NVS
void save_pin_to_nvs(const char* key, int val) {
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_i32(handle, key, val);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Đã lưu %s = %d vào NVS", key, val);
    }
}

// Xử lý yêu cầu GET / (Gửi giao diện HTML ra trình duyệt)
static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

// Xử lý yêu cầu POST /save (Nhận cấu hình chân từ web gửi lên)
static esp_err_t save_post_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf) - 1));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Lỗi nhận dữ liệu");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Phân tách chuỗi query (ví dụ: i2s_ws=5&i2s_sck=6...)
    char *token = strtok(buf, "&");
    while (token != NULL) {
        char *eq = strchr(token, '=');
        if (eq != NULL) {
            *eq = '\0';
            char *key = token;
            char *val_str = eq + 1;
            int pin_val = atoi(val_str);
            
            // Lưu từng chân vào NVS
            save_pin_to_nvs(key, pin_val);
        }
        token = strtok(NULL, "&");
    }

    const char *resp = "Đã lưu cấu hình chân vào NVS thành công!";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// Khởi động HTTP Server
static httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = index_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t save_uri = {
            .uri      = "/save",
            .method   = HTTP_POST,
            .handler  = save_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &save_uri);
    }
    return server;
}

// Cấu hình Wi-Fi chế độ Access Point (Phát Wi-Fi)
static void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ESP_WIFI_SSID,
            .ssid_len = strlen(ESP_WIFI_SSID),
            .channel = ESP_WIFI_CHANNEL,
            .password = ESP_WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    if (strlen(ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP đã phát thành công. SSID:%s Password:%s", ESP_WIFI_SSID, ESP_WIFI_PASS);
}

void app_main(void) {
    // Khởi tạo NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Khởi động Wi-Fi AP
    wifi_init_softap();

    // Khởi động Web Server
    start_webserver();
}