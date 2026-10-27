/*
 * Notes:
 * Peripherals: hardware component that performs a specific function alongside a CPU
 * - examples: Interal Peripherals - GPIO, ADC, UART, LEDC | External Peripherals - Temperature sensor, Accelerometer, OLED Display, LCD Display
 * 
 * 
 */

#include <stdio.h>
#include <stdint.h> 
#include <stdbool.h> 
#include <math.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_timer.h"

// Defining Motor Pins on Motor Driver
#define LM1 GPIO_NUM_25
#define LM2 GPIO_NUM_26
#define RM3 GPIO_NUM_14
#define RM4 GPIO_NUM_12

// Defining Accelerometer Pins
#define X_PIN ADC_CHANNEL_6
#define Y_PIN ADC_CHANNEL_3
#define Z_PIN ADC_CHANNEL_0

// To accurately calculate the axis
#define AVG 100

// Define Control Period
#define CONTROL_PERIOD_MS 10
#define THETA_T 0
#define g 9.81

//Global Variables
adc_oneshot_unit_handle_t adc_handle;

// Initialize
void init_motors() {
    gpio_reset_pin(LM1); 
    gpio_set_direction(LM1, GPIO_MODE_OUTPUT);
    gpio_reset_pin(LM2); 
    gpio_set_direction(LM2, GPIO_MODE_OUTPUT);
    gpio_reset_pin(RM3); 
    gpio_set_direction(RM3, GPIO_MODE_OUTPUT);
    gpio_reset_pin(RM4); 
    gpio_set_direction(RM4, GPIO_MODE_OUTPUT);
}

void init_adc() {

    // Config and Create ADC unit 1
    // Fixed bug: Was of adc_oneshot_unit_handlea_t type, but it didn't recognize, so i searched for ESP-IDF documentation, scrolled to 
    // Create an ADC Unit Handle Under Normal Oneshot Mode, figured it was of wrong, type, so it was replaced to adc_oneshot_unit_init_cfg_t
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };

    ESP_ERROR_CHECK( adc_oneshot_new_unit(&init_config, &adc_handle) );

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    // Configure each Channel 
    // ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, X_PIN, &channel_config)); for now ignore x-axis until we measure tilt side ways
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, Y_PIN, &channel_config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, Z_PIN, &channel_config));
}

// Functions
void read_accelerometer(float *y_val, float* z_val) {
    // Reads raw 12-bit values [0,4095] from ADXL335
    int raw_y; 
    int raw_z;
    float y_axis = 0.0; 
    float z_axis = 0.0;
    for (int i = 0; i < AVG; i++) {
        ESP_ERROR_CHECK( adc_oneshot_read(adc_handle, Y_PIN, &raw_y) ); 
        y_axis += raw_y;
        ESP_ERROR_CHECK( adc_oneshot_read(adc_handle, Z_PIN, &raw_z) ); 
        z_axis += raw_z;
    }  
    *y_val = y_axis/AVG; 
    *z_val = z_axis/AVG; 
}

void update_motors(float correction) {

}

void control_task(void* pvParameters) {
    
    float raw_y = 0; 
    float raw_z = 0;

    while(1) {

        read_accelerometer(&raw_y, &raw_z); 
        float y_axis = (float)(raw_y - 2048); 
        float z_axis = (float)(raw_z - 2048); 
    
        float theta_m = atan2(z_axis, y_axis);
        float e = THETA_T - theta_m; 




        vTaskDelay( pdMS_TO_TICKS(CONTROL_PERIOD_MS) );
    }

} 

void app_main(void) {

    // Program Setup
    init_motors();
    init_adc();

    // Program Loop
    xTaskCreate(control_task, "control task", 4096, NULL, 5, NULL); 
}

