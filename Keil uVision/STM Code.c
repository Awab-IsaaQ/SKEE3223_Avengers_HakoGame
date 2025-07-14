#include "stm32f4xx.h"

// -------------------------------------------
//         PIN DEFINITIONS (STM32F4)
// -------------------------------------------

// LEDs on GPIOB (active HIGH)
#define LED1_PORT GPIOB
#define LED1_PIN  12
#define LED2_PORT GPIOB
#define LED2_PIN  13
#define LED3_PORT GPIOB
#define LED3_PIN  14

// Game Buttons
#define BTN1_PORT GPIOA
#define BTN1_PIN  2   // Active HIGH
#define BTN2_PORT GPIOA
#define BTN2_PIN  3   // Active HIGH
#define BTN3_PORT GPIOB
#define BTN3_PIN  0   // Active LOW

// Replay Button on PB1 (active LOW)
#define REPLAY_PORT GPIOB
#define REPLAY_PIN  1

// Speaker on GPIOD pin 2
#define SPK_PORT GPIOD
#define SPK_PIN  2

// -------------------------------------------
//             GAME CONFIGURATION
// -------------------------------------------

int LENGTH = 100;           // Initial note duration (ms)
int MIN_LENGTH = 50;        // Fastest allowed note duration
int notes[3] = {1568, 1976, 2349}; // Button tones (G6, B6, D7)
int gamepattern[20];        // Stores generated pattern
int patternLength;          // Current pattern length

// Timing system
volatile uint32_t sysTickMillis = 0;
volatile int replay_requested = 0;

// -------------------------------------------
//              SYSTEM TICK
// -------------------------------------------

void SysTick_Handler(void) {
    sysTickMillis++;
}

void delay_ms(uint32_t ms) {
    uint32_t start = sysTickMillis;
    while ((sysTickMillis - start) < ms);
}

uint32_t millis(void) {
    return sysTickMillis;
}

// -------------------------------------------
//               LED CONTROL
// -------------------------------------------

void led_on(int index) {
    if (index == 1) LED1_PORT->ODR |= (1 << LED1_PIN);
    else if (index == 2) LED2_PORT->ODR |= (1 << LED2_PIN);
    else if (index == 3) LED3_PORT->ODR |= (1 << LED3_PIN);
}

void led_off(int index) {
    if (index == 1) LED1_PORT->ODR &= ~(1 << LED1_PIN);
    else if (index == 2) LED2_PORT->ODR &= ~(1 << LED2_PIN);
    else if (index == 3) LED3_PORT->ODR &= ~(1 << LED3_PIN);
}

void all_leds_on(void) {
    led_on(1); led_on(2); led_on(3);
}

void all_leds_off(void) {
    led_off(1); led_off(2); led_off(3);
}

// -------------------------------------------
//         GPIO + INTERRUPT INITIALIZATION
// -------------------------------------------

void gpio_init(void) {
    // Enable GPIO and SYSCFG clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN |
                    RCC_AHB1ENR_GPIOBEN |
                    RCC_AHB1ENR_GPIODEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    // Configure LED pins as output
    LED1_PORT->MODER |=  (0b01 << (LED1_PIN * 2));
    LED2_PORT->MODER |=  (0b01 << (LED2_PIN * 2));
    LED3_PORT->MODER |=  (0b01 << (LED3_PIN * 2));

    // Buttons 1 & 2 (active HIGH): pull-down
    BTN1_PORT->PUPDR |=  (0b10 << (BTN1_PIN * 2));
    BTN2_PORT->PUPDR |=  (0b10 << (BTN2_PIN * 2));

    // Button 3 & Replay (active LOW): pull-up
    BTN3_PORT->PUPDR |=  (0b01 << (BTN3_PIN * 2));
    REPLAY_PORT->PUPDR |=  (0b01 << (REPLAY_PIN * 2));

    // Speaker pin as output
    SPK_PORT->MODER |= (0b01 << (SPK_PIN * 2));

    // Set EXTI1 (PB1) for replay interrupt
    SYSCFG->EXTICR[0] |= (0x1 << 4);  // EXTI1 = PB1
    EXTI->IMR  |= (1 << 1);           // Unmask interrupt line 1
    EXTI->RTSR |= (1 << 1);           // Trigger on rising edge
    NVIC_EnableIRQ(EXTI1_IRQn);       // Enable interrupt in NVIC

    all_leds_off();  // Start with LEDs off
}

// EXTI1 IRQ: triggered by replay button
void EXTI1_IRQHandler(void) {
    if (EXTI->PR & (1 << 1)) {
        EXTI->PR = (1 << 1);   // Clear flag
        replay_requested = 1; // Set flag
    }
}

// -------------------------------------------
//                SOUND SYSTEM
// -------------------------------------------

void tone(int freq, int duration_ms) {
    int period_us = 1000000 / freq;
    int half_us = period_us / 2;
    int cycles = (duration_ms * 1000) / period_us;

    for (int i = 0; i < cycles; i++) {
        SPK_PORT->ODR |= (1 << SPK_PIN);
        for (volatile int j = 0; j < half_us * 8; j++) __NOP();
        SPK_PORT->ODR &= ~(1 << SPK_PIN);
        for (volatile int j = 0; j < half_us * 8; j++) __NOP();
    }
}

void play_note(int index, int notespeed) {
    led_on(index);
    tone(notes[index - 1], notespeed);
    delay_ms(notespeed / 2);
    led_off(index);
}

// -------------------------------------------
//             GAME SEQUENCES
// -------------------------------------------

// Startup blinking animation
void initial_sequence() {
    for (int i = 0; i < 6; i++) {
        play_note(1, 20);
        play_note(2, 20);
        play_note(3, 20);
    }
}

// Short sparkling "level up" melody
void play_level_up_note() {
    all_leds_on();
    tone(1568, 40); delay_ms(50);  // G6
    tone(1864, 40); delay_ms(50);  // A#6
    tone(2093, 60); delay_ms(60);  // C7
    all_leds_off();
}

// Coin-like high tone effect (for win)
void play_win_note() {
    all_leds_on();
    tone(2700, 20); delay_ms(5);
    tone(3000, 25); delay_ms(5);
    tone(3400, 20); delay_ms(5);
    all_leds_off();
}

// Error tone + LED flash
void game_over(void) {
    all_leds_on();
    tone(200, 200); delay_ms(200);
    all_leds_off(); delay_ms(100);
}

// -------------------------------------------
//            INPUT + GAME SETUP
// -------------------------------------------

// Wait for any button to be pressed to start
void wait_for_start() {
    while (1) {
        if ((BTN1_PORT->IDR & (1 << BTN1_PIN)) ||
            (BTN2_PORT->IDR & (1 << BTN2_PIN)) ||
            !(BTN3_PORT->IDR & (1 << BTN3_PIN))) {
            delay_ms(300); // Debounce delay
            break;
        }
    }
}

// Simple pseudo-random generator (based on millis)
uint32_t rng_seed = 1;
void seed_random(uint32_t seed) {
    rng_seed = seed;
}

int random_range(int min, int max) {
    rng_seed = rng_seed * 1103515245 + 12345;
    return min + (rng_seed % (max - min));
}

// Generate pattern of values 1–3
void generate_game(int length) {
    seed_random(millis());
    for (int i = 0; i < length; i++) {
        gamepattern[i] = random_range(1, 4);
    }
}

// -------------------------------------------
//              GAME LOOP
// -------------------------------------------

void play_game() {
    int roundCount = 0;
    int userInput = 0;
    int gameSpeed = LENGTH;
    const int buttonSpeed = 25;

    while (1) {
        generate_game(patternLength);
        delay_ms(500);

    replay_pattern:
        // Show pattern to player
        for (int j = 0; j < patternLength; j++) {
            play_note(gamepattern[j], gameSpeed);
            if (j < patternLength - 1 && gamepattern[j] == gamepattern[j + 1])
                delay_ms(70); // Extra spacing for same-note blink
        }

        // Wait for player input or replay request
        replay_requested = 0;
        int playerStarted = 0;

        while (!playerStarted && !replay_requested) {
            if ((BTN1_PORT->IDR & (1 << BTN1_PIN)) ||
                (BTN2_PORT->IDR & (1 << BTN2_PIN)) ||
                !(BTN3_PORT->IDR & (1 << BTN3_PIN))) {
                playerStarted = 1;
            }

            if (replay_requested || !(REPLAY_PORT->IDR & (1 << REPLAY_PIN))) {
                while (!(REPLAY_PORT->IDR & (1 << REPLAY_PIN))) {}
                delay_ms(50);
                replay_requested = 0;
                goto replay_pattern;
            }
        }

        // Player input check
        for (int i = 0; i < patternLength; i++) {
            int buttonPressed = 0;
            while (!buttonPressed) {
                if (BTN1_PORT->IDR & (1 << BTN1_PIN)) {
                    userInput = 1; buttonPressed = 1;
                } else if (BTN2_PORT->IDR & (1 << BTN2_PIN)) {
                    userInput = 2; buttonPressed = 1;
                } else if (!(BTN3_PORT->IDR & (1 << BTN3_PIN))) {
                    userInput = 3; buttonPressed = 1;
                }
            }

            play_note(userInput, buttonSpeed);

            // Wrong input ends game
            if (userInput != gamepattern[i]) {
                game_over();
                return;
            }

            // Wait for button release
            while ((BTN1_PORT->IDR & (1 << BTN1_PIN)) ||
                   (BTN2_PORT->IDR & (1 << BTN2_PIN)) ||
                   !(BTN3_PORT->IDR & (1 << BTN3_PIN))) {}
            delay_ms(50);
        }

        // Successful round
        roundCount++;
        if (roundCount % 3 == 0) {
            patternLength++;
            delay_ms(50);
            play_level_up_note();
        } else {
            delay_ms(50);
            play_win_note();
        }

        // Increase difficulty (shorter note duration)
        if (gameSpeed > MIN_LENGTH) gameSpeed -= 10;
    }
}

// -------------------------------------------
//                 MAIN
// -------------------------------------------

int main(void) {
    gpio_init();
    SysTick_Config(SystemCoreClock / 1000);
    initial_sequence();

    while (1) {
        patternLength = 4;
        wait_for_start();
        play_game();
    }
}
