/*
 * Notes:
 * Peripherals: hardware component that performs a specific function alongside a CPU
 * - examples: Interal Peripherals - GPIO, ADC, UART, LEDC | External Peripherals - Temperature sensor, Accelerometer, OLED Display, LCD Display
 * 
 * 
 */

 // C Libraries
#include <stdio.h>
#include <stdint.h> 
#include <stdbool.h> 
#include <math.h>

// ESP Libraries
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_timer.h"

// Defining Motor Pins based on Motor Driver Pins
#define LM1 GPIO_NUM_25
#define LM2 GPIO_NUM_26
#define RM3 GPIO_NUM_14
#define RM4 GPIO_NUM_13

#define LM1_PWM_CHANNEL LEDC_CHANNEL_0
#define RM3_PWM_CHANNEL LEDC_CHANNEL_1

// Defining Accelerometer Channel Pins
#define X_PIN ADC_CHANNEL_6
#define Y_PIN ADC_CHANNEL_3
#define Z_PIN ADC_CHANNEL_0

// Define Control Period
#define CONTROL_PERIOD_MS 10
// To accurately calculate the axis
#define AVG 100
// Target angle which physically describes the SBR standing straight up
#define THETA_T 0
// Definiton of g
#define g 9.81f
// Define Gain Constants
#define KP 1
#define KI 0
#define KD 0

#define PWM_TIMER LEDC_TIMER_0
#define PWM_MODE LEDC_LOW_SPEED_MODE
#define PWM_FREQ 5000 //5KHz Frequency
#define PWM_RES LEDC_TIMER_10_BIT // 10-Bit resolution [0,1023]
#define PWM_MAX 1023



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
    /*
    - For now, ignore reading x-axis until we measure the tilt SIDE WAYS
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, X_PIN, &channel_config)); 
    */
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, Y_PIN, &channel_config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, Z_PIN, &channel_config));
}

void init_pwm() {

    ledc_timer_config_t ledc_timer = {
        .speed_mode = PWM_MODE, 
        .timer_num = PWM_TIMER, 
        .duty_resolution = PWM_RES,
        .freq_hz = PWM_FREQ, 
        .clk_cfg = LEDC_AUTO_CLK
    }; 
    ESP_ERROR_CHECK( ledc_timer_config(&ledc_timer) );

    ledc_channel_config_t left = {
        .gpio_num = LM1, 
        .speed_mode = PWM_MODE, 
        .channel = LM1_PWM_CHANNEL, 
        .timer_sel = PWM_TIMER, 
        .duty = 0, 
        .hpoint = 0
    };  
    ESP_ERROR_CHECK( ledc_channel_config(&left) );

    ledc_channel_config_t right = {
        .gpio_num = RM3, 
        .speed_mode = PWM_MODE, 
        .channel = RM3_PWM_CHANNEL, 
        .timer_sel = PWM_TIMER, 
        .duty = 0, 
        .hpoint = 0
    };  
    ESP_ERROR_CHECK( ledc_channel_config(&right) );

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

void update_motors(float u) {
    
    if (u < 0.0f) {
        gpio_set_level(LM1, 1);
        gpio_set_level(LM2, 0); 
        gpio_set_level(RM3, 1); 
        gpio_set_level(RM4, 0); 
        
    } else if(u > 0.0f) {
        gpio_set_level(LM1, 0);
        gpio_set_level(LM2, 1); 
        gpio_set_level(RM3, 0); 
        gpio_set_level(RM4, 1); 

    } else {
        gpio_set_level(LM1, 0);
        gpio_set_level(LM2, 0); 
        gpio_set_level(RM3, 0); 
        gpio_set_level(RM4, 0);

    }

}

void set_motor_pwm() {

}

void control_task(void* pvParameters) {
    
    float raw_y = 0; 
    float raw_z = 0;

    float integral = 0.0f;
    float derivative = 0.0f; 
    float prev_e = 0.0f; 
    int64_t prev_time = esp_timer_get_time(); 

    while(1) {

        read_accelerometer(&raw_y, &raw_z); 
        float y_axis = (float)(raw_y - 2048); 
        float z_axis = (float)(raw_z - 2048); 
        float theta_m = atan2(z_axis, y_axis);

        float e = THETA_T - theta_m; 
        
        int64_t curr_time = esp_timer_get_time(); 

        float delta_t = curr_time - prev_time;  
        integral += e * delta_t; 
        if (delta_t > 0.0f) 
            derivative = (e-prev_e)/delta_t;

        float u = e*KP + integral*KP + derivative*KD; 

        update_motors(u);

        prev_e = e; 
        prev_time = curr_time; 
        vTaskDelay( pdMS_TO_TICKS(CONTROL_PERIOD_MS) );
    }

} 

void app_main(void) {

    // Program Setup
    init_motors();
    init_pwm();
    init_adc();

    // Program Loop
    xTaskCreate(control_task, "control task", 4096, NULL, 5, NULL); 
}