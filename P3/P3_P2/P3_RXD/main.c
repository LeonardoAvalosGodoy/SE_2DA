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

// UART0: terminal. UART2: comunicacion con el ESP32 1.
// UART2: RXD = GPIO16, TXD = GPIO17.

// Fuente ASCII de 5 filas por 5 columnas.
static const char font[][5][6] = {
    // Letras mayusculas
    // A (0)
    {
        " *** ",
        "*   *",
        "*****",
        "*   *",
        "*   *"
    },
    // B (1)
    {
        "**** ",
        "*   *",
        "**** ",
        "*   *",
        "**** "
    },
    // C (2)
    {
        " ****",
        "*    ",
        "*    ",
        "*    ",
        " ****"
    },
    // D (3)
    {
        "**** ",
        "*   *",
        "*   *",
        "*   *",
        "**** "
    },
    // E (4)
    {
        "*****",
        "*    ",
        "***  ",
        "*    ",
        "*****"
    },
    // F (5)
    {
        "*****",
        "*    ",
        "***  ",
        "*    ",
        "*    "
    },
    // G (6)
    {
        " ****",
        "*    ",
        "*  **",
        "*   *",
        " ****"
    },
    // H (7)
    {
        "*   *",
        "*   *",
        "*****",
        "*   *",
        "*   *"
    },
    // I (8)
    {
        " *** ",
        "  *  ",
        "  *  ",
        "  *  ",
        " *** "
    },
    // J (9)
    {
        "  ***",
        "    *",
        "    *",
        "*   *",
        " *** "
    },
    // K (10)
    {
        "*   *",
        "*  * ",
        "***  ",
        "*  * ",
        "*   *"
    },
    // L (11)
    {
        "*    ",
        "*    ",
        "*    ",
        "*    ",
        "*****"
    },
    // M (12)
    {
        "*   *",
        "** **",
        "* * *",
        "*   *",
        "*   *"
    },
    // N (13)
    {
        "*   *",
        "**  *",
        "* * *",
        "*  **",
        "*   *"
    },
    // O (14)
    {
        " *** ",
        "*   *",
        "*   *",
        "*   *",
        " *** "
    },
    // P (15)
    {
        "**** ",
        "*   *",
        "**** ",
        "*    ",
        "*    "
    },
    // Q (16)
    {
        " *** ",
        "*   *",
        "*   *",
        "*  **",
        " ****"
    },
    // R (17)
    {
        "**** ",
        "*   *",
        "**** ",
        "*  * ",
        "*   *"
    },
    // S (18)
    {
        " ****",
        "*    ",
        " *** ",
        "    *",
        "**** "
    },
    // T (19)
    {
        "*****",
        "  *  ",
        "  *  ",
        "  *  ",
        "  *  "
    },
    // U (20)
    {
        "*   *",
        "*   *",
        "*   *",
        "*   *",
        " *** "
    },
    // V (21)
    {
        "*   *",
        "*   *",
        "*   *",
        " * * ",
        "  *  "
    },
    // W (22)
    {
        "*   *",
        "*   *",
        "* * *",
        "** **",
        "*   *"
    },
    // X (23)
    {
        "*   *",
        " * * ",
        "  *  ",
        " * * ",
        "*   *"
    },
    // Y (24)
    {
        "*   *",
        " * * ",
        "  *  ",
        "  *  ",
        "  *  "
    },
    // Z (25)
    {
        "*****",
        "   * ",
        "  *  ",
        " *   ",
        "*****"
    },
    // Letras minusculas.
    // a (26)
    {
        "     ",
        " *** ",
        "*  * ",
        "* ** ",
        " ** *"
    },
    // b (27)
    {
        "*    ",
        "*    ",
        "**** ",
        "*   *",
        "**** "
    },
    // c (28)
    {
        "     ",
        " *** ",
        "*    ",
        "*    ",
        " *** "
    },
    // d (29)
    {
        "    *",
        "    *",
        " ****",
        "*   *",
        " ****"
    },
    // e (30)
    {
        "     ",
        " *** ",
        "*****",
        "*    ",
        " *** "
    },
    // f (31)
    {
        "  ** ",
        " *   ",
        "***  ",
        " *   ",
        " *   "
    },
    // g (32)
    {
        "     ",
        " ****",
        "*   *",
        " ****",
        " *** "
    },
    // h (33)
    {
        "*    ",
        "*    ",
        "**** ",
        "*   *",
        "*   *"
    },
    // i (34)
    {
        "  *  ",
        "     ",
        " **  ",
        "  *  ",
        " *** "
    },
    // j (35)
    {
        "   * ",
        "     ",
        "   * ",
        "   * ",
        " **  "
    },
    // k (36)
    {
        "*    ",
        "*  * ",
        "***  ",
        "*  * ",
        "*   *"
    },
    // l (37)
    {
        " **  ",
        "  *  ",
        "  *  ",
        "  *  ",
        " *** "
    },
    // m (38)
    {
        "     ",
        "** * ",
        "* * *",
        "*   *",
        "*   *"
    },
    // n (39)
    {
        "     ",
        "**** ",
        "*   *",
        "*   *",
        "*   *"
    },
    // o (40)
    {
        "     ",
        " *** ",
        "*   *",
        "*   *",
        " *** "
    },
    // p (41)
    {
        "     ",
        "**** ",
        "*   *",
        "**** ",
        "*    "
    },
    // q (42)
    {
        "     ",
        " ****",
        "*   *",
        " ****",
        "    *"
    },
    // r (43)
    {
        "     ",
        " *** ",
        "*    ",
        "*    ",
        "*    "
    },
    // s (44)
    {
        "     ",
        " *** ",
        " **  ",
        "  ** ",
        " *** "
    },
    // t (45)
    {
        " *   ",
        "***  ",
        " *   ",
        " *   ",
        "  ** "
    },
    // u (46)
    {
        "     ",
        "*   *",
        "*   *",
        "*   *",
        " ****"
    },
    // v (47)
    {
        "     ",
        "*   *",
        "*   *",
        " * * ",
        "  *  "
    },
    // w (48)
    {
        "     ",
        "*   *",
        "* * *",
        "* * *",
        " * * "
    },
    // x (49)
    {
        "     ",
        "*   *",
        " * * ",
        " * * ",
        "*   *"
    },
    // y (50)
    {
        "     ",
        "*   *",
        "*   *",
        " ****",
        "    *"
    },
    // z (51)
    {
        "     ",
        "*****",
        "  *  ",
        " *   ",
        "*****"
    },
    // Numeros.
    // 0 (52)
    {
        " *** ",
        "*  **",
        "* * *",
        "**  *",
        " *** "
    },
    // 1 (53)
    {
        "  *  ",
        " **  ",
        "  *  ",
        "  *  ",
        " *** "
    },
    // 2 (54)
    {
        " *** ",
        "*   *",
        "  ** ",
        " *   ",
        "*****"
    },
    // 3 (55)
    {
        " *** ",
        "    *",
        "  ** ",
        "    *",
        " *** "
    },
    // 4 (56)
    {
        "*  * ",
        "*  * ",
        "*****",
        "   * ",
        "   * "
    },
    // 5 (57)
    {
        "*****",
        "*    ",
        "**** ",
        "    *",
        "**** "
    },
    // 6 (58)
    {
        " *** ",
        "*    ",
        "**** ",
        "*   *",
        " *** "
    },
    // 7 (59)
    {
        "*****",
        "    *",
        "   * ",
        "  *  ",
        "  *  "
    },
    // 8 (60)
    {
        " *** ",
        "*   *",
        " *** ",
        "*   *",
        " *** "
    },
    // 9 (61)
    {
        " *** ",
        "*   *",
        " ****",
        "    *",
        " *** "
    },
    // Caracteres especiales.
    // espacio (62)
    {
        "     ",
        "     ",
        "     ",
        "     ",
        "     "
    },
    // ! (63)
    {
        "  *  ",
        "  *  ",
        "  *  ",
        "     ",
        "  *  "
    },
    // . (64)
    {
        "     ",
        "     ",
        "     ",
        "     ",
        "  *  "
    },
    // + (65)
    {
        "     ",
        "  *  ",
        "*****",
        "  *  ",
        "     "
    },
    // - (66)
    {
        "     ",
        "     ",
        "*****",
        "     ",
        "     "
    },
    // , (67)
    {
        "     ",
        "     ",
        "     ",
        "  *  ",
        " *   "
    },
};

// Buscar el indice correspondiente a un caracter de la pancarta.
int id_caracter(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }

    if (c >= 'a' && c <= 'z') {
        return 26 + (c - 'a');
    }

    if (c >= '0' && c <= '9') {
        return 52 + (c - '0');
    }

    if (c == ' ') {
        return 62;
    }

    if (c == '!') {
        return 63;
    }

    if (c == '.') {
        return 64;
    }

    if (c == '+') {
        return 65;
    }

    if (c == '-') {
        return 66;
    }

    if (c == ',') {
        return 67;
    }

    return -1;
}

// Estructura que recibe la segunda tarea por medio de la cola.
typedef struct {
    char mensaje[MAX_TEXTO + 1];
    uint8_t checksum_recibido;
    bool formato_valido;
} trama_t;

static QueueHandle_t cola_tramas;

// Enviar un caracter.
void putchar_uart(uart_port_t uart_num, char c) {
    uart_write_bytes(uart_num, &c, 1);
}

// Enviar una cadena.
void puts_uart(uart_port_t uart_num, const char *str) {
    uart_write_bytes(uart_num, str, strlen(str));
}

// Imprimir la pancarta por filas para que los caracteres aparezcan juntos.
void imprimir_pancarta(uart_port_t uart_num, const char *str) {
    size_t longitud = strlen(str);

    for (int fila = 0; fila < 5; fila++) {
        for (size_t i = 0; i < longitud; i++) {
            int indice = id_caracter(str[i]);

            if (indice >= 0) {
                puts_uart(uart_num, font[indice][fila]);
                putchar_uart(uart_num, ' ');
            }
        }

        puts_uart(uart_num, "\r\n");
    }
}

// Inicializar UART0 para la terminal.
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

// Inicializar UART2 para comunicarse con el ESP32 1.
void init_uart2(void) {
    uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, BUF_SIZE1 * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

// Calcular checksum con XOR.
uint8_t calcular_checksum(const char *mensaje) {
    uint8_t checksum = 0;

    for (int i = 0; mensaje[i] != '\0'; i++) {
        checksum ^= (uint8_t)mensaje[i];
    }

    return checksum;
}

// Tarea 1: recibir la trama y comprobar su formato.
void recepcion_trama_task(void *arg) {
    trama_t trama;
    uint8_t c;
    (void)arg;

    while (1) {
        if (uart_read_bytes(UART_NUM_2, &c, 1, portMAX_DELAY) != 1) {
            continue;
        }

        memset(&trama, 0, sizeof(trama));
        trama.formato_valido = (c == SF);

        if (!trama.formato_valido) {
            xQueueSend(cola_tramas, &trama, portMAX_DELAY);
            continue;
        }

        size_t i = 0;
        bool fin = false;

        while (1) {
            if (uart_read_bytes(UART_NUM_2, &c, 1, pdMS_TO_TICKS(500)) != 1) {
                trama.formato_valido = false;
                break;
            }

            if (c == EF) {
                fin = true;
                break;
            }

            if (id_caracter((char)c) < 0 || i >= MAX_TEXTO) {
                trama.formato_valido = false;
            } else if (trama.formato_valido) {
                trama.mensaje[i] = (char)c;
                i++;
            }
        }

        trama.mensaje[i] = '\0';

        if (fin) {
            // El checksum es un solo byte; tambien puede valer 0x00.
            if (uart_read_bytes(UART_NUM_2, &c, 1, pdMS_TO_TICKS(500)) == 1) {
                trama.checksum_recibido = c;
            } else {
                trama.formato_valido = false;
            }
        }

        xQueueSend(cola_tramas, &trama, portMAX_DELAY);
    }
}

// Tarea 2: verificar checksum, responder ACK/NACK y mostrar la pancarta.
void transmision_pancarta_task(void *arg) {
    trama_t trama;
    bool correcto;
    (void)arg;

    while (1) {
        xQueueReceive(cola_tramas, &trama, portMAX_DELAY);
        correcto = trama.formato_valido &&
                   calcular_checksum(trama.mensaje) == trama.checksum_recibido;

        if (!correcto) {
            puts_uart(UART_NUM_2, "NACK\n");
            continue;
        }

        puts_uart(UART_NUM_2, "ACK\n");
        puts_uart(UART_NUM_0, "Pancarta recibida:\r\n");
        imprimir_pancarta(UART_NUM_0, trama.mensaje);
    }
}

void app_main(void) {
    init_uart0();
    init_uart2();

    cola_tramas = xQueueCreate(2, sizeof(trama_t));

    if (cola_tramas == NULL) {
        puts_uart(UART_NUM_0, "Error al crear la cola.\r\n");
        return;
    }

    if (xTaskCreate(recepcion_trama_task, "Recepcion_trama", 4096, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(transmision_pancarta_task, "Transmision_pancarta", 4096, NULL, 5, NULL) != pdPASS) {
        puts_uart(UART_NUM_0, "Error al crear las tareas.\r\n");
    }
}
