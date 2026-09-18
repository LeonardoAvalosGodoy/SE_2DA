#include <stdio.h>
    #include <stdlib.h>
    #include <freertos/FreeRTOS.h>
    #include <freertos/task.h>
    #include <freertos/queue.h>
    #include <freertos/semphr.h>
    #include <freertos/event_groups.h>
    #include <driver/gpio.h>
    #include "esp_timer.h"
    #include "esp_random.h"


    int leds[] = {32,33,25,26,27};
    int botones[] = {18,19,21,23};

    QueueHandle_t handlerQueue;
    QueueHandle_t entrada_cola;
    SemaphoreHandle_t mutex_juego;
    EventGroupHandle_t eventos_juego;
    const int JUEGO_ACTIVO_BIT = BIT0;

    typedef enum{
    mov_izq,
    mov_der,
    disparar,
    detener_juego
    } mecanica;

    uint64_t time_aux = 0;
    uint64_t time_enemigos = 0;
    uint64_t time_movimiento_enemigos = 0;
    uint64_t time_dispara_enemigo = 0;

    int segundos = 0;
    int posicion_actual = 12;
    int vidas = 3;
    int puntuacion = 0;
    int puntuacion_maxima = 0;
    int estado_juego = 0;

    int proyectil_x[5]; //posicion de anchura
    int proyectil_y[5]; //posicion de largo
    int proyectil_activo[5] ={0,0,0,0,0};

    int proyectil_x_enemigo;
    int proyectil_y_enemigo;
    int proyectil_activo_enemigo = 0;

    int enemigo_x[5];
    int enemigo_y[5];
    int enemigo_activo[5]= {0,0,0,0,0};



    static void IRAM_ATTR gpio_interrupt_handler(void *args){
    int pinNumber = (int)args;
    uint64_t time = esp_timer_get_time() / 1000;
    if(time - time_aux > 250){
        time_aux = time;
        xQueueSendFromISR(handlerQueue,&pinNumber,NULL);
    }
    }

    void init_gpio(){
    for(int i = 0; i < 5; i++){
        gpio_reset_pin(leds[i]);
        gpio_set_level(leds[i],0);
        gpio_set_direction(leds[i],GPIO_MODE_OUTPUT);
    }

    for(int i = 0; i < 4; i++){
        gpio_reset_pin(botones[i]);
        gpio_set_direction(botones[i],GPIO_MODE_INPUT);
        gpio_pulldown_en(botones[i]);
        gpio_pullup_dis(botones[i]);

        gpio_set_intr_type(botones[i],GPIO_INTR_POSEDGE);
        gpio_isr_handler_add(botones[i],gpio_interrupt_handler,(void *)botones[i]);
    }
    }

    void tiempo_jugado(){
        static uint64_t tiempo_anterior = 0;

        uint64_t tiempo_actual = esp_timer_get_time() / 1000000;

        if(tiempo_actual > tiempo_anterior){
            tiempo_anterior = tiempo_actual;
            segundos++;
        }
    }

    void leds_bin(int valor){
        for(int i = 0; i < 5; i++){
            gpio_set_level(leds[i],(valor >> i) & 1);
        }
    }

    void entrada(void *args){
    int pinNumber;
    mecanica evento;
    while(1){
        if(xQueueReceive(handlerQueue,&pinNumber,portMAX_DELAY)){
        switch(pinNumber){
            case 18:
            evento = mov_izq;
            xQueueSend(entrada_cola,&evento,portMAX_DELAY);
            break;
        
            case 19:
            evento = mov_der;
            xQueueSend(entrada_cola,&evento,portMAX_DELAY);
            break;

            case 21:
            evento = disparar;
            xQueueSend(entrada_cola,&evento,portMAX_DELAY);
            break;

            case 23:
            evento = detener_juego;
            xQueueSend(entrada_cola,&evento,portMAX_DELAY);
            break;

            default:
            break;
        }
        }
    }
    }


    void juego(void *args){
    mecanica evento;
    while(1){

        bool hay_evento = xQueueReceive(entrada_cola,&evento,150 / portTICK_PERIOD_MS);

        if(xSemaphoreTake(mutex_juego, portMAX_DELAY) == pdTRUE){

            if(vidas <= 0 && estado_juego == 0){
                estado_juego = 1;
                xEventGroupClearBits(eventos_juego, JUEGO_ACTIVO_BIT);
                if(puntuacion > puntuacion_maxima){
                    puntuacion_maxima = puntuacion;
                }
            }

            if(hay_evento){
                if(estado_juego != 0){
                    if(evento == detener_juego){
                        vidas = 3;
                        puntuacion = 0;
                        posicion_actual = 12;
                        segundos = 0;

                        proyectil_activo_enemigo = 0;

                        for(int i = 0; i < 5; i++){
                            enemigo_activo[i] = 0;
                            proyectil_activo[i] = 0;
                        }
                        estado_juego = 0;
                        xEventGroupSetBits(eventos_juego, JUEGO_ACTIVO_BIT);
                    }
                } else {
                    switch(evento){
                        case mov_izq:
                        if(posicion_actual > 0){
                            posicion_actual--;
                        }
                        break;

                        case mov_der:
                        if(posicion_actual < 25){
                            posicion_actual++;
                        }
                        break;

                        case disparar:
                        for(int p = 0; p < 5; p++){
                            if(proyectil_activo[p] == 0){
                                proyectil_x[p] = posicion_actual;
                                proyectil_y[p] = 10;
                                proyectil_activo[p] = 1;
                                break;
                            }
                        }
                        break;

                        case detener_juego:
                        estado_juego = 2;
                        xEventGroupClearBits(eventos_juego, JUEGO_ACTIVO_BIT);
                        if(puntuacion > puntuacion_maxima){
                            puntuacion_maxima = puntuacion;
                        }
                        break;
                    }
                }
            }

            if(estado_juego == 0){

                for(int p = 0; p < 5; p++){
                    if(proyectil_activo[p] == 1){
                        proyectil_y[p]--;
                        
                        if(proyectil_y[p] < 0){
                            proyectil_activo[p] = 0;
                            continue;
                        }

                        for(int i = 0; i < 5; i++){
                            if(enemigo_activo[i] == 1 && proyectil_x[p] == enemigo_x[i] && (proyectil_y[p] == enemigo_y[i] || proyectil_y[p] == enemigo_y[i] - 1)){
                                enemigo_activo[i] = 0;
                                proyectil_activo[p] = 0;
                                puntuacion++;
                                break;
                            }
                        }
                        
                        // Colisión con el proyectil enemigo
                        if(proyectil_activo_enemigo == 1 && proyectil_x[p] == proyectil_x_enemigo){
                            if(proyectil_y[p] == proyectil_y_enemigo || proyectil_y[p] == proyectil_y_enemigo - 1 || proyectil_y[p] == proyectil_y_enemigo + 1){
                                proyectil_activo[p] = 0;
                                proyectil_activo_enemigo = 0;
                            }
                        }
                    }
                }

                uint64_t tiempo_actual = esp_timer_get_time() / 1000;

                if(tiempo_actual - time_movimiento_enemigos > 300){
                    time_movimiento_enemigos = tiempo_actual;

                    for(int i = 0; i < 5; i++){
                        if(enemigo_activo[i] == 1){
                            enemigo_y[i]++;

                            if(enemigo_y[i] > 10){
                                enemigo_activo[i] = 0;
                            }
                        }
                    }
                    for(int i = 0; i < 5; i++){
                        if(enemigo_activo[i] == 1 && enemigo_x[i] == posicion_actual && enemigo_y[i] == 10){
                            enemigo_activo[i] = 0;
                            if(vidas > 0){
                                vidas--;
                            }
                            posicion_actual = 12;
                            break;
                        }
                    }
                }

                if(tiempo_actual - time_dispara_enemigo > 800){
                    time_dispara_enemigo = tiempo_actual;

                    int enemigo_eligido = esp_random() % 5;
                    if(enemigo_activo[enemigo_eligido] == 1 && proyectil_activo_enemigo == 0){
                        proyectil_x_enemigo = enemigo_x[enemigo_eligido];
                        proyectil_y_enemigo = enemigo_y[enemigo_eligido];
                        proyectil_activo_enemigo = 1;
                    }
                }

                if(proyectil_activo_enemigo == 1 ){
                    proyectil_y_enemigo++;

                    if(proyectil_y_enemigo > 10){
                        proyectil_activo_enemigo = 0;
                    }
                    if(proyectil_activo_enemigo == 1 && proyectil_x_enemigo == posicion_actual && proyectil_y_enemigo == 10){
                        proyectil_activo_enemigo = 0;
                        if(vidas > 0){
                            vidas--;
                        }
                        posicion_actual = 12;
                    }
                }
            }

            xSemaphoreGive(mutex_juego);
        }
    }
    }

    void enemigos(void *args){
        while(1){
            xEventGroupWaitBits(eventos_juego, JUEGO_ACTIVO_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

            uint64_t time_actual = esp_timer_get_time() / 1000;
            if(time_actual - time_enemigos > 250){
                time_enemigos = time_actual;

                if(xSemaphoreTake(mutex_juego, portMAX_DELAY) == pdTRUE){
                    for(int i = 0; i < 5; i++){
                        if(enemigo_activo[i] == 0){
                            enemigo_x[i] = esp_random() % 25;
                            enemigo_y[i] = 0;
                            enemigo_activo[i] = 1;
                            break;
                        }
                    }
                    xSemaphoreGive(mutex_juego);
                }
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    void pantalla(void *args){
    while(1){
        if(xSemaphoreTake(mutex_juego, portMAX_DELAY) == pdTRUE){

            if(estado_juego != 0){
                printf("\033[2J\033[1;1H"); 
                printf("=============================\n");
                printf("        SPACE DEFENDER       \n");
                printf("=============================\n");

                if(estado_juego == 1){
                    printf("SUERTE EN LA PROXIMA\n");
                }else if(estado_juego == 2){
                    printf("HASTA LUEGO\n");
                }
                printf("Puntuacion:%d   Puntuacion maxima: %d\n",puntuacion,puntuacion_maxima);
                printf("(Presiona el cuarto boton para reiniciar)\n");
                printf("Segundos: %d\n",segundos);
                leds_bin(segundos);
            } else {
                tiempo_jugado();
                printf("\033[2J\033[1;1H"); 
                printf("=============================\n");
                printf("        SPACE DEFENDER       \n");
                printf("=============================\n");

                printf("Puntuacion:%d   Vidas: %d\n",puntuacion,vidas);
                printf("Segundos: %d \n",segundos);
                leds_bin(segundos);

                printf("+-------------------------+\n");
                for(int i = 0; i < 11; i++){    //Largo
                    printf("|");
                    for(int j = 0; j < 25; j++){  //Ancho
                        
                        if(i == 10 && j == posicion_actual){
                            printf("^");
                        }else{
                            int hay_proyectil_jugador = 0;
                            for(int p = 0; p < 5; p++){
                                if(proyectil_activo[p] == 1 && i == proyectil_y[p] && j == proyectil_x[p]){
                                    hay_proyectil_jugador = 1;
                                    break;
                                }
                            }

                            int hay_proyectil_enemigo = 0;
                            if(proyectil_activo_enemigo == 1 && i == proyectil_y_enemigo && j == proyectil_x_enemigo){
                                hay_proyectil_enemigo = 1;
                            }

                            if(hay_proyectil_jugador == 1 && hay_proyectil_enemigo == 1){
                                printf(" "); 
                            }else if(hay_proyectil_jugador == 1 || hay_proyectil_enemigo == 1){
                                printf("|"); 
                            }else{
                                int enemigo_mostrado = 0;
                                for(int k = 0; k < 5; k++){
                                    if(enemigo_activo[k] == 1 && i == enemigo_y[k] && j == enemigo_x[k]){
                                        printf("*");
                                        enemigo_mostrado = 1;
                                        break;
                                    }
                                }
                                if(enemigo_mostrado == 0){
                                    printf(" ");
                                }
                            }
                        }
                    }
                    printf("|\n");
                }
                printf("+-------------------------+\n");
            }

            xSemaphoreGive(mutex_juego);
        }
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    }

    void app_main() {
    handlerQueue = xQueueCreate(10, sizeof(int));
    entrada_cola = xQueueCreate(10, sizeof(mecanica));
    
    mutex_juego = xSemaphoreCreateMutex();
    eventos_juego = xEventGroupCreate();
    
    xEventGroupSetBits(eventos_juego, JUEGO_ACTIVO_BIT);

    xTaskCreatePinnedToCore(entrada,"entrada",2048,NULL,1,NULL,1);
    xTaskCreatePinnedToCore(juego,"juego",2048,NULL,1,NULL,1);

    xTaskCreatePinnedToCore(enemigos,"enemigos",2048,NULL,3,NULL,0);
    xTaskCreatePinnedToCore(pantalla,"pantalla",2048,NULL,3,NULL,0);
    
    gpio_install_isr_service(0);
    init_gpio();
    }
