#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "diag/black_box.hpp"
#include "diag/stack_monitor.hpp"
#include "esp_private/panic_internal.h"
#include "hal/uart_hal.h"
#include "xtensa_context.h"

// Forward declare: defined in esp_system/panic.c, not in any public header
extern "C" void esp_panic_handler_feed_wdts(void);

// Direct UART HAL output — no stdio/VFS dependency
// Safe even when the UART driver or VFS layer is corrupted (LL-026).
namespace
{

uart_hal_context_t s_panic_uart = {.dev = &UART0};

void panic_putc(char c)
{
    uint32_t sz = 0;
    while (uart_hal_get_txfifo_len(&s_panic_uart) == 0)
    {
    }
    uart_hal_write_txfifo(&s_panic_uart, reinterpret_cast<const uint8_t*>(&c), 1, &sz);
}

void panic_puts(const char* s)
{
    for (; *s; ++s)
    {
        if (*s == '\n')
        {
            panic_putc('\r');
        }
        panic_putc(*s);
    }
}

// Minimal unsigned integer printer to a static buffer
void panic_print_uint(unsigned long val)
{
    char buf[16];
    char* p = buf + sizeof(buf);
    *--p = '\0';
    if (val == 0)
    {
        *--p = '0';
    }
    else
    {
        while (val > 0)
        {
            *--p = '0' + (val % 10);
            val /= 10;
        }
    }
    panic_puts(p);
}

void panic_print_hex_addr(uint32_t addr)
{
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    buf[10] = '\0';
    for (int i = 9; i >= 2; --i)
    {
        unsigned nibble = addr & 0xF;
        buf[i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        addr >>= 4;
    }
    panic_puts(buf);
}

} // anonymous namespace

// NOLINTBEGIN(bugprone-reserved-identifier) // reason: platform-specific CPU register access
extern "C" void __wrap_esp_panic_handler(void* info) noexcept
{

    __asm__ volatile("rsil a0, 3");

    // Feed RWDT to prevent reset during crash dump (Gap 3 fix)
    esp_panic_handler_feed_wdts();

    auto* pi = static_cast<panic_info_t*>(info);
    auto* frame = static_cast<const XtExcFrame*>(pi->frame);

    panic_puts("\n=== CRASH ===\n");

    // Print exception cause and summary from panic_info_t (Gap 4 fix)
    panic_puts("exccause=");
    panic_print_uint(static_cast<unsigned long>(panic_get_cause(pi->frame)));
    panic_puts(" name=");
    panic_puts(pi->reason ? pi->reason : "unknown");
    panic_puts(" pc=");
    panic_print_hex_addr(panic_get_address(pi->frame));
    panic_puts(" excvaddr=");
    panic_print_hex_addr(frame->excvaddr);
    panic_puts(" ps=");
    panic_print_hex_addr(frame->ps);
    panic_puts(" sp=");
    panic_print_hex_addr(frame->a1);
    panic_puts("\n");

    panic_puts("=== REGISTERS ===\n");
    // Print a0-a15 (each is a long, 4 bytes on Xtensa)
    panic_puts("a0=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a0));
    panic_puts("  a1=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a1));
    panic_puts("  a2=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a2));
    panic_puts("  a3=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a3));
    panic_puts("\n");
    panic_puts("a4=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a4));
    panic_puts("  a5=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a5));
    panic_puts("  a6=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a6));
    panic_puts("  a7=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a7));
    panic_puts("\n");
    panic_puts("a8=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a8));
    panic_puts("  a9=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a9));
    panic_puts("  a10=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a10));
    panic_puts("  a11=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a11));
    panic_puts("\n");
    panic_puts("a12=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a12));
    panic_puts("  a13=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a13));
    panic_puts("  a14=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a14));
    panic_puts("  a15=0x");
    panic_print_hex_addr(static_cast<uint32_t>(frame->a15));
    panic_puts("\n");

    panic_puts("=== BLACK BOX ===\n");
    ecotiter::diag::BlackBox::instance().dump(panic_puts);

    panic_puts("=== STACK ===\n");
    ecotiter::diag::StackMonitor::instance().logAllWatermarks(panic_puts);

    panic_puts("!!! EXCEPTION END !!!\n");

    extern void __real_esp_panic_handler(void*);
    __real_esp_panic_handler(info);
}
// NOLINTEND(bugprone-reserved-identifier) // reason: END platform register access
