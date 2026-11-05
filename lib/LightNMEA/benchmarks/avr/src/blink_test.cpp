/*
 * blink_test.cpp - минимальный тест: мигание LED на PB0 (MISO, pin 31 на TQFP-64)
 * Также отправляет 'A' на USART0 TX (PD1) каждые 500 мс для проверки UART.
 *
 * Build: cd benchmarks/avr && F_CPU=16000000 ./build.sh BENCH=blink
 *   или вручную:
 *   avr-g++ -mmcu=atmega32 -std=gnu++17 -DF_CPU=16000000UL -Os blink_test.cpp -o build/blink.elf
 *   avr-objcopy -O ihex build/blink.elf build/blink.hex
 */

#include <avr/io.h>
#include <util/delay.h>

int main() {
    // PB0 как выход (MISO, пин 31 на TQFP-64)
    DDRB |= (1 << PB0);
    
    // USART0 TX (PD1)
    // F_CPU=16MHz, U2X, baud=115200: UBRR = 16000000/(8*115200)-1 = 15.75 → 16
    UCSRA = (1 << U2X);
    UBRRH = 0;
    UBRRL = 16;
    UCSRB = (1 << TXEN);
    UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0);  // 8N1
    
    while (1) {
        // Мигаем LED
        PORTB ^= (1 << PB0);
        _delay_ms(250);
        
        // Отправляем 'A' (0x41)
        while (!(UCSRA & (1 << UDRE))) {}
        UDR = 'A';
    }
    
    return 0;
}