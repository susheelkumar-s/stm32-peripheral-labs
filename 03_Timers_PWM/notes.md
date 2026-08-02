# 03_Timers_PWM — PWM LED Control on STM32 Nucleo F401RE with CLI over UART

## What I Built

- PWM on TIM2 CH1, PA5 (onboard LED)
- Smooth breathing fade using a non-blocking SoftTimer
- UART command control: `DUTY`, `FADE`, `SPEED`, `REGS` commands
- Live timer register readout via UART

## PWM Configuration

- Timer clock: 84MHz / (PSC+1) = 84MHz / 84 = 1MHz
- PWM frequency: 1MHz / (ARR+1) = 1MHz / 1000 = 1kHz
- Duty cycle: CCR / (ARR+1) × 100%

### Key Formulas

```
PWM freq = Timer_clock / (ARR + 1)
Duty %   = CCR / (ARR + 1) × 100
```

## Commands

| Command        | Action                            |
|----------------|------------------------------------|
| `FADE`         | Start breathing fade effect        |
| `FADE_OFF`     | Stop fade, hold brightness         |
| `DUTY <0-1000>`| Set exact duty cycle               |
| `DUTY_MAX`     | Full brightness                    |
| `DUTY_OFF`     | LED off                            |
| `SPEED <1-100>`| Control fade step speed            |
| `REGS`         | Print live TIM2 register values    |

## Key Insight

- `CCR > ARR` → output stays HIGH permanently (100% duty, no compare trigger)
- Non-blocking fade: SoftTimer checks `HAL_GetTick()` — CPU never blocks/waits

## Architecture Decisions (and Why)

**Single LED owner (`led_mode` enum).**
`LED_OFF / LED_ON / LED_MANUAL / LED_BLINK / LED_FADE`. Exactly one switch block in the main loop writes to `__HAL_TIM_SET_COMPARE`. No command touches the timer register directly. This replaced an earlier design with independent flags (`fade_enabled`, `led_timer.enabled`, direct GPIO toggles) that silently overwrote each other and drifted out of sync with `STATUS` output.

> Rule of thumb: any hardware resource two or more code paths can write to needs a single designated owner, not per-feature flags.

**PWM, not GPIO toggle, for blink.**
Once PA5 is in Alternate Function mode for TIM2, direct `HAL_GPIO_TogglePin` writes to the ODR register have no effect on the pin. Blink modes drive duty between 0 and 1000 through the same PWM path as fade/manual.

**stdout is unbuffered.**
`setvbuf(stdout, NULL, _IONBF, 0);` is called before the main loop. Without it, newlib fully buffers stdout (because `_isatty()` returns 0 on bare metal), so printf output only flushes when the internal buffer fills — causing prompt/banner text to appear late and out of order relative to what was actually printed first.

**RX echo bypasses `HAL_UART_Transmit`.**
The echo writes directly to `USART2->DR` after polling TXE, instead of calling `HAL_UART_Transmit`. Two independent `HAL_UART_Transmit` calls sharing one handle (the echo, and any printf mid-transmission) can collide — HAL's busy-state flag causes the second call to silently return `HAL_BUSY` and drop the byte. Register-level echo has no such shared state.

**RX ISR re-arms before doing anything else.**
`HAL_UART_Receive_IT` is called again as the first line of `HAL_UART_RxCpltCallback`, on a locally snapshotted copy of the byte (`uint8_t received = rx_byte;`), before any processing or echo. This minimizes the window where the UART is not listening for the next byte, and avoids acting on `rx_byte` after it may have already been overwritten by a new interrupt.

**Debounced button interrupt.**
`HAL_GPIO_EXTI_Callback` ignores presses within 50ms of the last accepted one (`HAL_GetTick()` diff), since mechanical contact bounce fires the interrupt 5–20× per physical press otherwise.

**Shared ISR-modified variables are all `volatile`.**
`rx_byte`, `rx_index`, `cmd_ready`, `button_press_count`, `last_button_time` — without `volatile`, the compiler can legally cache a stale value across loop iterations since it can't see the ISR-side writes.

## Known-Good Debugging Notes (Bugs Hit + Root Cause)

| Symptom | Root Cause | Fix |
|---|---|---|
| Startup prompt appears late, after first command | stdout fully buffered (`_isatty` returns 0 by default) | `setvbuf(..., _IONBF, ...)` |
| First 1–2 typed characters missing from echo, but full command still parses correctly | Echo `HAL_UART_Transmit` colliding with an in-flight printf on the same handle, returning `HAL_BUSY` and dropping the echo byte | Direct register echo |
| CLI stops responding to input after adding PWM | Accidental TIM2 channel pin remap onto PA2/PA3 (USART2 pins) in CubeMX pinout view | Always verify PA2/PA3 still show USART2_TX/USART2_RX after touching the pinout |
| CLI silently stops receiving anything after adding a new peripheral | USART2 global interrupt no longer enabled in NVIC | Easy to overlook during CubeMX regeneration when reconfiguring other peripherals — recheck NVIC settings |
| LED state and STATUS output disagree / one feature overrides another | Multiple independent flags/timers writing to the same PWM channel with no coordination | Single `led_mode` enum, single owner block |
| Button counter always 0 | No `HAL_GPIO_EXTI_Callback` implemented yet — declaring the counter variable and printf line isn't enough, the increment logic has to actually exist | Implement the callback |
| `atoi` implicit declaration warning | Missing `#include <stdlib.h>` | Add the include |

## Open / To Verify

- `SPEED` intermittently falls through to the range-error branch — suspected stale/offset buffer content after backspace editing mid-command. Needs:
  - A clean (no-backspace) retest of `SPEED 50`
  - A review of the backspace handling in `HAL_UART_RxCpltCallback` to confirm it fully clears edited characters rather than just decrementing `rx_index`
