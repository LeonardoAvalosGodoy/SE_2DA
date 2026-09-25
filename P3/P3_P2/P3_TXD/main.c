#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_err.h"

#define BUF_SIZE0 1024
#define BUF_SIZE1 1024
#define MAX_TEXTO 25
#define SF '#'
#define EF '$'

// UART0: terminal. UART1: comunicacion con el ESP32 2.
// UART1: TXD = GPIO4, RXD = GPIO5.

typedef struct {
    char texto[MAX_TEXTO + 1];
} mensaje_t;

static QueueHandle_t cola_mensajes;
static QueueHandle_t cola_resultados;

// Inicializar UART0.
void init_uart0(void) {
    uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, BUF_SIZE0 * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

// Inicializar UART1.
void init_uart1(void) {
    uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, BUF_SIZE1 * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 4, 5, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

// Recibir un caracter.
char uart_getchar(uart_port_t uart_num) {
    uint8_t c = 0;
    uart_read_bytes(uart_num, &c, 1, portMAX_DELAY);
    return (char)c;
}

// Enviar un caracter.
void putchar_uart(uart_port_t uart_num, char c) {
    uart_write_bytes(uart_num, &c, 1);
}

// Enviar una cadena.
void puts_uart(uart_port_t uart_num, const char *str) {
    uart_write_bytes(uart_num, str, strlen(str));
}

// Verificar los caracteres permitidos.
bool caracter_valido(char c) {
    if ((c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') ||
        c == ' ' || c == '!' || c == '.' ||
        c == ',' || c == '+' || c == '-') {
        return true;
    }

    return false;
}

// Recibir un mensaje de hasta 25 caracteres, con Enter y Backspace.
void uart_getPancarta(uart_port_t uart_num, char *str, uint8_t max_len) {
    uint8_t i = 0;
    char c;
    static bool ignorar_lf = false;

    while (1) {
        c = uart_getchar(uart_num);

        // Evitar procesar dos veces Enter cuando llega CR + LF.
        if (ignorar_lf) {
            ignorar_lf = false;

            if (c == '\n') {
                continue;
            }
        }

        // Detectar Enter.
        if (c == '\r' || c == '\n') {
            if (c == '\r') {
                ignorar_lf = true;
            }

            puts_uart(uart_num, "\r\n");
            break;
        }

        // Detectar Backspace o Delete.
        if (c == 8 || (uint8_t)c == 127) {
            if (i > 0) {
                i--;
                puts_uart(uart_num, "\b \b");
            }

            continue;
        }

        // Validar el caracter y la longitud.
        if (caracter_valido(c) && i < max_len - 1) {
            str[i] = c;
            i++;
            putchar_uart(uart_num, c);
        }
    }

    str[i] = '\0';
}

// Calcular checksum con XOR.
uint8_t calcular_checksum(const char *mensaje) {
    uint8_t checksum = 0;

    for (int i = 0; mensaje[i] != '\0'; i++) {
        checksum ^= (uint8_t)mensaje[i];
    }

    return checksum;
}

// Esperar ACK o NACK por UART1, con tiempo limite de 2 segundos.
bool esperar_respuesta(void) {
    char respuesta[5];
    size_t i = 0;
    uint8_t c;
    const TickType_t limite = pdMS_TO_TICKS(2000);
    TickType_t inicio = xTaskGetTickCount();

    while (xTaskGetTickCount() - inicio < limite) {
        TickType_t restante = limite - (xTaskGetTickCount() - inicio);

        if (uart_read_bytes(UART_NUM_1, &c, 1, restante) != 1) {
            continue;
        }

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            respuesta[i] = '\0';
            return strcmp(respuesta, "ACK") == 0;
        }

        if (i >= sizeof(respuesta) - 1) {
            return false;
        }

        respuesta[i] = (char)c;
        i++;
    }

    return false;
}

// Tarea 1: recibir y validar el mensaje del usuario.
void recepcion_mensaje_task(void *arg) {
    mensaje_t mensaje;
    bool correcto;
    (void)arg;

    vTaskDelay(500 / portTICK_PERIOD_MS);
    while (1) {
        puts_uart(UART_NUM_0, "Ingrese un texto maximo de 25 caracteres: ");
        uart_getPancarta(UART_NUM_0, mensaje.texto, sizeof(mensaje.texto));

        xQueueSend(cola_mensajes, &mensaje, portMAX_DELAY);
        xQueueReceive(cola_resultados, &correcto, portMAX_DELAY);

        if (correcto) {
            puts_uart(UART_NUM_0, "Mensaje enviado correctamente.\r\n");
        } else {
            puts_uart(UART_NUM_0, "Error al enviar mensaje.\r\n");
        }
    }
}

// Tarea 2: construir y enviar la trama; esperar ACK o NACK.
void transmision_trama_task(void *arg) {
    mensaje_t mensaje;
    uint8_t checksum;
    bool correcto;
    (void)arg;

    while (1) {
        xQueueReceive(cola_mensajes, &mensaje, portMAX_DELAY);
        checksum = calcular_checksum(mensaje.texto);

        uart_flush_input(UART_NUM_1);
        putchar_uart(UART_NUM_1, SF);
        puts_uart(UART_NUM_1, mensaje.texto);
        putchar_uart(UART_NUM_1, EF);
        putchar_uart(UART_NUM_1, (char)checksum);

        correcto = esperar_respuesta();
        xQueueSend(cola_resultados, &correcto, portMAX_DELAY);
    }
}

void app_main(void) {
    init_uart0();
    init_uart1();

    cola_mensajes = xQueueCreate(1, sizeof(mensaje_t));
    cola_resultados = xQueueCreate(1, sizeof(bool));

    if (cola_mensajes == NULL || cola_resultados == NULL) {
        puts_uart(UART_NUM_0, "Error al crear las colas.\r\n");
        return;
    }

    if (xTaskCreate(recepcion_mensaje_task, "Recepcion_mensaje", 4096, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(transmision_trama_task, "Transmision_trama", 4096, NULL, 5, NULL) != pdPASS) {
        puts_uart(UART_NUM_0, "Error al crear las tareas.\r\n");
    }
}
