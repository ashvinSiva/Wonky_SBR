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
#define CONTROL_PERIOD_MS 200
// To accurately calculate the axis
#define AVG 8
// Target angle which physically describes the SBR standing straight up
#define THETA_T 0.0f
// Definiton of g
#define g 9.81f
// Define Gain Constants
#define KP 1.0f
#define KI 1.0f
#define KD 1.0f

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
float filteredY = 0; 
float filteredZ = 0;
float alpha = 0.5; 
void read_accelerometer(int *y_val, int* z_val) {
    // Initially implemented the averageing method, then replaced it with low-pass filter method, 
    // Finally averaging the reading and use that reading into the low-pass filter formula

    // Reads raw 12-bit values [0,4095] from ADXL335
    int raw_y; 
    int raw_z;
    int y_tot = 0; 
    int z_tot = 0; 

    for (int i = 0; i < AVG; i++) {
        ESP_ERROR_CHECK( adc_oneshot_read(adc_handle, Y_PIN, &raw_y) );
        y_tot += raw_y; 
        ESP_ERROR_CHECK( adc_oneshot_read(adc_handle, Z_PIN, &raw_z) );
        z_tot += raw_z;
    }
    y_tot /= AVG; 
    z_tot /= AVG;

    if (filteredY != 0 && filteredZ != 0) {
        filteredY = alpha * y_tot + (1 - alpha) * filteredY;
        filteredZ = alpha * z_tot + (1 - alpha) * filteredZ;
    } else {
        filteredY = y_tot; 
        filteredZ = z_tot;
    }
    *y_val = filteredY; 
    *z_val = filteredZ;  

}

void update_motors(float u) {
    
    if (u > -40.0f && u < 0.0f) {
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
    
    int raw_y = 0; 
    int raw_z = 0;

    float integral = 0.0f;
    float derivative = 0.0f; 
    float prev_e = 0.0f; 
    int64_t prev_time = esp_timer_get_time(); 

    while(1) {
        printf("----------------------------------------------------------------------------------------\n");
        read_accelerometer(&raw_y, &raw_z); 
        int y_axis = 0; 
        int z_axis = 0;
        float theta_m = 0;

        if ((2719 <= raw_y && raw_y <= 2723) && (3488 <= raw_z && raw_z <= 3491)) {

            y_axis = 0; 
            z_axis = 0; 
            theta_m = 0; 

        } else {

            y_axis = (raw_y - 2722); //  0g horizontal forward
            z_axis = (raw_z - 3489); //  1g vertical upward
            theta_m = atan2((double)z_axis, (double)y_axis);
            theta_m *= (180/M_PI); // convert from radians to degrees
            
        }

        printf("theta_m: %.2f | raw_y: %d | raw_z: %d\n", theta_m, raw_y, raw_z);

        float e = THETA_T - theta_m; 
        
        int64_t curr_time = esp_timer_get_time(); // returns time in microseconds

        float delta_t = (curr_time - prev_time)/1000000.0f;  
        integral = e * delta_t; 
        if (delta_t > 0.0f) 
            derivative = (e-prev_e)/delta_t;

        float u = (KP * e) + (KI * integral) + (KD * derivative); // so many confusing fixes 

        printf("y: %d | z: %d | error: %.2f | u: %.2f\n", y_axis, z_axis, e, u); 

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