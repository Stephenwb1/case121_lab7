/* HTTP GET Example using plain POSIX sockets

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "driver/i2c_master.h"
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

#define I2C_MASTER_SCL_IO 8
#define I2C_MASTER_SDA_IO 10
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000
#define I2C_MASTER_TIMEOUT_MS 1000
#define TEMP_SENSOR_ADDR 0x70
#define SHTC3_MEASURE_CMD 0x7CA2

// Ultrasonic pins

#define TRIG_PIN GPIO_NUM_3
#define ECHO_PIN GPIO_NUM_2

i2c_master_bus_handle_t bus_handle;
i2c_master_dev_handle_t dev_handle;

// I2C initialization

static void i2c_master_init(void) {

  i2c_master_bus_config_t bus_config = {
      .i2c_port = I2C_MASTER_NUM,
      .sda_io_num = I2C_MASTER_SDA_IO,
      .scl_io_num = I2C_MASTER_SCL_IO,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,

  };

  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));
  i2c_device_config_t dev_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = TEMP_SENSOR_ADDR,
      .scl_speed_hz = I2C_MASTER_FREQ_HZ,
  };

  ESP_ERROR_CHECK(
      i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));
}

// Read temperature from SHTC3

esp_err_t read_temperature_celsius(float *temp_celsius) {
  ESP_ERROR_CHECK(i2c_master_transmit(
      dev_handle,
      (uint8_t[]){(SHTC3_MEASURE_CMD >> 8), (SHTC3_MEASURE_CMD & 0xFF)}, 2,
      I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS));

  vTaskDelay(pdMS_TO_TICKS(20));
  uint8_t data[6] = {0};
  esp_err_t err =
      i2c_master_receive(dev_handle, data, sizeof(data),
                         I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
  if (err != ESP_OK)
    return err;

  uint16_t raw_temp = (data[0] << 8) | data[1];
  *temp_celsius = -45 + 175 * ((float)raw_temp / 65535);

  return ESP_OK;
}

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
  i2c_master_init();

  while (1) {
    // READ TEMPERATURE AND HUMIDITY SENSOR
    float temp_c = 0;
    if (read_temperature_celsius(&temp_c) != ESP_OK) {
      ESP_LOGE(TAG, "Temperature read failed");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    ESP_LOGI(TAG, "Local temperature is %.2fC", temp_c);

    vTaskDelay(pdMS_TO_TICKS(1000));

    // END OF SHTC3 SENSOR
    //=============================
    //  FOR GET REQUEST FROM WEATHER
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
        
    //add local weather data
    char local[64];
    snprintf(local, sizeof(local), "Local temperature is %.2fC", temp_c);
    strncat(POST_PAYLOAD, local, sizeof(POST_PAYLOAD) - strlen(POST_PAYLOAD) - 1);
    
    ESP_LOGI(TAG, "Local temperature is %.2fC", temp_c);

    ESP_LOGI(TAG_WEATHER,
             "... done reading from socket. Last read return=%d errno=%d.", r,
             errno);
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
