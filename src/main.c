#include "main.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#define BANK_COUNT 6
#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_POLL_MS 10
#define LED_TEST_MS 1000
#define BANK_INDICATOR_BLINK_MS 100
#define BANK_INDICATOR_PAUSE_MS 1000
#define TEMP_READ_INTERVAL_US 1000000

#define I2C_MASTER_PORT I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define TEMP_SENSOR_ADDR 0x48
#define TEMP_SENSOR_TEMP_REG 0x00

#define SETTINGS_NAMESPACE "settings"
#define ENABLED_BANK_COUNT_KEY "bank_count"

static const char *TAG = "ir_blaster";

static const gpio_num_t bank_pins[BANK_COUNT] = {
    PIN_BANK1,
    PIN_BANK2,
    PIN_BANK3,
    PIN_BANK4,
    PIN_BANK5,
    PIN_BANK6,
};

static void set_enabled_bank_count(int enabled_bank_count)
{
    for (int i = 0; i < BANK_COUNT; ++i) {
        ESP_ERROR_CHECK(gpio_set_level(bank_pins[i], i < enabled_bank_count));
    }
}

static void init_settings_storage(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);
}

static int load_enabled_bank_count(void)
{
    nvs_handle_t settings_handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &settings_handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    }

    ESP_ERROR_CHECK(err);

    uint8_t enabled_bank_count = 0;
    err = nvs_get_u8(settings_handle, ENABLED_BANK_COUNT_KEY, &enabled_bank_count);
    nvs_close(settings_handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    }

    ESP_ERROR_CHECK(err);

    if (enabled_bank_count > BANK_COUNT) {
        return 0;
    }

    return enabled_bank_count;
}

static void save_enabled_bank_count(int enabled_bank_count)
{
    nvs_handle_t settings_handle;

    ESP_ERROR_CHECK(nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &settings_handle));
    ESP_ERROR_CHECK(nvs_set_u8(settings_handle, ENABLED_BANK_COUNT_KEY, enabled_bank_count));
    ESP_ERROR_CHECK(nvs_commit(settings_handle));
    nvs_close(settings_handle);
}

static void init_bank_outputs(void)
{
    uint64_t bank_pin_mask = 0;

    for (int i = 0; i < BANK_COUNT; ++i) {
        bank_pin_mask |= 1ULL << bank_pins[i];
    }

    gpio_config_t bank_config = {
        .pin_bit_mask = bank_pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&bank_config));
    set_enabled_bank_count(0);
}

static void init_indicator_led_outputs(void)
{
    gpio_config_t led_config = {
        .pin_bit_mask = (1ULL << PIN_LED_LEFT) | (1ULL << PIN_LED_RIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&led_config));
    ESP_ERROR_CHECK(gpio_set_level(PIN_LED_LEFT, 0));
    ESP_ERROR_CHECK(gpio_set_level(PIN_LED_RIGHT, 0));
}

static void test_indicator_leds(void)
{
    ESP_ERROR_CHECK(gpio_set_level(PIN_LED_LEFT, 1));
    ESP_ERROR_CHECK(gpio_set_level(PIN_LED_RIGHT, 1));
    vTaskDelay(pdMS_TO_TICKS(LED_TEST_MS));
    ESP_ERROR_CHECK(gpio_set_level(PIN_LED_LEFT, 0));
    ESP_ERROR_CHECK(gpio_set_level(PIN_LED_RIGHT, 0));
}

static void init_boot_button(void)
{
    gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << PIN_BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&button_config));
}

static void init_i2c(void)
{
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
        .clk_flags = 0,
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_PORT, &i2c_config));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_PORT, i2c_config.mode, 0, 0, 0));
}

static esp_err_t read_temperature_c(float *temperature_c)
{
    uint8_t register_address = TEMP_SENSOR_TEMP_REG;
    uint8_t temperature_data[2] = {0};

    esp_err_t err = i2c_master_write_read_device(
        I2C_MASTER_PORT,
        TEMP_SENSOR_ADDR,
        &register_address,
        sizeof(register_address),
        temperature_data,
        sizeof(temperature_data),
        pdMS_TO_TICKS(100));

    if (err != ESP_OK) {
        return err;
    }

    int16_t raw_temperature = ((int16_t)temperature_data[0] << 4) | (temperature_data[1] >> 4);

    if (raw_temperature & 0x0800) {
        raw_temperature |= 0xF000;
    }

    *temperature_c = raw_temperature * 0.0625f;
    return ESP_OK;
}

static void service_bank_indicator(int enabled_bank_count, int64_t now_us)
{
    static int indicated_bank_count = -1;
    static int blink_count = 0;
    static bool led_is_on = false;
    static int64_t next_change_us = 0;

    if (enabled_bank_count <= 0) {
        indicated_bank_count = enabled_bank_count;
        blink_count = 0;
        led_is_on = false;
        next_change_us = 0;
        ESP_ERROR_CHECK(gpio_set_level(PIN_LED_LEFT, 0));
        return;
    }

    if (enabled_bank_count != indicated_bank_count) {
        indicated_bank_count = enabled_bank_count;
        blink_count = 0;
        led_is_on = true;
        next_change_us = now_us + (BANK_INDICATOR_BLINK_MS * 1000);
        ESP_ERROR_CHECK(gpio_set_level(PIN_LED_LEFT, 1));
        return;
    }

    if (now_us < next_change_us) {
        return;
    }

    if (led_is_on) {
        led_is_on = false;
        ++blink_count;
        ESP_ERROR_CHECK(gpio_set_level(PIN_LED_LEFT, 0));

        if (blink_count >= enabled_bank_count) {
            blink_count = 0;
            next_change_us = now_us + (BANK_INDICATOR_PAUSE_MS * 1000);
        } else {
            next_change_us = now_us + (BANK_INDICATOR_BLINK_MS * 1000);
        }

        return;
    }

    led_is_on = true;
    ESP_ERROR_CHECK(gpio_set_level(PIN_LED_LEFT, 1));
    next_change_us = now_us + (BANK_INDICATOR_BLINK_MS * 1000);
}

void app_main(void)
{
    init_settings_storage();
    init_bank_outputs();
    init_indicator_led_outputs();
    init_boot_button();
    init_i2c();
    test_indicator_leds();

    int enabled_bank_count = load_enabled_bank_count();
    set_enabled_bank_count(enabled_bank_count);

    int64_t next_temperature_read_us = esp_timer_get_time();

    while (true) {
        int64_t now_us = esp_timer_get_time();

        service_bank_indicator(enabled_bank_count, now_us);

        if (now_us >= next_temperature_read_us) {
            float temperature_c = 0.0f;
            esp_err_t err = read_temperature_c(&temperature_c);

            if (err == ESP_OK) {
                printf("Temperature: %.2f C\n", temperature_c);
            } else {
                ESP_LOGW(TAG, "temperature read failed: %s", esp_err_to_name(err));
            }

            next_temperature_read_us = now_us + TEMP_READ_INTERVAL_US;
        }

        if (gpio_get_level(PIN_BOOT_BUTTON) == 0) {
            vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));

            if (gpio_get_level(PIN_BOOT_BUTTON) == 0) {
                enabled_bank_count = (enabled_bank_count + 1) % (BANK_COUNT + 1);
                set_enabled_bank_count(enabled_bank_count);
                save_enabled_bank_count(enabled_bank_count);

                while (gpio_get_level(PIN_BOOT_BUTTON) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}
