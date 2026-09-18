#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "string.h"

#define BUF_SIZE0 (1024)
#define BUF_SIZE1 (1024)

// Caracteres de control para la trama y respuestas
#define SF '#'
#define EF '$'
#define ACK 0x06
#define NACK 0x15

// Cola para comunicar la tarea de recepción de teclado con la de transmisión
QueueHandle_t cola_mensajes;

// --- FUNCIONES DE LA BIBLIOTECA (Parte 1) ---
char uart_getchar(uart_port_t uart_num){
    uint8_t data;
    int len = uart_read_bytes(uart_num, &data, 1, portMAX_DELAY);
    return (len > 0) ? (char)data : 0;
}

void uart_putchar(uart_port_t uart_num, char c){
    uart_write_bytes(uart_num, &c, 1);
}

void uart_puts(uart_port_t uart_num, char * str){
    uart_write_bytes(uart_num, str, strlen(str));
}

void uart_gets(uart_port_t uart_num, char *str, uint8_t max_len){
    int i = 0; char c;
    while(i < max_len - 1){
        c = uart_getchar(uart_num);
        if(c == '\r' || c == '\n') break;
        uart_putchar(uart_num, c);
        str[i++] = c;
    }
    str[i] = '\0';
}

void uart_getNum(uart_port_t uart_num, char * str, uint8_t max_len){
    int i = 0; char c;
    while(i < max_len - 1){
        c = uart_getchar(uart_num);
        if(c == '\r' || c == '\n') break;
        if(c >= '0' && c <= '9'){
            uart_putchar(uart_num, c);
            str[i++] = c;
        }
    }
    str[i] = '\0';
}

void uart_getAlpha(uart_port_t uart_num, char *str, uint8_t max_len){
    int i = 0; char c;
    while(i < max_len - 1){
        c = uart_getchar(uart_num);
        if(c == '\r' || c == '\n') break;
        if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')){
            uart_putchar(uart_num, c);
            str[i++] = c;
        }
    }
    str[i] = '\0';
}

// Función principal de validación para la pancarta
void uart_getPancarta(uart_port_t uart_num, char *str, uint8_t max_len){
    int i = 0;
    char c;
    while(i < max_len - 1){
        c = uart_getchar(uart_num);
        if(c == '\r' || c == '\n'){ break; }
        if(c == 8 || c == 127){ // Backspace o Delete
            if (i > 0){ 
                i--; 
                uart_puts(uart_num, "\b \b"); 
            }
            continue;
        }
        if ((c >= 'A' && c <='Z') || (c >= 'a' && c <= 'z') || 
            (c >= '0' && c <= '9') || (c == ' ') || 
            (c == '!') || (c == '.') || (c == '+') || (c == '-')) {
                uart_putchar(uart_num, c); 
                str[i++] = c;
        }
    }
    str[i] = '\0';
}
// ---------------------------------------------

void init_uarts(void){
    uart_config_t cfg = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    // UART0 para terminal
    uart_driver_install(UART_NUM_0, BUF_SIZE0 * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &cfg);
    uart_set_pin(UART_NUM_0, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // UART1 para comunicación con ESP32 2
    uart_driver_install(UART_NUM_1, BUF_SIZE1 * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &cfg);
    uart_set_pin(UART_NUM_1, 4, 5, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// Tarea 1: Lee caracteres y valida longitud
void tarea_recepcion_mensaje(void *arg){
    char buffer[26];
    while(1){
        uart_puts(UART_NUM_0, "\nIngrese un texto (max 25 caracteres): ");
        uart_getPancarta(UART_NUM_0, buffer, sizeof(buffer));
        uart_puts(UART_NUM_0, "\nEnviando trama...\n");
        // Envía el mensaje verificado a la tarea de transmisión
        xQueueSend(cola_mensajes, buffer, portMAX_DELAY);
    }
}

// Tarea 2: Calcula Checksum, arma trama, y espera respuesta
void tarea_transmision_trama(void *arg){
    char mensaje[26];
    uint8_t trama[50];
    
    while(1){
        if(xQueueReceive(cola_mensajes, mensaje, portMAX_DELAY)){
            int len = strlen(mensaje);
            uint8_t checksum = 0;
            int idx = 0;
            
            trama[idx++] = SF; // Inicio de trama
            
            for(int i = 0; i < len; i++){
                trama[idx++] = mensaje[i];
                checksum ^= mensaje[i]; // Cálculo de XOR
            }
            
            trama[idx++] = EF; // Fin de trama
            trama[idx++] = checksum; // Checksum al final
            
            uart_write_bytes(UART_NUM_1, trama, idx);
            
            // Esperar respuesta (ACK/NACK)
            uint8_t rx_byte;
            int rx_len = uart_read_bytes(UART_NUM_1, &rx_byte, 1, pdMS_TO_TICKS(1000));
            
            if(rx_len > 0){
                if(rx_byte == ACK){
                    uart_puts(UART_NUM_0, "Mensaje enviado correctamente.\n");
                } else if(rx_byte == NACK) {
                    uart_puts(UART_NUM_0, "Error al enviar mensaje (NACK).\n");
                }
            } else {
                uart_puts(UART_NUM_0, "Error al enviar mensaje (Timeout).\n");
            }
        }
    }
}

void app_main(void){
    init_uarts();
    cola_mensajes = xQueueCreate(5, sizeof(char) * 26);
    
    xTaskCreate(tarea_recepcion_mensaje, "rx_msg", 4096, NULL, 5, NULL);
    xTaskCreate(tarea_transmision_trama, "tx_trama", 4096, NULL, 5, NULL);
}
