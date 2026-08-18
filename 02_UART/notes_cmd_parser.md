# 02_UART — UART Communication on STM32 Nucleo F401RE

## What I built
1. UART printf retarget — printf works over USART2 at 115200 baud
2. Interrupt-driven UART receive with command parser
3. Non-blocking software timer system using SysTick

## Key concepts learned
- HAL_UART_Receive_IT receives one byte per interrupt call
- Must re-arm the interrupt at end of RxCpltCallback
- Shared variables with ISR must be volatile
- Non-blocking timers using HAL_GetTick() allow multiple simultaneous tasks without HAL_Delay blocking

## Commands implemented
- LED_ON / LED_OFF — control onboard LED
- STATUS — show uptime, LED state, button count
- FAST / SLOW / STOP — control LED blink rate
- HELP — list commands

## Bugs I hit

### Bug 1 — Backspace storing garbage in buffer

**What happened:**
The echo was unconditional at the top of the callback. When backspace (`\b` or `0x7F`) was pressed, it got echoed back to the terminal AND stored in `rx_buffer` as a raw control character. When Enter was pressed, `process_command()` received a buffer containing invisible garbage bytes, which matched no valid command and printed "Unknown command."

**What caused it:**
```c
// This ran for EVERY character including \b and 0x7F
while (!(USART2->SR & USART_SR_TXE));
USART2->DR = received;   // echoing backspace itself — wrong
```

**How we fixed it:**
Moved echo inside each specific case. Backspace now sends the erase sequence `\b \b` instead of echoing `0x7F`, and only printable ASCII characters (`0x20` to `0x7E`) are echoed and stored in the buffer. All control characters are handled explicitly or silently ignored.

### Bug 2 — Stale buffer triggering unknown command after backspace

**What happened:**
When the user typed a command, pressed backspace to delete it, then pressed Enter, `process_command()` still received the old command from the previous input. This happened because decrementing `rx_index` to 0 did not clear the actual bytes in `rx_buffer`. The old data was still sitting in memory.

**What caused it:**
```c
// Only decremented index — old data still in buffer
rx_index--;
// rx_buffer still contained "LED_ON" even though user deleted everything
```
Also `cmd_ready` was being set even when `rx_index == 0`, so an empty Enter after full backspace still triggered `process_command()` with stale data.

**How we fixed it:**
Two changes together solved it. First, `rx_buffer[rx_index] = '\0'` clears the character from the buffer at the exact position when backspace is pressed, not just the index. Second, added an `if (rx_index > 0)` guard before setting `cmd_ready`, so pressing Enter on an empty buffer never triggers `process_command()` at all.

## Interview questions this covers
- Interrupt vs polling
- volatile keyword necessity
- UART interrupt-driven receive pattern
- Non-blocking timer pattern
