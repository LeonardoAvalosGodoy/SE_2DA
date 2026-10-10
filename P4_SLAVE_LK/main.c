
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/i2c_master.h"
#include "driver/i2c_slave.h"
#include "driver/gpio.h"
#include "esp_err.h"

#define UART_PORT UART_NUM_0
#define UART_BAUD_RATE 115200
#define TASK_STACK_SIZE 4096
#define BUF_SIZE 1024

#define BMP_SDA 21
#define BMP_SCL 22
#define BMP280_ADDR 0x77

#define SLAVE_SDA 18
#define SLAVE_SCL 19
#define SLAVE_ADDR 0x42

#define HEADER_REQ 0x1F
#define HEADER_RESP 0x2F
#define CMD 0x28

static i2c_master_bus_handle_t bmp_bus;
static i2c_master_dev_handle_t bmp_sensor;
static i2c_slave_dev_handle_t slave_handle;

static QueueHandle_t rx_queue;
static uint8_t rx_buf[2];

static uint16_t T1;
static int16_t T2;
static int16_t T3;

/* ESTRUCTURA PARA GUARDAR UNA SOLICITUD */
typedef struct {
    uint8_t header;
    uint8_t cmd;
} solicitud_t;

/* CONFIGURACIÓN UART */
void init_uart(void){
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

/* ESCRIBIR POR UART */
void puts_uart(uart_port_t uart_num, const char *str){
    uart_write_bytes(uart_num, str, strlen(str));
}


void float_to_str(float val, char *buf){
    int i = 0;
    bool negativo = (val < 0.0f);

    if(negativo) val = -val;

    int total = (int)(val * 100.0f + 0.5f);
    int entero = total / 100;
    int decimal = total % 100;

    if(negativo && total != 0) buf[i++] = '-';

    if(entero == 0){
        buf[i++] = '0';
    }else{
        char tmp[8];
        int t = 0;

        while(entero > 0 && t < (int)sizeof(tmp)){
            tmp[t++] = '0' + (entero % 10);
            entero /= 10;
        }

        while(t > 0) buf[i++] = tmp[--t];
    }

    buf[i++] = '.';
    buf[i++] = '0' + (decimal / 10);
    buf[i++] = '0' + (decimal % 10);
    buf[i] = '\0';
}

/* EVENTO DE RECEPCIÓN I2C */
static bool IRAM_ATTR on_recv(i2c_slave_dev_handle_t handle,
                              const i2c_slave_rx_done_event_data_t *edata,
                              void *arg){
    BaseType_t woken = pdFALSE;
    solicitud_t solicitud = {0};

    // Copia los bytes y no solamente el puntero al buffer.
    if(edata != NULL && edata->buffer != NULL){
        solicitud.header = edata->buffer[0];
        solicitud.cmd = edata->buffer[1];
    }

    // Envía una copia de la solicitud a la tarea.
    xQueueSendFromISR((QueueHandle_t)arg, &solicitud, &woken);
    return woken == pdTRUE;
}

/* INICIALIZACIÓN DEL BMP280 */
static void bmp280_init(void){
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BMP_SDA,
        .scl_io_num = BMP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bmp_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BMP280_ADDR,
        .scl_speed_hz = 100000,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bmp_bus, &dev_cfg, &bmp_sensor));

    // Obtiene los valores de calibración de temperatura.
    uint8_t reg = 0x88;
    uint8_t cal[6];

    ESP_ERROR_CHECK(i2c_master_transmit_receive(bmp_sensor, &reg, 1, cal, 6, 1000));

    T1 = ((uint16_t)cal[1] << 8) | cal[0];
    T2 = (int16_t)(((uint16_t)cal[3] << 8) | cal[2]);
    T3 = (int16_t)(((uint16_t)cal[5] << 8) | cal[4]);

    // Configura el BMP280 para realizar mediciones.
    uint8_t cmd[2] = {0xF4, 0x27};
    ESP_ERROR_CHECK(i2c_master_transmit(bmp_sensor, cmd, sizeof(cmd), 1000));

    puts_uart(UART_PORT, "\r\nBMP280 OK\r\n");
}

/* LEER TEMPERATURA DEL BMP280 */
static float bmp280_read(void){
    uint8_t reg = 0xFA;
    uint8_t raw[3];

    if(i2c_master_transmit_receive(bmp_sensor, &reg, 1, raw, 3, 1000) != ESP_OK){
        return -999.0f;
    }

    int32_t adc = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    int32_t v1 = ((((adc >> 3) - ((int32_t)T1 << 1)) * (int32_t)T2) >> 11);
    int32_t v2 = (((((adc >> 4) - (int32_t)T1) * ((adc >> 4) - (int32_t)T1)) >> 12) * (int32_t)T3) >> 14;

    return (float)(((v1 + v2) * 5 + 128) >> 8) / 100.0f;
}

/* PREPARAR LA SIGUIENTE RECEPCIÓN */
static void preparar_recepcion(void){
    // Evita conservar el encabezado y comando anteriores.
    memset(rx_buf, 0, sizeof(rx_buf));
    ESP_ERROR_CHECK(i2c_slave_receive(slave_handle, rx_buf, sizeof(rx_buf)));
}

/* INICIALIZACIÓN I2C SLAVE */
static void init_i2c_slave(void){
    i2c_slave_config_t cfg = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = SLAVE_SDA,
        .scl_io_num = SLAVE_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .send_buf_depth = 512,
        .slave_addr = SLAVE_ADDR,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7,
    };

    ESP_ERROR_CHECK(i2c_new_slave_device(&cfg, &slave_handle));

    gpio_set_pull_mode(SLAVE_SDA, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SLAVE_SCL, GPIO_PULLUP_ONLY);

    i2c_slave_event_callbacks_t callbacks = {
        .on_recv_done = on_recv,
    };

    ESP_ERROR_CHECK(i2c_slave_register_event_callbacks(slave_handle, &callbacks, rx_queue));
    preparar_recepcion();
}

/* TAREA QUE RECIBE SOLICITUDES Y RESPONDE */
static void tarea_slave(void *arg){
    solicitud_t solicitud;
    char temp_str[16];

    puts_uart(UART_PORT, "\r\nSlave listo\r\n");

    while(1){
        if(xQueueReceive(rx_queue, &solicitud, portMAX_DELAY) == pdTRUE){

            // Solo procesa las solicitudes que coinciden.
            if(solicitud.header == HEADER_REQ && solicitud.cmd == CMD){
                float temperatura = bmp280_read();

                if(temperatura != -999.0f){
                    // Prepara la respuesta con la temperatura.
                    uint8_t tx[6] = {HEADER_RESP, CMD, 0, 0, 0, 0};
                    memcpy(&tx[2], &temperatura, sizeof(float));

                    // Coloca la respuesta para que el master la lea.
                    esp_err_t err = i2c_slave_transmit(slave_handle, tx, sizeof(tx), 200);

                    if(err != ESP_OK){
                        puts_uart(UART_PORT, "\r\nError al preparar respuesta I2C\r\n");

                        // Reinicia el dispositivo si falló la transmisión.
                        ESP_ERROR_CHECK(i2c_del_slave_device(slave_handle));
                        slave_handle = NULL;
                        xQueueReset(rx_queue);

                        vTaskDelay(pdMS_TO_TICKS(10));
                        init_i2c_slave();
                        continue;
                    }

                    // Muestra una medición por solicitud válida.
                    float_to_str(temperatura, temp_str);
                    puts_uart(UART_PORT, "\r\n(SLAVE) Temperatura: ");
                    puts_uart(UART_PORT, temp_str);
                    puts_uart(UART_PORT, " C\r\n");
                }
            }

            // Prepara la siguiente recepción, sea válida o no.
            preparar_recepcion();
        }
    }
}

/* FUNCIÓN PRINCIPAL */
void app_main(void){
    init_uart();
    bmp280_init();

    // La cola guarda copias de las solicitudes.
    rx_queue = xQueueCreate(4, sizeof(solicitud_t));
    configASSERT(rx_queue != NULL);

    init_i2c_slave();

    xTaskCreate(tarea_slave, "tarea_slave", TASK_STACK_SIZE, NULL, 10, NULL);
}
