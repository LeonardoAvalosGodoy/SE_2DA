#ifndef uart_funciones
#define uart_funciones

#include <stdint.h>
#include "driver/uart.h"

//leer un solo caracter
char uart_getchar(uart_port_t uart_num);

//recibe una cadena
void uart_gets(uart_port_t uart_num,char *str,uint8_t max_len);

//recibe una cadena de solo numeros
void uart_getNum(uart_port_t uart_num, char *str,uint8_t max_len);

//recibe una cadena de solo letras
void uart_getAlpha(uart_port_t uart_num,char *str,uint8_t max_len);

//envia un solo caracter
void uart_putchar(uart_port_t uart_num,char c);

//envia una cadena completa
void uart_puts(uart_port_t,char *str);

#endif
