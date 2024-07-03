#include "esc_hw4.h"

float current_offset_ = -1;

static void process(esc_hw4_parameters_t *parameter, int current_raw_offset, uint *current_raw);
float get_voltage(uint16_t voltage_raw, esc_hw4_parameters_t *parameter);
float get_temperature(uint16_t temperature_raw);
float get_current(uint raw, int offset, float multiplier);

void esc_hw4_task(void *parameters)
{
    esc_hw4_parameters_t parameter = *(esc_hw4_parameters_t *)parameters;
    *parameter.rpm = 0;
    *parameter.voltage = 0;
    *parameter.current = 0;
    *parameter.temperature_fet = 0;
    *parameter.temperature_bec = 0;
    *parameter.cell_voltage = 0;
    *parameter.consumption = 0;
    *parameter.cell_count = 1;
    xTaskNotifyGive(receiver_task_handle);
#ifdef SIM_SENSORS
    *parameter.rpm = 12345.67;
    *parameter.consumption = 123.4;
    *parameter.voltage = 12.34;
    *parameter.current = 5.678;
    *parameter.temperature_fet = 12.34;
    *parameter.temperature_bec = 23.45;
    *parameter.cell_voltage = 3.75;
#endif
    TaskHandle_t task_handle;
    uint cell_count_delay = 15000;
    cell_count_parameters_t cell_count_parameters = {cell_count_delay, parameter.voltage, parameter.cell_count};
    xTaskCreate(cell_count_task, "cell_count_task", STACK_CELL_COUNT, (void *)&cell_count_parameters, 1, &task_handle);
    xQueueSendToBack(tasks_queue_handle, task_handle, 0);

    int current_raw_offset = -1;
    uint current_raw = 0;
    if (parameter.current_is_manual_offset)
    {
        current_raw_offset = parameter.current_offset * 4096.0 / 3.3 / parameter.current_multiplier;
        if (current_raw_offset > ESC_HW4_ADC_RES)
            current_raw_offset = ESC_HW4_ADC_RES;
    }
    else
    {
        uint current_delay = 15000;
        auto_offset_int_parameters_t current_offset_parameters = {current_delay, &current_raw, &current_raw_offset};
        xTaskCreate(auto_offset_int_task, "esc_hw4_current_offset_task", STACK_AUTO_OFFSET, &current_offset_parameters, 1, &task_handle);
        xQueueSendToBack(tasks_queue_handle, task_handle, 0);
    }

    uart1_begin(19200, UART1_TX_GPIO, UART_ESC_RX, ESC_HW4_TIMEOUT_US, 8, 1, UART_PARITY_NONE, false);

    while (1)
    {
        ulTaskNotifyTakeIndexed(1, pdTRUE, portMAX_DELAY);
        process(&parameter, current_raw_offset, &current_raw);
    }
}

static void process(esc_hw4_parameters_t *parameter, int current_raw_offset, uint *current_raw)
{
    uint16_t pwm, throttle;
    static uint32_t timestamp = 0;
    uint8_t lenght = uart1_available();
    if (lenght == ESC_HW4_PACKET_LENGHT || lenght == ESC_HW4_PACKET_LENGHT + 1)
    {
        uint8_t data[ESC_HW4_PACKET_LENGHT];
        uart1_read_bytes(data, ESC_HW4_PACKET_LENGHT);
        throttle = (uint16_t)data[4] << 8 | data[5]; // 0-1024
        pwm = (uint16_t)data[6] << 8 | data[7];      // 0-1024
        float rpm = (uint32_t)data[8] << 16 | (uint16_t)data[9] << 8 | data[10];
        // try to filter invalid data frames
        if (throttle < 1024 &&
            pwm < 1024 &&
            rpm < 200000 &&
            data[11] <= 0xF &&
            data[13] <= 0xF &&
            data[15] <= 0xF &&
            data[17] <= 0xF)
        {
            *current_raw = (uint16_t)data[13] << 8 | data[14];
            float voltage = get_voltage((uint16_t)data[11] << 8 | data[12], parameter);
            float current = 0;
            if (throttle > parameter->current_thresold / 100.0 * 1024 && current_raw_offset != -1)
            {
                current = get_current(*current_raw, current_raw_offset, parameter->current_multiplier);
                if (current > parameter->current_max)
                    current = parameter->current_max;
            }
            float temperature_fet = get_temperature((uint16_t)data[15] << 8 | data[16]);
            float temperature_bec = get_temperature((uint16_t)data[17] << 8 | data[18]);
            rpm *= parameter->rpm_multiplier;
            if (parameter->pwm_out)
                xTaskNotifyGive(pwm_out_task_handle);
            *parameter->rpm = get_average(parameter->alpha_rpm, *parameter->rpm, rpm);
            if (current_raw_offset != -1)
                *parameter->consumption += get_consumption(*parameter->current, parameter->current_max, &timestamp);
            *parameter->voltage = get_average(parameter->alpha_voltage, *parameter->voltage, voltage);
            *parameter->current = get_average(parameter->alpha_current, *parameter->current, current);
            *parameter->temperature_fet = get_average(parameter->alpha_temperature, *parameter->temperature_fet, temperature_fet);
            *parameter->temperature_bec = get_average(parameter->alpha_temperature, *parameter->temperature_bec, temperature_bec);
            *parameter->cell_voltage = *parameter->voltage / *parameter->cell_count;
            if (debug)
            {
                uint32_t packet = (uint32_t)data[1] << 16 | (uint16_t)data[2] << 8 | data[3];
                printf("\nEsc HW4 (%u) < Packet: %i Rpm: %.0f Volt: %0.2f Curr: %.2f TempFet: %.0f TempBec: %.0f Cons: %.0f CellV: %.2f CRaw: %i CRawOffset: %i CurrMult: %.2f",
                       uxTaskGetStackHighWaterMark(NULL), packet, *parameter->rpm, *parameter->voltage, *parameter->current, *parameter->temperature_fet, *parameter->temperature_bec, *parameter->consumption, *parameter->cell_voltage, *current_raw, current_raw_offset, parameter->current_multiplier);
            }
        }
        else
        {
            if (debug)
            {
                printf("\nEsc HW4 packet error (%u): ", uxTaskGetStackHighWaterMark(NULL));
                for (uint i = 0; i < ESC_HW4_PACKET_LENGHT; i++)
                    printf("0x%X ", data[i]);
            }
        }
    }
}

float get_voltage(uint16_t voltage_raw, esc_hw4_parameters_t *parameter)
{
    return ESC_HW4_V_REF * voltage_raw / ESC_HW4_ADC_RES * parameter->divisor;
}

float get_temperature(uint16_t temperature_raw)
{
    float voltage = temperature_raw * ESC_HW4_V_REF / ESC_HW4_ADC_RES;
    float ntcR_Rref = (voltage * ESC_HW4_NTC_R1 / (ESC_HW4_V_REF - voltage)) / ESC_HW4_NTC_R_REF;
    if (ntcR_Rref < 0.001)
        return 0;
    float temperature = 1 / (log(ntcR_Rref) / ESC_HW4_NTC_BETA + 1 / 298.15) - 273.15;
    if (temperature < 0)
        return 0;
    return temperature;
}

float get_current(uint raw, int offset, float multiplier)
{
    // float current = (*parameter->current_raw - *parameter->current_offset) * ESC_HW4_V_REF / (parameter->ampgain * ESC_HW4_DIFFAMP_SHUNT * ESC_HW4_ADC_RES);
    if ((int)raw - offset < 0)
        return 0;
    return ((int)raw - offset) * ESC_HW4_V_REF / ESC_HW4_ADC_RES * multiplier;
}
