#include <stdio.h>
#include "pico/stdlib.h"

#include <math.h>
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "pico/bootrom.h"
#include "hardware/timer.h"
#include "hardware/pwm.h"

#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h"


//Definição das Constantes
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C
#define btA 5        // Pino do botão A
//#define btB 6        // Pino do botão B
#define move_X 26 // Pino para Eixo X
#define move_Y 27 // Pino para Eixo Y
#define bt_JOY 22 // botão do Joystick
#define DEAD_ZONE 30
//declarações de constantes
const uint led_g = 11; //LED verde
const uint led_b = 12; //LED azul
const uint led_r = 13; //LED vermelho
const int ADC_C0 = 0; //canal ADC para eixo X
const int ADC_C1 = 1;//canal ADC para eixo Y
const uint16_t WRAP = 4096;
const float DIVISER = 2;

uint16_t vrx_value, vry_value;
uint16_t div_value_x, div_value_y;

bool green_state = false;
bool pwm_enabled = true;
int border_style = 0;

bool cor_borda = true; 
ssd1306_t ssd;

static volatile uint32_t last_time = 0; 

void init_pwm(uint led){
    gpio_set_function(led, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(led); //obter o canal PWM da GPIO
    pwm_set_clkdiv(slice, DIVISER); //define o divisor de clock do PWM
    pwm_set_wrap(slice, WRAP);
    pwm_set_gpio_level(led, 0); //começa desligado
    pwm_set_enabled(slice,true);
}

void set_pwm(uint led, uint16_t value){ 
    if (pwm_enabled){
        uint slice = pwm_gpio_to_slice_num(led);
        pwm_set_gpio_level(led, value);
    }else{
        pwm_set_gpio_level(led, 0);
    }
}

void set_pin_led(uint pin){
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, false);
}

void set_pin_button(uint button){
    gpio_init(button);
    gpio_set_dir(button, GPIO_IN);
    gpio_pull_up(button);
}

void setup_joystick()
{
  // Inicializa o ADC e os pinos de entrada analógica
  adc_init();         // Inicializa o módulo ADC
  adc_gpio_init(move_X); // Configura o pino VRX (eixo X) para entrada ADC
  adc_gpio_init(move_Y); // Configura o pino VRY (eixo Y) para entrada ADC

  // Inicializa o pino do botão do joystick
  gpio_init(bt_JOY);             // Inicializa o pino do botão
  gpio_set_dir(bt_JOY, GPIO_IN); // Configura o pino do botão como entrada
  gpio_pull_up(bt_JOY);          // Ativa o pull-up no pino do botão para evitar flutuações
}

void setup_i2c(){
  i2c_init(I2C_PORT, 400 * 1000);

  gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); // Set the GPIO pin function to I2C
  gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); // Set the GPIO pin function to I2C
  gpio_pull_up(I2C_SDA); // Pull up the data line
  gpio_pull_up(I2C_SCL); // Pull up the clock line
}

void init_OLED(){
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
    ssd1306_config(&ssd); // Configura o display
    ssd1306_send_data(&ssd); // Envia os dados para o display

     // Limpa o display. O display inicia com todos os pixels apagados.
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);
}

void joystick_read_axis(uint16_t *vx_value, uint16_t *vy_value)
{
  // Leitura do valor do eixo X do joystick
  adc_select_input(ADC_C0); // Seleciona o canal ADC para o eixo X
  sleep_us(2);                     // Pequeno delay para estabilidade
  uint16_t x = 4095 - adc_read(); // Lê o valor do eixo X (0-4095) e inverte o calculo

  // Leitura do valor do eixo Y do joystick
  adc_select_input(ADC_C1); // Seleciona o canal ADC para o eixo Y
  sleep_us(2);                     // Pequeno delay para estabilidade
  uint16_t y = adc_read() - 10;         // Lê o valor do eixo Y (0-4095)e diminui 10

  // Aplica a zona morta
  *vx_value = (abs(x - *vx_value) > DEAD_ZONE) ? x : *vx_value;
  *vy_value = (abs(y - *vy_value) > DEAD_ZONE) ? y : *vy_value;
}

void game_move(uint16_t x, uint16_t y){
    if (x < 1) x = 1;
    if (y < 1) y = 1;
    
    ssd1306_fill(&ssd, !cor_borda);
    ssd1306_rect(&ssd, 3, 3, 122, 58, cor_borda, !cor_borda);
    ssd1306_rect(&ssd, x, y, 8, 8, true, true);
    ssd1306_send_data(&ssd);
}

static void button_press(uint gpio, uint32_t events);
static void ajuste_OLED();

void setup(){
    stdio_init_all();
    setup_joystick();
    init_pwm(led_r);
    init_pwm(led_b);
    set_pin_led(led_g);
    set_pin_button(btA);
    setup_i2c();
    init_OLED();
}



int main()
{
    setup();

    gpio_set_irq_enabled_with_callback(btA, GPIO_IRQ_EDGE_FALL, true, &button_press);  
    gpio_set_irq_enabled(bt_JOY, GPIO_IRQ_EDGE_FALL, true);  // Apenas habilita interrupção    

    printf("Inicio-Joystick-PWM\n"); 

    while (true) {
        joystick_read_axis(&vrx_value, &vry_value);

        div_value_x = vrx_value / 72;
        div_value_y = vry_value / 34; 
        // Ajusta div_value_y
        ajuste_OLED();
        
        game_move(div_value_x, div_value_y);

        vrx_value = 4095 - vrx_value;
        vry_value = 4095 - vry_value;

        if (vry_value < 230 && vry_value > 25 && vrx_value > 346 && vrx_value < 500) {
            set_pwm(led_b, 0);
            set_pwm(led_r, 0);
        } else {
            set_pwm(led_b, vry_value);
            set_pwm(led_r, (vrx_value > 500) ? vrx_value : 0);
        }

       printf("valor vx = %d\n", div_value_x);
       //printf("valor vy = %d\n", vry_value);


    }
    return 0;
}

void button_press(uint gpio, uint32_t events) {
    uint32_t current_time = to_us_since_boot(get_absolute_time());

    if (current_time - last_time > 200000) {
        last_time = current_time;

        if (gpio == btA) {
            pwm_enabled = !pwm_enabled;
            printf("entrei aqui\n");
        }
        else if (gpio == bt_JOY) {
            green_state = !green_state;
            gpio_put(led_g, green_state);
            border_style = (border_style + 1) % 3;
            cor_borda =! cor_borda;
        } 
    } 
}

void ajuste_OLED(){
    if(div_value_y == 0){
        div_value_y = vry_value / 34 + 3; 
    }
    if(div_value_y == 1){
        div_value_y = vry_value / 34 + 2; 
    }
    if(div_value_y == 2){
        div_value_y = vry_value / 34 + 1; 
    }
    if(div_value_y == 120){
        div_value_y = vry_value / 34 - 3; 
    }
    if(div_value_y == 119){
        div_value_y = vry_value / 34 - 2; 
    }
    if(div_value_y == 118){
        div_value_y = vry_value / 34 - 1; 
    }
    if(div_value_x == 0){
        div_value_x = vrx_value / 72 + 3; 
    }
    if(div_value_x == 1){
        div_value_x = vrx_value / 72 + 2; 
    }
    if(div_value_x == 2){
        div_value_x = vrx_value / 72 + 1; 
    }
    if(div_value_x == 56){
        div_value_x = vrx_value / 72 - 3; 
    }
    if(div_value_x == 55){
        div_value_x = vrx_value / 72 - 2; 
    }
    if(div_value_x == 54){
        div_value_x = vrx_value / 72 - 1; 
    }
    
}