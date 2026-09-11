    #include <stdio.h>
    #include <stdlib.h>
    #include <freertos/FreeRTOS.h>
    #include <freertos/task.h>
    #include <freertos/queue.h>
    #include <driver/gpio.h>
    #include "esp_timer.h"
    #include "esp_random.h"


    int leds[] = {32,33,25,26,27};
    int botones[] = {18,19,21};

    QueueHandle_t handlerQueue;
    QueueHandle_t entrada_cola;

    typedef enum{
    mov_izq,
    mov_der,
    disparar
    } mecanica;

    uint64_t time_aux = 0;
    uint64_t time_enemigos = 0;
    uint64_t time_movimiento_enemigos = 0;
    uint64_t time_movimiento_proyectil = 0;
    uint64_t time_dispara_enemigo = 0;

    int segundos = 0;
    int posicion_actual = 12;
    int vidas = 3;
    int puntuacion = 0;
    int puntuacion_maxima = 0;
    int juego_terminado = 0;

    int proyectil_x; //posicion de anchura
    int proyectil_y; //posicion de largo
    int proyectil_activo = 0;

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

    for(int i = 0; i < 3; i++){
        gpio_reset_pin(botones[i]);
        gpio_set_direction(botones[i],GPIO_MODE_INPUT);
        gpio_pulldown_en(botones[i]);
        gpio_pullup_dis(botones[i]);

        gpio_set_intr_type(botones[i],GPIO_INTR_POSEDGE);
        gpio_isr_handler_add(botones[i],gpio_interrupt_handler,(void *)botones[i]);
    }
    }

    void tiempo_jugado(){
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        segundos++;
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

            default:
            break;
        }
        }
    }
    }


    void juego(void *args){
    mecanica evento;
    while(1){
        if(vidas == 0){
            juego_terminado = 1;
            if(puntuacion > puntuacion_maxima){
                puntuacion_maxima = puntuacion;
            }
        }
        if(xQueueReceive(entrada_cola,&evento,150 / portTICK_PERIOD_MS)){
        if(juego_terminado == 1){
            vidas = 3;
            puntuacion = 0;
            posicion_actual =12;

            proyectil_activo = 0;
            proyectil_activo_enemigo = 0;
            
            for(int i = 0; i < 5; i++){
                enemigo_activo[i] = 0;
            }
            juego_terminado = 0;
        }else{
        switch(evento){

            case mov_izq:
            if(posicion_actual > 0){
            posicion_actual--;
            }
            break;

            case mov_der:
            if(posicion_actual <25){
            posicion_actual++;
            }
            break;

            case disparar:
            if(proyectil_activo == 0){
                proyectil_x = posicion_actual;
                proyectil_y = 10;
                proyectil_activo = 1;
            }
            break;
        }
        }
    }
        if(proyectil_activo == 1){
            proyectil_y--;

            if(proyectil_y < 0){
                proyectil_activo = 0;
            }
            for(int i = 0; i < 5; i++){
                if(enemigo_activo[i] == 1 && proyectil_x == enemigo_x[i] && proyectil_y == enemigo_y[i]){
                    enemigo_activo[i] = 0;
                    proyectil_activo = 0;
                    puntuacion++;
                    break;
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
                        vidas--;
                        posicion_actual = 12;
                        break;
                    }
                }
            }
    
        if(tiempo_actual - time_dispara_enemigo > 800){
            time_dispara_enemigo = tiempo_actual;
        
        int enemigo_eligido = esp_random() % 5;
        if(enemigo_activo[enemigo_eligido] == 1){
            proyectil_x_enemigo = enemigo_x[enemigo_eligido];
            proyectil_y_enemigo = enemigo_y[enemigo_eligido];
            proyectil_activo_enemigo = 1;
        }
        }

        if(proyectil_activo_enemigo == 1){
            proyectil_y_enemigo++;

            if(proyectil_y_enemigo > 10){
                proyectil_activo_enemigo = 0;
            }
            if(proyectil_activo_enemigo == 1 && proyectil_x_enemigo == posicion_actual && proyectil_y_enemigo == 10){
                proyectil_activo_enemigo = 0;
                vidas--;
                posicion_actual = 12;
            }
        }
    }
    }

    void enemigos(void *args){
        while(1){
            uint64_t time_actual = esp_timer_get_time() / 1000;
            if(time_actual - time_enemigos > 250){
                time_enemigos = time_actual;

            for(int i = 0; i < 5; i++){
                if(enemigo_activo[i] == 0){
                    enemigo_x[i] = esp_random() % 25;
                    enemigo_y[i] = 0;
                    enemigo_activo[i] = 1;
                    break;
                }
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    }

    void pantalla(void *args){
    while(1){
        if(vidas == 0){
        printf("\033[2J\033[1;1H"); 
        printf("=============================\n");
        printf("        SPACE DEFENDER       \n");
        printf("=============================\n");
        printf("SUERTE EN LA PROXIMA!!\n");
        printf("Puntuacion:%d   Puntuacion maxima: %d\n",puntuacion,puntuacion_maxima);
        
        vTaskDelay(200 / portMAX_DELAY);
        continue;
        }
        printf("\033[2J\033[1;1H"); 
        printf("=============================\n");
        printf("        SPACE DEFENDER       \n");
        printf("=============================\n");

        printf("Puntuacion:%d   Vidas: %d\n",puntuacion,vidas);

        printf("+-------------------------+\n");
        for(int i = 0; i < 11; i++){    //Largo
        printf("|");
        for(int j = 0; j < 25; j++){  //Ancho
            if(i == 10 && j == posicion_actual){
            printf("^");
            //este else if signfica 
            //si se esta mandando un proyectil,debe de coincidir con las coordenas
            //del mapa
            }else if(proyectil_activo == 1 && i == proyectil_y && j == proyectil_x){
                printf("|");
            }else if(proyectil_activo_enemigo == 1 && i == proyectil_y_enemigo && j == proyectil_x_enemigo){
                printf("|");

            }else if(proyectil_activo == 1 && proyectil_activo_enemigo == 1 && proyectil_x == proyectil_x_enemigo 
            && proyectil_y == proyectil_y_enemigo){
                printf(" ");
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
        printf("|\n");
        }
        printf("+-------------------------+\n");
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    }

    void app_main() {
    handlerQueue = xQueueCreate(10, sizeof(int));
    entrada_cola = xQueueCreate(10, sizeof(mecanica));

    xTaskCreatePinnedToCore(entrada,"entrada",2048,NULL,1,NULL,1);
    xTaskCreatePinnedToCore(juego,"juego",2048,NULL,1,NULL,1);

    xTaskCreatePinnedToCore(enemigos,"enemigos",2048,NULL,3,NULL,0);
    xTaskCreatePinnedToCore(pantalla,"pantalla",2048,NULL,3,NULL,0);
    gpio_install_isr_service(0);
    init_gpio();
    }
