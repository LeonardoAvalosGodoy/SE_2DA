#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/uart.h"

//Trabajaremos con VSPI
#define PIN_MOSI 23
#define PIN_MISO 19
#define PIN_SCLK 18
#define PIN_CS 5

//Definimos el boton
#define BTN_PIN 4

//Defincion de registros del sensor bmp280
#define BMP280_ID 0xD0      //ID DEL SENSOR
#define BMP280_RESET 0xE0   //RESET DEL SENSOR
#define BMP280_CTRL_MEAS 0xF4 //CONTROL DE MEDICION
#define BMP280_CONFIG 0xF5   //CONFIGURACION DEL SENSOR(FILTRO/ESTABILIDAD)
#define BMP280_PRESS_DATA 0xF7  //INICIO DE DATOS(PRESION Y TEMPERATURA)
#define BMP280_CALIB_START 0x88  //INICIO DE CALIBRACION(AJUSTA MEDICIONES)
#define BMP280_CHIP_ID 0x58 //ID ESPERADO DEL SENSOR
#define BMP280_READ_BIT 0x80 //BIT PARA LEER DATOS EN SPI

//Tamaño del buffer para lectura de datos
#define BUF_SIZE 10

static spi_device_handle_t spi_handle;

static float buffer[BUF_SIZE];
static int idx = 0;
static int lleno = 0;

static SemaphoreHandle_t mutex; // Mutex para proteger el acceso al buffer
static QueueHandle_t btn_queue; // cola para eventos del botón

//estrucuta para la calibracion del boton
typedef struct{
    uint16_t dig_T1;
    int16_t dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2, dig_P3, dig_P4, dig_P5;
    int16_t dig_P6, dig_P7, dig_P8, dig_P9;
}bmp280_calib_t;

static bmp280_calib_t calib; // Variable global para almacenar los datos de calibración

// Una pulsacion requiere nivel bajo estable y una liberacion estable previa.
static void tarea_antirrebote(void *arg){
    int estable = 1;
    int candidato = gpio_get_level(BTN_PIN);
    TickType_t cambio = xTaskGetTickCount();
    const TickType_t filtro = pdMS_TO_TICKS(50);
    TickType_t periodo = pdMS_TO_TICKS(10);
    if (periodo == 0){
        periodo = 1;
    }

    while (1){
        int nivel = gpio_get_level(BTN_PIN);
        TickType_t ahora = xTaskGetTickCount();
        if (nivel != candidato){
            candidato = nivel;
            cambio = ahora;
        }
        if (candidato != estable && (ahora - cambio) >= filtro){
            estable = candidato;
            if (estable == 0){
                // Marca el inicio del nivel bajo que supero el filtro.
                xQueueSend(btn_queue, &cambio, 0);
            }
        }
        vTaskDelay(periodo);
    }
}

//Escribe una cadena de caracteres en el puerto UART indicado.
void puts_uart(uart_port_t uart_num, char * str){
    uart_write_bytes(uart_num, str, strlen(str));
}

static void uart_print(const char *txt){
    puts_uart(UART_NUM_0, (char *)txt);
}

// Convierte los digitos manualmente y los envia por UART.
static void uart_entero(uint32_t valor){
    char digitos[10];
    int n = 0;
    do{
        digitos[n++] = '0' + valor % 10;
        valor /= 10;
    }while (valor > 0);
    while (n > 0){
        uart_write_bytes(UART_NUM_0, &digitos[--n], 1);
    }
}

static void uart_temperatura(float valor){
    int32_t centesimas = (int32_t)(valor * 100.0f + (valor < 0 ? -0.5f : 0.5f));
    if (centesimas < 0){
        uart_print("-");
        centesimas = -centesimas;
    }
    uart_entero((uint32_t)centesimas / 100);
    uart_print(".");
    if (centesimas % 100 < 10){
        uart_print("0");
    }
    uart_entero((uint32_t)centesimas % 100);
    uart_print(" C\n");
}

void init_gpio(void){
    gpio_reset_pin(BTN_PIN);
    gpio_set_direction(BTN_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(BTN_PIN);      
    gpio_pulldown_dis(BTN_PIN);    
    gpio_set_intr_type(BTN_PIN, GPIO_INTR_DISABLE);
}

//configuracion uart0 para comunicacion serial
void init_uart0(void){
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
}

//configuracion spi
void init_spi(void){
    spi_bus_config_t bus = {
        .miso_io_num = PIN_MISO,
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 1000000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 16,
    };
    spi_bus_add_device(SPI3_HOST, &dev, &spi_handle);
}

//lee un registro del sensor
static uint8_t bmp280_read_reg(uint8_t reg)
{
    uint8_t tx[2] = { reg | BMP280_READ_BIT, 0 };
    uint8_t rx[2] = { 0 };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    if (spi_device_transmit(spi_handle, &t) != ESP_OK){
        return 0;
    }
    return rx[1];
}

//lee varios registros del sensor
static bool bmp280_read_regs(uint8_t reg, uint8_t *data, size_t len){
    uint8_t tx[len + 1];
    uint8_t rx[len + 1];

    for(int i = 0; i < len + 1; i++){
        tx[i] = 0;
    }

    tx[0] = reg | BMP280_READ_BIT;
    spi_transaction_t t = {
        .length    = (len + 1) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    if (spi_device_transmit(spi_handle, &t) != ESP_OK){
        return false;
    }
    for(int i = 0; i < len; i++){
        data[i] = rx[i+1];
    }
    return true;
}

//escribe el registro del sensor
static void bmp280_write_reg(uint8_t reg, uint8_t value){
    uint8_t tx[2] = { reg & ~BMP280_READ_BIT, value };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
    };
    spi_device_transmit(spi_handle, &t);
}

//inicializacion del sensor
static bool bmp280_init(void){
    uint8_t id = bmp280_read_reg(BMP280_ID);

    //verificamos id
    if (id != BMP280_CHIP_ID){
        uart_print("Error: BMP280 no encontrado\n");
        return false;
    }

    //reseteamos el sensor
    bmp280_write_reg(BMP280_RESET, 0xB6);
    vTaskDelay(pdMS_TO_TICKS(100));

    //leemos los datos de calibracion
    uint8_t raw[24];
    if (!bmp280_read_regs(BMP280_CALIB_START, raw, 24)){
        return false;
    }
    //guarda datos de calibracion
    calib.dig_T1 = (raw[1]  << 8) | raw[0];
    calib.dig_T2 = (raw[3]  << 8) | raw[2];
    calib.dig_T3 = (raw[5]  << 8) | raw[4];
    calib.dig_P1 = (raw[7]  << 8) | raw[6];
    calib.dig_P2 = (raw[9]  << 8) | raw[8];
    calib.dig_P3 = (raw[11] << 8) | raw[10];
    calib.dig_P4 = (raw[13] << 8) | raw[12];
    calib.dig_P5 = (raw[15] << 8) | raw[14];
    calib.dig_P6 = (raw[17] << 8) | raw[16];
    calib.dig_P7 = (raw[19] << 8) | raw[18];
    calib.dig_P8 = (raw[21] << 8) | raw[20];
    calib.dig_P9 = (raw[23] << 8) | raw[22];

    bmp280_write_reg(BMP280_CTRL_MEAS, 0x57);
    bmp280_write_reg(BMP280_CONFIG,    0x10);
    return true;
}

static void init_bmp280(void){
    if (!bmp280_init()){
        uart_print("Error: iniciando BMP280\n");
        esp_restart();
    }
}

static float compensar_temperatura(int32_t adc_T, int32_t *t_fine){
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)calib.dig_T1 << 1))) * calib.dig_T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - calib.dig_T1) * ((adc_T >> 4) - calib.dig_T1)) >> 12) * calib.dig_T3) >> 14;
    *t_fine = var1 + var2;
    return ((*t_fine * 5 + 128) >> 8) / 100.0f;
}

static float compensar_presion(int32_t adc_P, int32_t t_fine){
    int64_t var1 = (int64_t)t_fine - 128000;
    int64_t var2 = var1 * var1 * calib.dig_P6;
    var2 += (var1 * calib.dig_P5) << 17;
    var2 += ((int64_t)calib.dig_P4) << 35;
    var1  = ((var1 * var1 * calib.dig_P3) >> 8) + ((var1 * calib.dig_P2) << 12);
    var1  = (((((int64_t)1) << 47) + var1) * calib.dig_P1) >> 33;
    if (var1 == 0){
        return 0;
    }
    int64_t p = 1048576 - adc_P;
    p = ((p << 31) - var2) * 3125 / var1;
    var1 = (calib.dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = (calib.dig_P8 * p) >> 19;
    p    = ((p + var1 + var2) >> 8) + ((int64_t)calib.dig_P7 << 4);
    return p / 25600.0f;
}

static bool bmp280_leer(float *temp, float *press){
    // SPI puede completar una transferencia aunque el sensor este desconectado.
    // Comprobamos tambien su identificador y los registros configurados.
    if (bmp280_read_reg(BMP280_ID) != BMP280_CHIP_ID ||
        bmp280_read_reg(BMP280_CTRL_MEAS) != 0x57 ||
        bmp280_read_reg(BMP280_CONFIG) != 0x10){
        return false;
    }
    uint8_t d[6];
    if (!bmp280_read_regs(BMP280_PRESS_DATA, d, 6)){
        return false;
    }
    bool todos_cero = true;
    bool todos_uno = true;
    for (int i = 0; i < 6; i++){
        todos_cero = todos_cero && d[i] == 0;
        todos_uno = todos_uno && d[i] == 0xFF;
    }
    if (todos_cero || todos_uno){
        return false;
    }
    int32_t adc_P = (d[0] << 12) | (d[1] << 4) | (d[2] >> 4);
    int32_t adc_T = (d[3] << 12) | (d[4] << 4) | (d[5] >> 4);
    int32_t t_fine;
    *temp  = compensar_temperatura(adc_T, &t_fine);
    *press = compensar_presion(adc_P, t_fine);
    return true;
}

static void imprimir(void){
    int n = lleno;
    if(lleno){
        n = BUF_SIZE;
    }else{
        n = idx;
    }

    if (n == 0){
        uart_print("Sin datos\n");
        return;
    }

    uart_print("-- Ultimas mediciones --\n");
    for(int i = 0; i < n; i++){
        int pos;
        if(lleno){
            pos = (idx + i) % BUF_SIZE;
        }else{
            pos = i;
        }
        uart_print("  [");
        uart_entero(i + 1);
        uart_print("] ");
        uart_temperatura(buffer[pos]);
    }
}

static float media(void)
{
    float s = 0;
    int n = lleno;
    if(lleno){
        n = BUF_SIZE;
    }else{
        n = idx;
    }
    for (int i = 0; i < n; i++){
        s += buffer[i];
    }
    return s / n;
}

static float mediana(void)
{
    float t[BUF_SIZE];
    int n = lleno;
    if(lleno){
        n = BUF_SIZE;
    }else{
        n = idx;
    }
    for (int i = 0; i < n; i++){
        t[i] = buffer[i];
    }
    for (int i = 0; i < n - 1; i++){
        for (int j = i + 1; j < n; j++){
            if (t[i] > t[j]){
                float a = t[i];
                t[i] = t[j];
                t[j] = a;
            }
        }
    }
    float resultado;
    if (n % 2 == 0){
        resultado = (t[n/2] + t[n/2 - 1]) / 2.0f;
    }else{
        resultado = t[n/2];
    }
    return resultado;
}

static void tarea_sensor(void *arg)
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(2000)); // esperar 2 segundos entre lecturas
        float temp, press;
        if (!bmp280_leer(&temp, &press)){
            uart_print("Error: no se pudo leer el BMP280. Revise las conexiones SPI\n");
            continue; // no guardar la lectura fallida
        }

        xSemaphoreTake(mutex, portMAX_DELAY);
        buffer[idx] = temp;
        idx = (idx + 1) % BUF_SIZE;
        if (idx == 0){
            lleno = 1;
        }
        xSemaphoreGive(mutex);

    }
}

static void tarea_boton(void *arg){
    TickType_t primer_tick, segundo_tick;
    const TickType_t ventana = pdMS_TO_TICKS(1500);

    while (1){
        if (xQueueReceive(btn_queue, &primer_tick, portMAX_DELAY) != pdTRUE){
            continue;
        }

        bool doble_click = false;
        TickType_t transcurrido = xTaskGetTickCount() - primer_tick;
        while (transcurrido < ventana){
            if (xQueueReceive(btn_queue, &segundo_tick,
                              ventana - transcurrido) != pdTRUE){
                break;
            }
            TickType_t diferencia = segundo_tick - primer_tick;
            if (diferencia >= pdMS_TO_TICKS(50) && diferencia <= ventana){
                doble_click = true;
                break;
            }
            transcurrido = xTaskGetTickCount() - primer_tick;
        }

        xSemaphoreTake(mutex, portMAX_DELAY);
        if (!lleno && idx == 0){
            uart_print("Sin datos\n");
        }else if (!doble_click){
            imprimir();
        }else{
            uart_print("-- Estadisticas --\n");
            uart_print("  Media:   ");
            uart_temperatura(media());
            uart_print("  Mediana: ");
            uart_temperatura(mediana());
        }
        xSemaphoreGive(mutex);
    }
}

void app_main(void)
{
    init_uart0();  
    init_spi();

    btn_queue = xQueueCreate(10, sizeof(TickType_t)); 
    mutex     = xSemaphoreCreateMutex();
    if (btn_queue == NULL || mutex == NULL){
        return;
    }
    init_gpio();

    init_bmp280();

    xTaskCreate(tarea_sensor, "sensor", 4096, NULL, 2, NULL);
    xTaskCreate(tarea_boton,  "boton",  4096, NULL, 1, NULL);
    xTaskCreate(tarea_antirrebote, "antirrebote", 2048, NULL, 2, NULL);
}
