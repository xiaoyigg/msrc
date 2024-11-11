#include "hott.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "airspeed.h"
#include "bmp180.h"
#include "bmp280.h"
#include "common.h"
#include "config.h"
#include "current.h"
#include "esc_apd_f.h"
#include "esc_apd_hv.h"
#include "esc_castle.h"
#include "esc_hw3.h"
#include "esc_hw4.h"
#include "esc_hw5.h"
#include "esc_kontronik.h"
#include "esc_pwm.h"
#include "ibus.h"
#include "ms5611.h"
#include "nmea.h"
#include "ntc.h"
#include "pwm_out.h"
#include "uart.h"
#include "uart_pio.h"
#include "voltage.h"

#define swap_16(value) (((value & 0xFF) << 8) | (value & 0xFF00) >> 8)

#define HOTT_VARIO_MODULE_ID 0x89
#define HOTT_GPS_MODULE_ID 0x8A
#define HOTT_ESC_MODULE_ID 0x8C
#define HOTT_GENERAL_AIR_MODULE_ID 0x8D
#define HOTT_ELECTRIC_AIR_MODULE_ID 0x8E

#define HOTT_VARIO_SENSOR_ID 0x90
#define HOTT_GPS_SENSOR_ID 0xA0
#define HOTT_GENERAL_AIR_SENSOR_ID 0xD0
#define HOTT_ELECTRIC_AIR_SENSOR_ID 0xE0

#define HOTT_TEXT_MODE_REQUEST_GPS 0xAF
#define HOTT_TEXT_MODE_REQUEST_GENERAL_AIR 0xDF
#define HOTT_TEXT_MODE_REQUEST_ELECTRIC_AIR 0xEF

#define HOTT_BINARY_MODE_REQUEST_ID 0x80
#define HOTT_TEXT_MODE_REQUEST_ID 0x7F

#define HOTT_TIMEOUT_US 1000
#define HOTT_PACKET_LENGHT 2

#define HOTT_START_BYTE 0x7C
#define HOTT_END_BYTE 0x7D

// TYPE
#define HOTT_TYPE_VARIO 0
#define HOTT_TYPE_ESC 1
#define HOTT_TYPE_ELECTRIC 2
#define HOTT_TYPE_GPS 3

// VARIO
#define HOTT_VARIO_ALTITUDE 0
#define HOTT_VARIO_M1S 1
#define HOTT_VARIO_M3S 2
#define HOTT_VARIO_M10S 3

//  ESC
#define HOTT_ESC_VOLTAGE 4
#define HOTT_ESC_CAPACITY 5
#define HOTT_ESC_TEMPERATURE 6
#define HOTT_ESC_CURRENT 7
#define HOTT_ESC_RPM 8
#define HOTT_ESC_THROTTLE 9  // 0-100%
#define HOTT_ESC_SPEED 10
#define HOTT_ESC_BEC_VOLTAGE 11
#define HOTT_ESC_BEC_CURRENT 12
#define HOTT_ESC_BEC_TEMPERATURE 13
#define HOTT_ESC_EXT_TEMPERATURE 14

// ELECTRIC (BATTERY)
#define HOTT_ELECTRIC_EXT_TEMPERATURE 15
#define HOTT_ELECTRIC_CELL_BAT_1_VOLTAGE 16
#define HOTT_ELECTRIC_CELL_BAT_2_VOLTAGE 17
#define HOTT_ELECTRIC_BAT_1_VOLTAGE 18
#define HOTT_ELECTRIC_BAT_2_VOLTAGE 19
#define HOTT_ELECTRIC_TEMPERATURE_1 20
#define HOTT_ELECTRIC_TEMPERATURE_2 21
#define HOTT_ELECTRIC_HEIGHT 22
#define HOTT_ELECTRIC_CURRENT 23
#define HOTT_ELECTRIC_CAPACITY 24
#define HOTT_ELECTRIC_M2S 25
#define HOTT_ELECTRIC_M3S 26
#define HOTT_ELECTRIC_RPM 27
#define HOTT_ELECTRIC_MINUTES 28
#define HOTT_ELECTRIC_SECONDS 29
#define HOTT_ELECTRIC_SPEED 30

// GPS
#define HOTT_GPS_DIRECTION 31
#define HOTT_GPS_SPEED 32
#define HOTT_GPS_LATITUDE 33
#define HOTT_GPS_LONGITUDE 34
#define HOTT_GPS_DISTANCE 35
#define HOTT_GPS_ALTITUDE 36
#define HOTT_GPS_CLIMBRATE 37
#define HOTT_GPS_CLIMBRATE3S 38
#define HOTT_GPS_SATS 39

// GENERAL (FUEL) - not used by msrc
#define HOTT_GENERAL_CELL 40
#define HOTT_GENERAL_BATTERY_1 41
#define HOTT_GENERAL_BATTERY_2 42
#define HOTT_GENERAL_TEMP_1 43
#define HOTT_GENERAL_TEMP_2 44
#define HOTT_GENERAL_RPM_1 45
#define HOTT_GENERAL_ALTITUDE 46
#define HOTT_GENERAL_CLIMBRATE 47
#define HOTT_GENERAL_CLIMBRATE3S 48
#define HOTT_GENERAL_CURRENT 49
#define HOTT_GENERAL_VOLTAGE 50
#define HOTT_GENERAL_CAPACITY 51
#define HOTT_GENERAL_SPEED 52
#define HOTT_GENERAL_RPM_2 53
#define HOTT_GENERAL_PRESSURE 54

typedef struct hott_sensor_vario_t {
    uint8_t startByte;     //  0
    uint8_t sensorID;      //  1
    uint8_t warningId;     //  2
    uint8_t sensorTextID;  //  3
    uint8_t alarmInverse;  //  4
    int16_t altitude;      //  6
    int16_t maxAltitude;
    int16_t minAltitude;
    int16_t m1s;
    int16_t m3s;
    int16_t m10s;
    uint8_t text[24];
    uint8_t empty;
    uint8_t version;
    uint8_t endByte;
    uint8_t checksum;
} __attribute__((packed)) hott_sensor_vario_t;

typedef struct hott_sensor_airesc_t {
    uint8_t startByte;                  // 1
    uint8_t sensorID;                   // 2
    uint8_t sensorTextID;               // Byte 3
    uint8_t inverse;                    // Byte 4
    uint8_t inverseStatusI;             // Byte 5
    uint16_t inputVolt;                 // Byte 6
    uint16_t minInputVolt;              // Byte 8
    uint16_t capacity;                  // Byte 10
    uint8_t escTemperature;             // Byte 12
    uint8_t maxEscTemperature;          // Byte 13
    uint16_t current;                   // Byte 14
    uint16_t maxCurrent;                // Byte 16
    uint16_t RPM;                       // Byte 18
    uint16_t maxRPM;                    // Byte 20
    uint8_t throttlePercent;            // Byte 22
    uint16_t speed;                     // Byte 23
    uint16_t maxSpeed;                  // Byte 25
    uint8_t BECVoltage;                 // Byte 27
    uint8_t minBECVoltage;              // Byte 28
    uint8_t BECCurrent;                 // Byte 29
    uint8_t minBECCurrent;              // Byte 30
    uint8_t maxBECCurrent;              // Byte 31
    uint8_t PWM;                        // Byte 32
    uint8_t BECTemperature;             // Byte 33
    uint8_t maxBECTemperature;          // Byte 34
    uint8_t motorOrExtTemperature;      // Byte 35
    uint8_t maxMotorOrExtTemperature;   // Byte 36
    uint16_t RPMWithoutGearOrExt;       // Byte 37
    uint8_t timing;                     // Byte 39
    uint8_t advancedTiming;             // Byte 40
    uint8_t highestCurrentMotorNumber;  // Byte 41
    uint8_t versionNumber;              // Byte 42
    uint8_t version;                    /* Byte 43: 00 version number */
    uint8_t endByte;                    /* Byte 44: 0x7D Ende byte */
    uint8_t checksum;                   /* Byte 45: Parity Byte */
} __attribute__((packed)) hott_sensor_airesc_t;

typedef struct hott_sensor_electric_air_t {  // ok
    uint8_t startByte;                       // 1
    uint8_t sensorID;                        // 2
    uint8_t alarmTone;                       // 3: Alarm
    uint8_t sensorTextID;                    // 4:
    uint8_t alarmInverse1;                   // 5:
    uint8_t alarmInverse2;                   // 6:
    uint8_t cell1L;                          // 7: Low Voltage Cell 1 in 0,02 V steps
    uint8_t cell2L;                          // 8: Low Voltage Cell 2 in 0,02 V steps
    uint8_t cell3L;                          // 9: Low Voltage Cell 3 in 0,02 V steps
    uint8_t cell4L;                          // 10: Low Voltage Cell 4 in 0,02 V steps
    uint8_t cell5L;                          // 11: Low Voltage Cell 5 in 0,02 V steps
    uint8_t cell6L;                          // 12: Low Voltage Cell 6 in 0,02 V steps
    uint8_t cell7L;                          // 13: Low Voltage Cell 7 in 0,02 V steps
    uint8_t cell1H;                          // 14: High Voltage Cell 1 in 0.02 V steps
    uint8_t cell2H;                          // 15
    uint8_t cell3H;                          // 16
    uint8_t cell4H;                          // 17
    uint8_t cell5H;                          // 18
    uint8_t cell6H;                          // 19
    uint8_t cell7H;                          // 20
    uint16_t battery1;                       // 21 Battery 1 in 100mv steps; 50 == 5V
    uint16_t battery2;                       // 23 Battery 2 in 100mv steps; 50 == 5V
    uint8_t temp1;                           // 25 Temp 1; Offset of 20. 20 == 0C
    uint8_t temp2;                           // 26 Temp 2; Offset of 20. 20 == 0C
    uint16_t height;                         // 27 28 Height. Offset -500. 500 == 0
    uint16_t current;                        // 29 30 1 = 0.1A
    uint16_t driveVoltage;                   // 31
    uint16_t capacity;                       // 33 34 mAh
    uint16_t m2s;                            // 35 36  /* Steigrate m2s; 0x48 == 0
    uint8_t m3s;                             // 37  /* Steigrate m3s; 0x78 == 0
    uint16_t rpm;                            // 38 39 /* RPM. 10er steps; 300 == 3000rpm
    uint8_t minutes;                         // 40
    uint8_t seconds;                         // 41
    uint8_t speed;                           // 42
    uint8_t version;                         // 43
    uint8_t endByte;                         // 44
    uint8_t checksum;                        // 45
} __attribute__((packed)) hott_sensor_electric_air_t;

typedef struct hott_sensor_general_air_t {  // ok
    uint8_t startByte;                      //#01 start byte constant value 0x7c
    uint8_t sensorID;                       //#02 EAM sensort id. constat value 0x8d=GENRAL AIR MODULE
    uint8_t alarmTone;                      //#03 1=A 2=B ... 0x1a=Z 0 = no alarm
                                            /* VOICE OR BIP WARNINGS
                                    Alarme sonore A.. Z, octet correspondant 1 à 26
                                    0x00 00 0 No alarm
                                    0x01 01 A
                                    0x02 02 B Negative Difference 2 B
                                    0x03 03 C Negative Difference 1 C
                                    0x04 04 D
                                    0x05 05 E
                                    0x06 06 F Min. Sensor 1 temp. F
                                    0x07 07 G Min. Sensor 2 temp. G
                                    0x08 08 H Max. Sensor 1 temp. H
                                    0x09 09 I Max. Sensor 2 temp. I
                                    0xA 10 J Max. Sens. 1 voltage J
                                    0xB 11 K Max. Sens. 2 voltage K
                                    0xC 12 L
                                    0xD 13 M Positive Difference 2 M
                                    0xE 14 N Positive Difference 1 N
                                    0xF 15 O Min. Altitude O
                                    0x10 16 P Min. Power Voltage P // We use this one for Battery Warning
                                    0x11 17 Q Min. Cell voltage Q
                                    0x12 18 R Min. Sens. 1 voltage R
                                    0x13 19 S Min. Sens. 2 voltage S
                                    0x14 20 T Minimum RPM T
                                    0x15 21 U
                                    0x16 22 V Max. used capacity V
                                    0x17 23 W Max. Current W
                                    0x18 24 X Max. Power Voltage X
                                    0x19 25 Y Maximum RPM Y
                                    0x1A 26 Z Max. Altitude Z
                                    */
    uint8_t sensorTextID;                   //#04 constant value 0xd0
    uint8_t alarmInverse1;                  //#05 alarm bitmask. Value is displayed inverted
                                            // Bit# Alarm field
                                            // 0 all cell voltage
                                            // 1 Battery 1
                                            // 2 Battery 2
                                            // 3 Temperature 1
                                            // 4 Temperature 2
                                            // 5 Fuel
                                            // 6 mAh
                                            // 7 Altitude
    uint8_t alarm_invers2;                  //#06 alarm bitmask. Value is displayed inverted
                                            // Bit# Alarm Field
                                            // 0 main power current
                                            // 1 main power voltage
                                            // 2 Altitude
                                            // 3 m/s
                                            // 4 m/3s
                                            // 5 unknown
                                            // 6 unknown
                                            // 7 "ON" sign/text msg active
    uint8_t cell[6];                        //#7 Volt Cell 1 (in 2 mV increments, 210 == 4.20 V)
                                            //#8 Volt Cell 2 (in 2 mV increments, 210 == 4.20 V)
                                            //#9 Volt Cell 3 (in 2 mV increments, 210 == 4.20 V)
                                            //#10 Volt Cell 4 (in 2 mV increments, 210 == 4.20 V)
                                            //#11 Volt Cell 5 (in 2 mV increments, 210 == 4.20 V)
                                            //#12 Volt Cell 6 (in 2 mV increments, 210 == 4.20 V)
    uint16_t battery1;             //#13 LSB battery 1 voltage LSB value. 0.1V steps. 50 = 5.5V only pos. voltages
                                   //#14 MSB
    uint16_t battery2;             //#15 LSB battery 2 voltage LSB value. 0.1V steps. 50 = 5.5V only pos. voltages
                                   //#16 MSB
    uint8_t temperature1;          //#17 Temperature 1. Offset of 20. a value of 20 = 0°C
    uint8_t temperature2;          //#18 Temperature 2. Offset of 20. a value of 20 = 0°C
    uint8_t fuel_procent;          //#19 Fuel capacity in %. Values 0--100
                                   // graphical display ranges: 0-100% with new firmwares of the radios MX12/MX20/...
    uint16_t fuel_ml;              //#20 LSB Fuel in ml scale. Full = 65535!
                                   //#21 MSB
    uint16_t rpm;                  //#22 RPM in 10 RPM steps. 300 = 3000rpm
                                   //#23 MSB
    uint16_t altitude;             //#24 altitude in meters. offset of 500, 500 = 0m
                                   //#25 MSB
    uint16_t climbrate;            //#26 climb rate in 0.01m/s. Value of 30000 = 0.00 m/s
                                   //#27 MSB
    uint8_t climbrate3s;           //#28 climb rate in m/3sec. Value of 120 = 0m/3sec
    uint16_t current;              //#29 current in 0.1A steps 100 == 10,0A
                                   //#30 MSB current display only goes up to 99.9 A (continuous)
    uint16_t main_voltage;         //#31 LSB Main power voltage using 0.1V steps 100 == 10,0V
                                   //#32 MSB (Appears in GAM display right as alternate display.)
    uint16_t batt_cap;             //#33 LSB used battery capacity in 10mAh steps
                                   //#34 MSB
    uint16_t speed;                //#35 LSB (air?) speed in km/h(?) we are using ground speed here per default
                                   //#36 MSB speed
    uint8_t min_cell_volt;         //#37 minimum cell voltage in 2mV steps. 124 = 2,48V
    uint8_t min_cell_volt_num;     //#38 number of the cell with the lowest voltage
    uint16_t rpm2;                 //#39 LSB 2nd RPM in 10 RPM steps. 100 == 1000rpm
                                   //#40 MSB
    uint8_t general_error_number;  //#41 General Error Number (Voice Error == 12) TODO: more documentation
    uint8_t pressure;              //#42 High pressure up to 16bar. 0,1bar scale. 20 == 2.0 bar
                                   // 1 bar = 10 hoch 5 Pa
    uint8_t version;               //#43 version number (Bytes 35 .43 new but not yet in the record in the display!)
    uint8_t endByte;               //#44 stop byte 0x7D
    uint8_t parity;                //#45 CHECKSUM CRC/Parity (calculated dynamicaly)
} __attribute__((packed)) hott_sensor_general_air_t;

typedef struct hott_sensor_gps_t {  // ok
    uint8_t startByte;              /* Byte 1: 0x7C = Start byte data */
    uint8_t sensorID;               /* Byte 2: 0x8A = GPS Sensor */
    uint8_t alarmTone;              /* Byte 3: 0…= warning beeps */
    uint8_t sensorTextID;           /* Byte 4: 160 0xA0 Sensor ID Neu! */

    uint8_t alarmInverse1; /* Byte 5: 01 inverse status */
    uint8_t alarmInverse2; /* Byte 6: 00 inverse status status 1 = kein GPS Signal */

    uint8_t
        flightDirection; /* Byte 7: 119 = Flugricht./dir. 1 = 2°; 0° (North), 9 0° (East), 180° (South), 270° (West) */
    uint16_t GPSSpeed; /* Byte 8: 8 = Geschwindigkeit/GPS speed low byte 8km/h */

    uint8_t LatitudeNS;   /* Byte 10: 000 = N = 48°39’988 */
    uint16_t LatitudeMin; /* Byte 11: 231 0xE7 = 0x12E7 = 4839 */
    uint16_t LatitudeSec; /* Byte 13: 171 220 = 0xDC = 0x03DC =0988 */

    uint8_t longitudeEW;   /* Byte 15: 000  = E= 9° 25’9360 */
    uint16_t longitudeMin; /* Byte 16: 150 157 = 0x9D = 0x039D = 0925 */
    uint16_t longitudeSec; /* Byte 18: 056 144 = 0x90 0x2490 = 9360*/

    uint16_t distance;       /* Byte 20: 027 123 = Entfernung/distance low byte 6 = 6 m */
    uint16_t altitude;       /* Byte 22: 243 244 = Höhe/Altitude low byte 500 = 0m */
    uint16_t climbrate;      /* Byte 24: 48 = Low Byte m/s resolution 0.01m 48 = 30000 = 0.00m/s (1=0.01m/s) */
    uint8_t climbrate3s;     /* Byte 26: climbrate in m/3s resolution, value of 120 = 0 m/3s*/
    uint8_t GPSNumSat;       /* Byte 27: GPS.Satelites (number of satelites) (1 byte) */
    uint8_t GPSFixChar;      /* Byte 28: GPS.FixChar. (GPS fix character. display, if DGPS, 2D oder 3D) (1 byte) */
    uint8_t homeDirection;   /* Byte 29: HomeDirection (direction from starting point to Model position) (1 byte) */
    uint8_t angleXdirection; /* Byte 30: angle x-direction (1 byte) */
    uint8_t angleYdirection; /* Byte 31: angle y-direction (1 byte) */
    uint8_t angleZdirection; /* Byte 32: angle z-direction (1 byte) */

    //  int8_t gps_time_h;  //#33 UTC time hours
    //  int8_t gps_time_m;  //#34 UTC time minutes
    //  int8_t gps_time_s;  //#35 UTC time seconds
    //  int8_t gps_time_sss;//#36 UTC time milliseconds
    //  int8_t msl_altitude_L;  //#37 mean sea level altitude
    //  int8_t msl_altitude_H;  //#38

    uint8_t gyroXLow;  /* Byte 33: gyro x low byte (2 bytes) */
    uint8_t gyroXHigh; /* Byte 34: gyro x high byte */
    uint8_t gyroYLow;  /* Byte 35: gyro y low byte (2 bytes) */
    uint8_t gyroYHigh; /* Byte 36: gyro y high byte */
    uint8_t gyroZLow;  /* Byte 37: gyro z low byte (2 bytes) */
    uint8_t gyroZHigh; /* Byte 38: gyro z high byte */

    uint8_t vibration; /* Byte 39: vibration (1 bytes) */
    uint8_t Ascii4;    /* Byte 40: 00 ASCII Free Character [4] appears right to home distance */
    uint8_t Ascii5;    /* Byte 41: 00 ASCII Free Character [5] appears right to home direction*/
    uint8_t GPS_fix;   /* Byte 42: 00 ASCII Free Character [6], we use it for GPS FIX */
    uint8_t version;   /* Byte 43: 00 version number */
    uint8_t endByte;   /* Byte 44: 0x7D Ende byte */
    uint8_t checksum;  /* Byte 45: Parity Byte */
} __attribute__((packed)) hott_sensor_gps_t;

typedef struct hott_sensors_t {
    bool is_enabled[4];
    float **gps;
    float **vario;
    float **esc;
    float **electric_air;
} hott_sensors_t;

static void process(hott_sensors_t *sensors);
static void send_packet(hott_sensors_t *sensors, uint8_t address);
static uint8_t get_crc(const uint8_t *buffer, uint len);
static void set_config(hott_sensors_t *sensors);

void hott_task(void *parameters) {
    hott_sensors_t sensors = {0};
    set_config(&sensors);
    context.led_cycle_duration = 6;
    context.led_cycles = 1;
    uart0_begin(19200, UART_RECEIVER_TX, UART_RECEIVER_RX, HOTT_TIMEOUT_US, 8, 1, UART_PARITY_NONE, false);
    debug("\nHOTT init");
    while (1) {
        ulTaskNotifyTakeIndexed(1, pdTRUE, portMAX_DELAY);
        process(&sensors);
    }
}

static void process(hott_sensors_t *sensors) {
    if (uart0_available() == HOTT_PACKET_LENGHT) {
        uint8_t buffer[HOTT_PACKET_LENGHT];
        uart0_read_bytes(buffer, HOTT_PACKET_LENGHT);
        debug("\nHOTT (%u) < ", uxTaskGetStackHighWaterMark(NULL));
        debug_buffer(buffer, HOTT_PACKET_LENGHT, "0x%X ");
        if (buffer[0] == HOTT_BINARY_MODE_REQUEST_ID) send_packet(sensors, buffer[1]);
    }
}

static void send_packet(hott_sensors_t *sensors, uint8_t address) {
    // packet in little endian
    switch (address) {
        case HOTT_VARIO_MODULE_ID: {
            if (!sensors->is_enabled[HOTT_TYPE_VARIO]) return;
            hott_sensor_vario_t packet = {0};
            packet.startByte = 0x7C;
            packet.sensorID = HOTT_VARIO_MODULE_ID;
            packet.sensorTextID = HOTT_VARIO_SENSOR_ID;
            packet.endByte = 0x7D;
            packet.checksum = get_crc((uint8_t *)&packet, sizeof(packet) - 1);
            uart0_write_bytes((uint8_t *)&packet, sizeof(packet));
            debug("\nHOTT (%u) %u > ", uxTaskGetStackHighWaterMark(NULL), sizeof(packet));
            debug_buffer(&packet, sizeof(packet), "0x%X ");

            // blink led
            vTaskResume(context.led_task_handle);
            break;
        }
        case HOTT_ESC_MODULE_ID: {
            if (!sensors->is_enabled[HOTT_TYPE_ESC]) return;
            hott_sensor_electric_air_t packet = {0};
            packet.startByte = 0x7C;
            packet.sensorID = HOTT_ESC_MODULE_ID;
            // packet.sensorTextID = HOTT_ELECTRIC_AIR_SENSOR_ID;
            packet.endByte = 0x7D;
            packet.checksum = get_crc((uint8_t *)&packet, sizeof(packet) - 1);
            uart0_write_bytes((uint8_t *)&packet, sizeof(packet));
            debug("\nHOTT (%u) Len: %u > ", uxTaskGetStackHighWaterMark(NULL), sizeof(packet));
            debug_buffer((uint8_t *)&packet, sizeof(packet), "0x%X ");

            // blink led
            vTaskResume(context.led_task_handle);
            break;
        }
        case HOTT_ELECTRIC_AIR_MODULE_ID: {
            if (!sensors->is_enabled[HOTT_TYPE_ELECTRIC]) return;
            hott_sensor_electric_air_t packet = {0};
            packet.startByte = 0x7C;
            packet.sensorID = HOTT_ELECTRIC_AIR_MODULE_ID;
            packet.sensorTextID = HOTT_ELECTRIC_AIR_SENSOR_ID;
            packet.endByte = 0x7D;
            packet.checksum = get_crc((uint8_t *)&packet, sizeof(packet) - 1);
            uart0_write_bytes((uint8_t *)&packet, sizeof(packet));
            debug("\nHOTT (%u) %u > ", uxTaskGetStackHighWaterMark(NULL), sizeof(packet));
            debug_buffer(&packet, sizeof(packet), "0x%X ");

            // blink led
            vTaskResume(context.led_task_handle);
            break;
        }
    }
}

static uint8_t get_crc(const uint8_t *buffer, uint len) {
    uint8_t crc = 0;
    for (uint i = 0; i < len; i++) crc += buffer[i];
    return crc;
}

static void set_config(hott_sensors_t *sensors) {
    config_t *config = config_read();
    TaskHandle_t task_handle;
    if (config->esc_protocol == ESC_PWM) {
        esc_pwm_parameters_t parameter = {config->rpm_multiplier, config->alpha_rpm, malloc(sizeof(float))};
        xTaskCreate(esc_pwm_task, "esc_pwm_task", STACK_ESC_PWM, (void *)&parameter, 2, &task_handle);
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_ESC] = true;
        sensors->esc[HOTT_ESC_RPM] = parameter.rpm;
    }
    if (config->esc_protocol == ESC_HW3) {
        esc_hw3_parameters_t parameter = {config->rpm_multiplier, config->alpha_rpm, malloc(sizeof(float))};
        xTaskCreate(esc_hw3_task, "esc_hw3_task", STACK_ESC_HW3, (void *)&parameter, 2, &task_handle);
        context.uart1_notify_task_handle = task_handle;
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_ESC] = true;
        sensors->esc[HOTT_ESC_RPM] = parameter.rpm;
    }
    if (config->esc_protocol == ESC_HW4) {
        esc_hw4_parameters_t parameter = {config->rpm_multiplier,
                                          config->enable_pwm_out,
                                          config->enable_esc_hw4_init_delay,
                                          config->alpha_rpm,
                                          config->alpha_voltage,
                                          config->alpha_current,
                                          config->alpha_temperature,
                                          config->esc_hw4_divisor,
                                          config->esc_hw4_current_multiplier,
                                          config->esc_hw4_current_thresold,
                                          config->esc_hw4_current_max,
                                          config->esc_hw4_is_manual_offset,
                                          config->esc_hw4_offset,
                                          malloc(sizeof(float)),
                                          malloc(sizeof(float)),
                                          malloc(sizeof(float)),
                                          malloc(sizeof(float)),
                                          malloc(sizeof(float)),
                                          malloc(sizeof(float)),
                                          malloc(sizeof(float)),
                                          malloc(sizeof(uint8_t))};
        xTaskCreate(esc_hw4_task, "esc_hw4_task", STACK_ESC_HW4, (void *)&parameter, 2, &task_handle);
        context.uart1_notify_task_handle = task_handle;
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (config->enable_pwm_out) {
            xTaskCreate(pwm_out_task, "pwm_out", STACK_PWM_OUT, (void *)parameter.rpm, 2, &task_handle);
            context.pwm_out_task_handle = task_handle;
            xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }

        sensors->is_enabled[HOTT_TYPE_ESC] = true;
        sensors->esc[HOTT_ESC_RPM] = parameter.rpm;
        sensors->esc[HOTT_ESC_TEMPERATURE] = parameter.temperature_fet;
        sensors->esc[HOTT_ESC_BEC_TEMPERATURE] = parameter.temperature_bec;
        sensors->esc[HOTT_ESC_VOLTAGE] = parameter.voltage;
        sensors->esc[HOTT_ESC_CURRENT] = parameter.current;
        sensors->esc[HOTT_ESC_CAPACITY] = parameter.consumption;
    }
    if (config->esc_protocol == ESC_HW5) {
        esc_hw5_parameters_t parameter = {
            config->rpm_multiplier,    config->alpha_rpm,     config->alpha_voltage, config->alpha_current,
            config->alpha_temperature, malloc(sizeof(float)), malloc(sizeof(float)), malloc(sizeof(float)),
            malloc(sizeof(float)),     malloc(sizeof(float)), malloc(sizeof(float)), malloc(sizeof(float)),
            malloc(sizeof(float)),     malloc(sizeof(float)), malloc(sizeof(float)), malloc(sizeof(uint8_t))};
        xTaskCreate(esc_hw5_task, "esc_hw5_task", STACK_ESC_HW5, (void *)&parameter, 2, &task_handle);
        context.uart1_notify_task_handle = task_handle;
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_ESC] = true;
        sensors->esc[HOTT_ESC_RPM] = parameter.rpm;
        sensors->esc[HOTT_ESC_TEMPERATURE] = parameter.temperature_fet;
        sensors->esc[HOTT_ESC_BEC_TEMPERATURE] = parameter.temperature_bec;
        sensors->esc[HOTT_ESC_VOLTAGE] = parameter.voltage;
        sensors->esc[HOTT_ESC_BEC_VOLTAGE] = parameter.voltage_bec;
        sensors->esc[HOTT_ESC_CURRENT] = parameter.current;
        sensors->esc[HOTT_ESC_BEC_CURRENT] = parameter.current_bec;
        sensors->esc[HOTT_ESC_CAPACITY] = parameter.consumption;
        sensors->esc[HOTT_ESC_EXT_TEMPERATURE] = parameter.temperature_motor;
    }
    if (config->esc_protocol == ESC_CASTLE) {
        esc_castle_parameters_t parameter = {config->rpm_multiplier, config->alpha_rpm,         config->alpha_voltage,
                                             config->alpha_current,  config->alpha_temperature, malloc(sizeof(float)),
                                             malloc(sizeof(float)),  malloc(sizeof(float)),     malloc(sizeof(float)),
                                             malloc(sizeof(float)),  malloc(sizeof(float)),     malloc(sizeof(float)),
                                             malloc(sizeof(float)),  malloc(sizeof(float)),     malloc(sizeof(float)),
                                             malloc(sizeof(float)),  malloc(sizeof(uint8_t))};
        xTaskCreate(esc_castle_task, "esc_castle_task", STACK_ESC_CASTLE, (void *)&parameter, 2, &task_handle);
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_ESC] = true;
        sensors->esc[HOTT_ESC_RPM] = parameter.rpm;
        sensors->esc[HOTT_ESC_TEMPERATURE] = parameter.temperature;
        sensors->esc[HOTT_ESC_VOLTAGE] = parameter.voltage;
        sensors->esc[HOTT_ESC_BEC_VOLTAGE] = parameter.voltage_bec;
        sensors->esc[HOTT_ESC_CURRENT] = parameter.current;
        sensors->esc[HOTT_ESC_BEC_CURRENT] = parameter.current_bec;
        sensors->esc[HOTT_ESC_CAPACITY] = parameter.consumption;
        sensors->esc[HOTT_ESC_EXT_TEMPERATURE] = parameter.consumption;
    }
    if (config->esc_protocol == ESC_KONTRONIK) {
        esc_kontronik_parameters_t parameter = {
            config->rpm_multiplier,    config->alpha_rpm,     config->alpha_voltage,  config->alpha_current,
            config->alpha_temperature, malloc(sizeof(float)), malloc(sizeof(float)),  malloc(sizeof(float)),
            malloc(sizeof(float)),     malloc(sizeof(float)), malloc(sizeof(float)),  malloc(sizeof(float)),
            malloc(sizeof(float)),     malloc(sizeof(float)), malloc(sizeof(uint8_t))};
        xTaskCreate(esc_kontronik_task, "esc_kontronik_task", STACK_ESC_KONTRONIK, (void *)&parameter, 2, &task_handle);
        context.uart1_notify_task_handle = task_handle;
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_ESC] = true;
        sensors->esc[HOTT_ESC_RPM] = parameter.rpm;
        sensors->esc[HOTT_ESC_TEMPERATURE] = parameter.temperature_fet;
        sensors->esc[HOTT_ESC_BEC_TEMPERATURE] = parameter.temperature_bec;
        sensors->esc[HOTT_ESC_VOLTAGE] = parameter.voltage;
        sensors->esc[HOTT_ESC_BEC_VOLTAGE] = parameter.voltage_bec;
        sensors->esc[HOTT_ESC_CURRENT] = parameter.current;
        sensors->esc[HOTT_ESC_BEC_CURRENT] = parameter.current_bec;
        sensors->esc[HOTT_ESC_CAPACITY] = parameter.consumption;
    }
    if (config->esc_protocol == ESC_APD_F) {
        esc_apd_f_parameters_t parameter = {config->rpm_multiplier, config->alpha_rpm,         config->alpha_voltage,
                                            config->alpha_current,  config->alpha_temperature, malloc(sizeof(float)),
                                            malloc(sizeof(float)),  malloc(sizeof(float)),     malloc(sizeof(float)),
                                            malloc(sizeof(float)),  malloc(sizeof(float)),     malloc(sizeof(uint8_t))};
        xTaskCreate(esc_apd_f_task, "esc_apd_f_task", STACK_ESC_APD_F, (void *)&parameter, 2, &task_handle);
        context.uart1_notify_task_handle = task_handle;
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_ESC] = true;
        sensors->esc[HOTT_ESC_RPM] = parameter.rpm;
        sensors->esc[HOTT_ESC_TEMPERATURE] = parameter.temperature;
        sensors->esc[HOTT_ESC_VOLTAGE] = parameter.voltage;
        sensors->esc[HOTT_ESC_CURRENT] = parameter.current;
        sensors->esc[HOTT_ESC_CAPACITY] = parameter.consumption;
    }
    if (config->esc_protocol == ESC_APD_HV) {
        esc_apd_hv_parameters_t parameter = {
            config->rpm_multiplier,    config->alpha_rpm,     config->alpha_voltage, config->alpha_current,
            config->alpha_temperature, malloc(sizeof(float)), malloc(sizeof(float)), malloc(sizeof(float)),
            malloc(sizeof(float)),     malloc(sizeof(float)), malloc(sizeof(float)), malloc(sizeof(uint8_t))};
        xTaskCreate(esc_apd_hv_task, "esc_apd_hv_task", STACK_ESC_APD_HV, (void *)&parameter, 2, &task_handle);
        context.uart1_notify_task_handle = task_handle;
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_ESC] = true;
        sensors->esc[HOTT_ESC_RPM] = parameter.rpm;
        sensors->esc[HOTT_ESC_TEMPERATURE] = parameter.temperature;
        sensors->esc[HOTT_ESC_VOLTAGE] = parameter.voltage;
        sensors->esc[HOTT_ESC_CURRENT] = parameter.current;
        sensors->esc[HOTT_ESC_CAPACITY] = parameter.consumption;
    }
    if (config->enable_gps) {
        nmea_parameters_t parameter = {config->gps_baudrate,  malloc(sizeof(float)), malloc(sizeof(float)),
                                       malloc(sizeof(float)), malloc(sizeof(float)), malloc(sizeof(float)),
                                       malloc(sizeof(float)), malloc(sizeof(float)), malloc(sizeof(float)),
                                       malloc(sizeof(float)), malloc(sizeof(float)), malloc(sizeof(float)),
                                       malloc(sizeof(float))};
        xTaskCreate(nmea_task, "nmea_task", STACK_GPS, (void *)&parameter, 2, &task_handle);
        context.uart_pio_notify_task_handle = task_handle;
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_GPS] = true;
        sensors->gps[HOTT_GPS_LATITUDE] = parameter.lat;
        sensors->gps[HOTT_GPS_LONGITUDE] = parameter.lon;
        sensors->gps[HOTT_GPS_SATS] = parameter.sat;
        sensors->gps[HOTT_GPS_ALTITUDE] = parameter.alt;
        sensors->gps[HOTT_GPS_SPEED] = parameter.spd_kmh;
        sensors->gps[HOTT_GPS_DIRECTION] = parameter.cog;
        sensors->gps[HOTT_GPS_DIRECTION] = parameter.cog;
    }
    if (config->enable_analog_voltage) {
        voltage_parameters_t parameter = {0, config->analog_rate, config->alpha_voltage,
                                          config->analog_voltage_multiplier, malloc(sizeof(float))};
        xTaskCreate(voltage_task, "voltage_task", STACK_VOLTAGE, (void *)&parameter, 2, &task_handle);
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_ELECTRIC] = true;
        sensors->electric_air[HOTT_ELECTRIC_BAT_1_VOLTAGE] = parameter.voltage;
    }
    if (config->enable_analog_current) {
        current_parameters_t parameter = {1,
                                          config->analog_rate,
                                          config->alpha_current,
                                          config->analog_current_multiplier,
                                          config->analog_current_offset,
                                          config->analog_current_autoffset,
                                          malloc(sizeof(float)),
                                          malloc(sizeof(float)),
                                          malloc(sizeof(float))};
        xTaskCreate(current_task, "current_task", STACK_CURRENT, (void *)&parameter, 2, &task_handle);
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_ELECTRIC] = true;
        sensors->electric_air[HOTT_ELECTRIC_CURRENT] = parameter.current;
    }
    if (config->enable_analog_ntc) {
        ntc_parameters_t parameter = {2, config->analog_rate, config->alpha_temperature, malloc(sizeof(float))};
        xTaskCreate(ntc_task, "ntc_task", STACK_NTC, (void *)&parameter, 2, &task_handle);
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_ELECTRIC] = true;
        sensors->electric_air[HOTT_ELECTRIC_TEMPERATURE_1] = parameter.ntc;
    }
    if (config->enable_analog_airspeed) {
        airspeed_parameters_t parameter = {3, config->analog_rate, config->alpha_airspeed, malloc(sizeof(float))};
        xTaskCreate(airspeed_task, "airspeed_task", STACK_AIRSPEED, (void *)&parameter, 2, &task_handle);
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_ESC] = true;
        sensors->esc[HOTT_ESC_SPEED] = parameter.airspeed;
    }
    if (config->i2c_module == I2C_BMP280) {
        bmp280_parameters_t parameter = {config->alpha_vario,   config->vario_auto_offset, config->i2c_address,
                                         config->bmp280_filter, malloc(sizeof(float)),     malloc(sizeof(float)),
                                         malloc(sizeof(float)), malloc(sizeof(float))};
        xTaskCreate(bmp280_task, "bmp280_task", STACK_BMP280, (void *)&parameter, 2, &task_handle);
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_VARIO] = true;
        sensors->vario[HOTT_VARIO_ALTITUDE] = parameter.altitude;
    }
    if (config->i2c_module == I2C_MS5611) {
        ms5611_parameters_t parameter = {config->alpha_vario,   config->vario_auto_offset, config->i2c_address,
                                         malloc(sizeof(float)), malloc(sizeof(float)),     malloc(sizeof(float)),
                                         malloc(sizeof(float))};
        xTaskCreate(ms5611_task, "ms5611_task", STACK_MS5611, (void *)&parameter, 2, &task_handle);
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_VARIO] = true;
        sensors->vario[HOTT_VARIO_ALTITUDE] = parameter.altitude;
    }
    if (config->i2c_module == I2C_BMP180) {
        bmp180_parameters_t parameter = {config->alpha_vario,   config->vario_auto_offset, config->i2c_address,
                                         malloc(sizeof(float)), malloc(sizeof(float)),     malloc(sizeof(float)),
                                         malloc(sizeof(float))};
        xTaskCreate(bmp180_task, "bmp180_task", STACK_BMP180, (void *)&parameter, 2, &task_handle);
        xQueueSendToBack(context.tasks_queue_handle, task_handle, 0);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensors->is_enabled[HOTT_TYPE_VARIO] = true;
        sensors->vario[HOTT_VARIO_ALTITUDE] = parameter.altitude;
    }
}
