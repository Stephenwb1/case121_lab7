/* HTTP GET Example using plain POSIX sockets

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include <string.h>

#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "sdkconfig.h"

/* Constants that aren't configurable in menuconfig */

#define WEATHER_SERVER "wttr.in"
#define WEATHER_PORT "80"
#define WEATHER_PATH "/Santa_Cruz?m&format=3"
static const char *TAG_WEATHER = "WTTR";

static const char *REQUEST = "GET " WEATHER_PATH " HTTP/1.0\r\n"
                             "Host: " WEATHER_SERVER ":" WEATHER_PORT "\r\n"
                             "User-Agent: esp-idf/1.0 esp32 curl\r\n"
                             "\r\n";

#define WEB_SERVER "10.0.0.169"
#define WEB_PORT "8000"
#define WEB_PATH "/"

static const char *TAG = "PHONE";

static const char *REQUEST_TEMPLATE = "POST " WEB_PATH " HTTP/1.0\r\n"
                                      "Host: " WEB_SERVER ":" WEB_PORT "\r\n"
                                      "User-Agent: esp-idf/1.0 esp32\r\n"
                                      "Content-Type: text/plain\r\n"
                                      "Content-Length: %d\r\n"
                                      "\r\n"
                                      "%s";

// send POST to phone
static void http_post_task(void *pvParameters) {
  const struct addrinfo hints = {
      .ai_family = AF_INET,
      .ai_socktype = SOCK_STREAM,
  };
  struct addrinfo *res;
  struct in_addr *addr;
  int s, r;
  char recv_buf[64];

  while (1) {
    //FOR GET REQUEST FROM WEATHER
    int err = getaddrinfo(WEATHER_SERVER, WEATHER_PORT, &hints, &res);

    if (err != 0 || res == NULL) {
      ESP_LOGE(TAG_WEATHER, "DNS lookup failed err=%d res=%p", err, res);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }

    /* Code to print the resolved IP.

       Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code
     */
    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGI(TAG_WEATHER, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

    s = socket(res->ai_family, res->ai_socktype, 0);
    if (s < 0) {
      ESP_LOGE(TAG_WEATHER, "... Failed to allocate socket.");
      freeaddrinfo(res);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }
    ESP_LOGI(TAG_WEATHER, "... allocated socket");

    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
      ESP_LOGE(TAG_WEATHER, "... socket connect failed errno=%d", errno);
      close(s);
      freeaddrinfo(res);
      vTaskDelay(4000 / portTICK_PERIOD_MS);
      continue;
    }

    ESP_LOGI(TAG_WEATHER, "... connected");
    freeaddrinfo(res);

    if (write(s, REQUEST, strlen(REQUEST)) < 0) {
      ESP_LOGE(TAG_WEATHER, "... socket send failed");
      close(s);
      vTaskDelay(4000 / portTICK_PERIOD_MS);
      continue;
    }
    ESP_LOGI(TAG_WEATHER, "... socket send success");

    struct timeval receiving_timeout;
    receiving_timeout.tv_sec = 5;
    receiving_timeout.tv_usec = 0;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                   sizeof(receiving_timeout)) < 0) {
      ESP_LOGE(TAG_WEATHER, "... failed to set socket receiving timeout");
      close(s);
      vTaskDelay(4000 / portTICK_PERIOD_MS);
      continue;
    }
    ESP_LOGI(TAG_WEATHER, "... set socket receiving timeout success");

    char POST_PAYLOAD[1024] = {0};
    /* Read HTTP response */
    do {
      bzero(recv_buf, sizeof(recv_buf));
      r = read(s, recv_buf, sizeof(recv_buf) - 1);
      if (r > 0) {
        strncat(POST_PAYLOAD, recv_buf, r);
      }
    } while (r > 0);

    ESP_LOGI(TAG_WEATHER, "... done reading from socket. Last read return=%d errno=%d.",
             r, errno);
    close(s);
    //
    // =====================================================
    // =====================================================
    // =====================================================
    // FOR PHONE SERVER POST
    //
    int err_phone = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

    if (err_phone != 0 || res == NULL) {
      ESP_LOGE(TAG, "DNS lookup failed err_phone=%d res=%p", err_phone, res);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }

    /* Code to print the resolved IP.

       Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code
     */
    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

    s = socket(res->ai_family, res->ai_socktype, 0);
    if (s < 0) {
      ESP_LOGE(TAG, "... Failed to allocate socket.");
      freeaddrinfo(res);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }
    ESP_LOGI(TAG, "... allocated socket");

    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
      ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
      close(s);
      freeaddrinfo(res);
      vTaskDelay(4000 / portTICK_PERIOD_MS);
      continue;
    }

    ESP_LOGI(TAG, "... connected");
    freeaddrinfo(res);

    // added for POSTING
    char request[512];
    int request_len = snprintf(request, sizeof(request), REQUEST_TEMPLATE,
                               strlen(POST_PAYLOAD), POST_PAYLOAD);

    if (write(s, request, request_len) < 0) {
      ESP_LOGE(TAG, "... socket send failed");
      close(s);
      vTaskDelay(4000 / portTICK_PERIOD_MS);
      continue;
    }
    ESP_LOGI(TAG, "... socket send success");

    struct timeval receiving_timeout_phone;
    receiving_timeout_phone.tv_sec = 5;
    receiving_timeout_phone.tv_usec = 0;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout_phone,
                   sizeof(receiving_timeout_phone)) < 0) {
      ESP_LOGE(TAG, "... failed to set socket receiving timeout");
      close(s);
      vTaskDelay(4000 / portTICK_PERIOD_MS);
      continue;
    }
    ESP_LOGI(TAG, "... set socket receiving timeout success");

    /* Read HTTP response */
    do {
      bzero(recv_buf, sizeof(recv_buf));
      r = read(s, recv_buf, sizeof(recv_buf) - 1);
      for (int i = 0; i < r; i++) {
        putchar(recv_buf[i]);
      }
    } while (r > 0);

    ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d.",
             r, errno);
    close(s);
    for (int countdown = 10; countdown >= 0; countdown--) {
      ESP_LOGI(TAG, "%d... ", countdown);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "Starting again!");
  }
}

void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  /* This helper function configures Wi-Fi or Ethernet, as selected in
   * menuconfig. Read "Establishing Wi-Fi or Ethernet Connection" section in
   * examples/protocols/README.md for more information about this function.
   */
  ESP_ERROR_CHECK(example_connect());

  xTaskCreate(&http_post_task, "http_post_task", 4096, NULL, 5, NULL);
}
