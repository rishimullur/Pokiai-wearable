#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/adc.h"
#include "driver/i2s.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wav_encoder.h"

#define MAX4466_ADC_PIN 34 // Replace with the actual ADC pin connected to MAX4466
#define ADC_CHANNEL ADC1_CHANNEL_6 // Replace with the ADC channel corresponding to the chosen pin
#define BUFFER_SIZE 512
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASS "your_wifi_password"
#define SERVER_IP "your_server_ip"
#define SERVER_PORT 12345

float audio_buffer[BUFFER_SIZE];

// Event group to signal when the Wi-Fi is connected
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI("wifi", "Connected to AP");
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI("wifi", "Disconnected from AP");
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    }
}

void wifi_init_sta() {
    wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI("wifi", "Wi-Fi initialized");
}

void setup_adc() {
    adc1_config_width(ADC_WIDTH_BIT_12); // Set ADC resolution to 12 bits
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_11); // Set attenuation to adjust input voltage range
}

void read_audio_data() {
    while (1) {
        int raw_value = adc1_get_raw(ADC_CHANNEL); // Read raw ADC value
        float scaled_value = (raw_value - 2048) / 2048.0; // Scale to -1.0 to 1.0

        // Store the scaled value in the audio buffer
        for (int i = 0; i < BUFFER_SIZE - 1; i++) {
            audio_buffer[i] = audio_buffer[i + 1];
        }
        audio_buffer[BUFFER_SIZE - 1] = scaled_value;

        // Additional processing or tasks can be performed here

        vTaskDelay(pdMS_TO_TICKS(20)); // Adjust the delay as needed
    }
}

void send_audio_data() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE("socket", "Unable to create socket");
        vTaskDelete(NULL);
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_address.sin_addr);

    while (1) {
        if (connect(sock, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
            ESP_LOGE("connect", "Connection failed");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);

        // Send audio_buffer data to the server
        send(sock, audio_buffer, BUFFER_SIZE * sizeof(float), 0);

        vTaskDelay(pdMS_TO_TICKS(100)); // Adjust the delay as needed
    }
}

extern "C" void app_main() {
    setup_adc();
    wifi_init_sta();

    xTaskCreate(read_audio_data, "read_audio_data", 4096, NULL, 5, NULL);
    xTaskCreate(send_audio_data, "send_audio_data", 4096, NULL, 5, NULL);

    // Your main application code can continue here
    while (1) {
        // Perform other tasks or leave it empty
        vTaskDelay(pdMS_TO_TICKS(1000)); // Adjust the delay as needed
    }
}