#include <avr/io.h>

#ifndef F_CPU
#error "F_CPU must be defined, e.g. -DF_CPU=16000000UL"
#endif

int main() {
    DDRD |= (1 << PD1);       // PD1 как выход

    while (1) {
        PORTD |= (1 << PD1);  // включить
        // задержка ~500 мс при 16 МГц
        volatile uint32_t d = 8000000UL;
        while (d--) {}

        PORTD &= ~(1 << PD1); // выключить
        d = 8000000UL;
        while (d--) {}
    }
}
