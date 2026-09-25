#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"

#define ECHO_TEST_TXD UART_PIN_NO_CHANGE
#define ECHO_TEST_RXD UART_PIN_NO_CHANGE
#define ECHO_TEST_RTS UART_PIN_NO_CHANGE
#define ECHO_TEST_CTS UART_PIN_NO_CHANGE

#define ECHO_UART_PORT_NUM UART_NUM_0
#define ECHO_UART_BAUD_RATE 115200

#define BUF_SIZE 1024


void init_uart(){
    uart_config_t uart_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS));
}

void uart_puts(uart_port_t uart_num, char *str){
    uart_write_bytes(uart_num, str, strlen(str));
}

char uart_getchar(uart_port_t uart_num){
    char c;
    uart_read_bytes(uart_num, &c, 1, portMAX_DELAY);
    return c;
}


void uart_gets(uart_port_t uart_num, char *str, uint8_t max_len){
    int i = 0;
    uint8_t c;

    while(i < max_len - 1){
        int len = uart_read_bytes(uart_num, &c, 1, portMAX_DELAY);
        if(len > 0){
            if(c == '\r' || c == '\n'){
                uart_puts(uart_num, "\r\n");
                break;
            }
            if(c == 8 || c == 127){
                if(i > 0){
                    i--;
                    uart_puts(uart_num, "\b \b");
                }
                continue;
            }
            str[i++] = c;
            uart_write_bytes(uart_num, (const char *)&c, 1);
        }
    }
    str[i] = '\0';
}


void uart_getNum(uart_port_t uart_num, char *str, uint8_t max_len){
    int i = 0;
    uint8_t c;

    while(i < max_len - 1){
        int len = uart_read_bytes(uart_num, &c, 1, portMAX_DELAY);
        if(len > 0){
            if(c == '\r' || c == '\n'){
                uart_puts(uart_num, "\r\n");
                break;
            }
            if(c == 8 || c == 127){
                if(i > 0){
                    i--;
                    uart_puts(uart_num, "\b \b");
                }
                continue;
            }
            if(c >= '0' && c <= '9'){
                str[i++] = c;
                uart_write_bytes(uart_num, (const char *)&c, 1);
            }
        }
    }
    str[i] = '\0';
}


void uart_getAlpha(uart_port_t uart_num, char *str, uint8_t max_len){
    int i = 0;
    uint8_t c;

    while(i < max_len - 1){
        int len = uart_read_bytes(uart_num, &c, 1, portMAX_DELAY);

        if(len > 0){
            if(c == '\r' || c == '\n'){
                uart_puts(uart_num, "\r\n");
                break;
            }
            if(c == 8 || c == 127){
                if(i > 0){
                    i--;
                    uart_puts(uart_num, "\b \b");
                }
                continue;
            }
            if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' '){
                str[i++] = c;
                uart_write_bytes(uart_num, (const char *)&c, 1);
            }
        }
    }
    str[i] = '\0';
}


void uart_putchar(uart_port_t uart_num, char c){
    uart_write_bytes(uart_num, &c, 1);
}




void uart_task(void *arg){
    char c;
    char cadena[26];
    char numeros[20];
    char letras[20];

    vTaskDelay(500 / portTICK_PERIOD_MS);
    while(1){

        uart_puts(ECHO_UART_PORT_NUM, "\r\nEscribe una cadena: ");
        uart_gets(ECHO_UART_PORT_NUM, cadena, sizeof(cadena));
        uart_puts(ECHO_UART_PORT_NUM, "Cadena recibida: ");
        uart_puts(ECHO_UART_PORT_NUM, cadena);
        uart_puts(ECHO_UART_PORT_NUM, "\r\n");

        uart_puts(ECHO_UART_PORT_NUM, "Escribe solo numeros: ");
        uart_getNum(ECHO_UART_PORT_NUM, numeros, sizeof(numeros));
        uart_puts(ECHO_UART_PORT_NUM, "Numeros recibidos: ");
        uart_puts(ECHO_UART_PORT_NUM, numeros);
        uart_puts(ECHO_UART_PORT_NUM, "\r\n");

        uart_puts(ECHO_UART_PORT_NUM, "Escribe solo letras: ");
        uart_getAlpha(ECHO_UART_PORT_NUM, letras, sizeof(letras));
        uart_puts(ECHO_UART_PORT_NUM, "Letras recibidas: ");
        uart_puts(ECHO_UART_PORT_NUM, letras);
        uart_puts(ECHO_UART_PORT_NUM, "\r\n");

        uart_puts(ECHO_UART_PORT_NUM, "Escribe un caracter: ");
        c = uart_getchar(ECHO_UART_PORT_NUM);
        uart_putchar(ECHO_UART_PORT_NUM,c);
        uart_puts(ECHO_UART_PORT_NUM, "\r\nCaracter recibido: ");
        uart_putchar(ECHO_UART_PORT_NUM, c);
        uart_puts(ECHO_UART_PORT_NUM, "\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void app_main(void){
    init_uart();
    xTaskCreate(uart_task, "uart_task", 4096, NULL, 5, NULL);

}
