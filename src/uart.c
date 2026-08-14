#include "uart.h"
#include <stdint.h>

#define UART0 ((volatile unsigned char *)0x10000000UL)

void uart_putc(char c) {
    if (c == '\n') uart_putc('\r');
    UART0[0] = (unsigned char)c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void print_uint(unsigned long v, int base, int width, char pad, int left_align) {
    char buf[32];
    int i = 0;
    if (v == 0) buf[i++] = '0';
    while (v) {
        int d = v % base;
        buf[i++] = d < 10 ? ('0' + d) : ('a' + d - 10);
        v /= base;
    }
    int digits = i;
    if (left_align) {
        while (i--) uart_putc(buf[i]);
        for (int k = digits; k < width; k++) uart_putc(' ');
    } else {
        for (int k = digits; k < width; k++) uart_putc(pad);
        while (i--) uart_putc(buf[i]);
    }
}

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* Interrupt-disable guard so a timer/ecall trap can't switch to another
 * task mid-string and interleave UART output between tasks. */
static uint32_t irq_save_disable(void) {
    uint32_t mstatus;
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(mstatus)); /* clear MIE (bit3) */
    return mstatus;
}
static void irq_restore(uint32_t saved) {
    __asm__ volatile ("csrw mstatus, %0" :: "r"(saved));
}

/* Tiny printf: %d %u %x %s %c %% only, plus '-' (left align) and a
 * numeric field width — enough for scheduler diagnostics. */
void uart_printf(const char *fmt, ...) {
    uint32_t saved = irq_save_disable();
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { uart_putc(*p); continue; }
        p++;
        int left_align = 0;
        int width = 0;
        char pad = ' ';
        if (*p == '-') { left_align = 1; p++; }
        if (*p == '0') { pad = '0'; p++; }
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        switch (*p) {
            case 'd': {
                long v = __builtin_va_arg(ap, int);
                if (v < 0) { uart_putc('-'); v = -v; }
                print_uint((unsigned long)v, 10, width, pad, left_align);
                break;
            }
            case 'u':
                print_uint(__builtin_va_arg(ap, unsigned int), 10, width, pad, left_align);
                break;
            case 'x':
                print_uint(__builtin_va_arg(ap, unsigned int), 16, width, pad, left_align);
                break;
            case 'l': {
                p++;
                if (*p == 'u') print_uint(__builtin_va_arg(ap, unsigned long), 10, width, pad, left_align);
                else if (*p == 'x') print_uint(__builtin_va_arg(ap, unsigned long), 16, width, pad, left_align);
                break;
            }
            case 's': {
                const char *s = __builtin_va_arg(ap, const char *);
                int len = str_len(s);
                if (left_align) {
                    uart_puts(s);
                    for (int i = len; i < width; i++) uart_putc(' ');
                } else {
                    for (int i = len; i < width; i++) uart_putc(' ');
                    uart_puts(s);
                }
                break;
            }
            case 'c':
                uart_putc((char)__builtin_va_arg(ap, int));
                break;
            case '%':
                uart_putc('%');
                break;
            default:
                uart_putc('%');
                uart_putc(*p);
        }
    }
    __builtin_va_end(ap);
    irq_restore(saved);
}
