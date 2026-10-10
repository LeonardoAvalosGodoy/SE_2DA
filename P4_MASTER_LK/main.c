
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_timer.h"

#define UART_PORT UART_NUM_0
#define UART_BAUD_RATE 115200
#define TASK_STACK_SIZE 4096
#define PRINT_STACK_SIZE 3072
#define BUF_SIZE 1024

#define MASTER_SDA 21
#define MASTER_SCL 22
#define SLAVE_ADDR 0x42

#define HEADER_REQ 0x1F
#define HEADER_RESP 0x2F
#define CMD 0x28

#define MAX_RETRIES 2
#define PERIODO_MS 2000
#define ESPERA_SLAVE_MS 50
#define TIMEOUT_INTENTO_MS 500
#define TIMEOUT_TX_MS 100

#define COLA_LEN 8
#define PRIO_SOLICITUD 10
#define PRIO_IMPRESION 5

typedef enum {
    MSG_REINTENTO,
    MSG_TEMPERATURA,
    MSG_FIN
} tipo_msg_t;

typedef struct {
    tipo_msg_t tipo;
    int intento;
    float temperatura;
} mensaje_t;

static i2c_master_bus_handle_t master_bus;
static i2c_master_dev_handle_t slave_dev;
static QueueHandle_t cola_msg;

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

/* CONFIGURACIÓN I2C MASTER */
void init_i2c_bus(void){
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = MASTER_SDA,
        .scl_io_num = MASTER_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &master_bus));
}

void init_i2c_slave(void){
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SLAVE_ADDR,
        .scl_speed_hz = 100000,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(master_bus, &dev_cfg, &slave_dev));
}

/* ESCRIBIR POR UART */
void puts_uart(uart_port_t uart_num, const char *str){
    uart_write_bytes(uart_num, str, strlen(str));
}

/* CONVERTIR TEMPERATURA A TEXTO */
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

/* ENVIAR MENSAJES A LA TAREA DE IMPRESIÓN */
static void enviar_msg(tipo_msg_t tipo, int intento, float temperatura){
    mensaje_t m = {
        .tipo = tipo,
        .intento = intento,
        .temperatura = temperatura,
    };

    // No perder la temperatura ni el mensaje de fin.
    TickType_t espera = 0;

    if(tipo == MSG_TEMPERATURA || tipo == MSG_FIN){
        espera = portMAX_DELAY;
    }

    xQueueSend(cola_msg, &m, espera);
}

/* TAREA QUE IMPRIME POR UART */
static void tarea_impresion(void *arg){
    mensaje_t m;
    char temp_str[16];
    char reintento[] = "\r\nReintento X...\r\n";

    puts_uart(UART_PORT, "\r\nMaster listo\r\n");

    while(1){
        if(xQueueReceive(cola_msg, &m, portMAX_DELAY) == pdTRUE){
            switch(m.tipo){
                case MSG_REINTENTO:
                    reintento[12] = '0' + m.intento;
                    puts_uart(UART_PORT, reintento);
                    break;

                case MSG_TEMPERATURA:
                    float_to_str(m.temperatura, temp_str);
                    puts_uart(UART_PORT, "\r\n(MASTER) Temperatura: ");
                    puts_uart(UART_PORT, temp_str);
                    puts_uart(UART_PORT, " C\r\n");
                    break;

                case MSG_FIN:
                    puts_uart(UART_PORT, "\r\nComunicacion terminada, el periferico no responde\r\n");
                    break;
            }
        }
    }
}

/* CALCULAR CUÁNTO TIEMPO QUEDA DE LOS 500 MS */
static int tiempo_restante_ms(int64_t inicio_us){
    int64_t transcurrido = esp_timer_get_time() - inicio_us;
    int64_t restante = (TIMEOUT_INTENTO_MS * 1000LL) - transcurrido;

    if(restante <= 0) return 0;
    return (int)(restante / 1000);
}

/* ESPERAR EL TIEMPO RESTANTE ANTES DE REINTENTAR */
static void esperar_fin_intento(int64_t inicio_us){
    int restante = tiempo_restante_ms(inicio_us);

    if(restante > 0){
        vTaskDelay(pdMS_TO_TICKS(restante));
    }
}

/* TAREA QUE SOLICITA LA TEMPERATURA */
static void tarea_solicitud(void *arg){
    uint8_t tx[2] = {HEADER_REQ, CMD};
    uint8_t rx[6];
    TickType_t ultima_activacion = xTaskGetTickCount();

    while(1){
        // Inicia una solicitud cada 2 segundos.
        vTaskDelayUntil(&ultima_activacion, pdMS_TO_TICKS(PERIODO_MS));

        bool respuesta = false;

        // Una solicitud inicial y máximo dos reintentos.
        for(int intento = 0; intento <= MAX_RETRIES; intento++){
            if(intento > 0){
                enviar_msg(MSG_REINTENTO, intento, 0.0f);
            }

            // Comienza a contar los 500 ms de este intento.
            int64_t inicio_us = esp_timer_get_time();

            esp_err_t err = i2c_master_transmit(slave_dev, tx, sizeof(tx), TIMEOUT_TX_MS);

            if(err == ESP_OK){
                // Da tiempo al slave para leer el BMP280.
                vTaskDelay(pdMS_TO_TICKS(ESPERA_SLAVE_MS));

                // Calcula cuánto queda de los 500 ms.
                int restante = tiempo_restante_ms(inicio_us);

                if(restante > 0){
                    err = i2c_master_receive(slave_dev, rx, sizeof(rx), restante);

                    if(err == ESP_OK && rx[0] == HEADER_RESP && rx[1] == CMD){
                        float temperatura;
                        memcpy(&temperatura, &rx[2], sizeof(float));

                        enviar_msg(MSG_TEMPERATURA, 0, temperatura);
                        respuesta = true;
                        break;
                    }
                }
            }

            // Si falló, espera hasta completar el intento.
            esperar_fin_intento(inicio_us);
        }

        // Después de tres intentos fallidos termina la comunicación.
        if(!respuesta){
            enviar_msg(MSG_FIN, 0, 0.0f);
            vTaskSuspend(NULL);
        }
    }
}

/* FUNCIÓN PRINCIPAL */
void app_main(void){
    init_uart();
    init_i2c_bus();
    init_i2c_slave();

    cola_msg = xQueueCreate(COLA_LEN, sizeof(mensaje_t));
    configASSERT(cola_msg != NULL);

    xTaskCreate(tarea_impresion, "tarea_impresion", PRINT_STACK_SIZE, NULL, PRIO_IMPRESION, NULL);
    xTaskCreate(tarea_solicitud, "tarea_solicitud", TASK_STACK_SIZE, NULL, PRIO_SOLICITUD, NULL);
}
