# STM32 Nucleo-F401RE — UART CLI + PWM + I2C OLED

Builds on the earlier UART CLI + TIM2 PWM project (see `03_Timers_PWM/notes.md`), adding
an SSD1306 I2C OLED display driven from the same command parser and non-blocking timer loop.

## Hardware
- Board: NUCLEO-F401RE
- UART: USART2, PA2 (TX) / PA3 (RX), ST-LINK VCP, 115200 8N1
- PWM: TIM2_CH1 on PA5
- Button: PC13, EXTI15_10, falling edge, 50ms debounce
- OLED: SSD1306, I2C1, **PB8 = SCL, PB9 = SDA**, address **0x3C**, Standard Mode (100kHz)

## OLED wiring
```
OLED VCC → 3.3V (CN6-4)   — NOT 5V, STM32 GPIOs are not 5V-tolerant on these pins
OLED GND → GND  (CN6-6)
OLED SCL → PB8  (CN5-3)
OLED SDA → PB9  (CN5-4)
```
**Pull-ups matter more than expected.** The onboard R1/R2 on the cheap breakout used here
were not reliably pulling SDA/SCL high — confirmed by I2C scans returning wildly inconsistent
ghost addresses (e.g. `0x08`, `0x2F`, `0x6B`) that changed on every run, worst at slower clock
speeds (10kHz gave *more* garbage hits than 100kHz — the opposite of what a rise-time problem
would predict, and the actual signature of floating/noise-susceptible lines, not marginal
timing). If a scan is inconsistent between runs — some scans clean, some full of nonsense
addresses, results getting worse at lower clock speed — suspect missing/ineffective pull-ups
or a weak connection before suspecting code. A solid, low-resistance GND path between board
and MCU is equally required; an intermittent GND reference produces the same kind of noise.
STM32's internal weak GPIO pull-ups (PB8/PB9 set to Pull-up in CubeMX) are a usable stopgap
if external 4.7kΩ resistors aren't on hand, though weaker than a proper external pull-up pair.

## Driver source
[afiskon/stm32-ssd1306](https://github.com/afiskon/stm32-ssd1306) — `ssd1306.c/.h`,
`ssd1306_fonts.c/.h`, `ssd1306_conf.h` (renamed from the repo's `_template` file and edited
to match this project's MCU family and `hi2c1` handle).

**Build error hit:** `ssd1306.h` ships defaulting to `#include "stm32f0xx_hal.h"` (the repo's
example target). This project is F401 (F4 family) — edit `ssd1306.h`'s include (or better,
configure it via `ssd1306_conf.h` per the repo's own instructions) to pull in
`stm32f4xx_hal.h` instead, or the build fails immediately with `fatal error: stm32f0xx_hal.h:
No such file or directory` on every file that includes it.

**Why the bare I2C scanner sometimes failed to find the display but the real driver works
reliably:** `ssd1306_Init()` sends the SSD1306's actual power-on/charge-pump/display-on
command sequence with the delays it expects, rather than a fast, cold `HAL_I2C_IsDeviceReady`
poll across 127 addresses. On borderline wiring, the difference between "scanner sees
nothing" and "driver works" can come down to this extra settling time and correct wake
sequence, not just electrical margin — another reason not to over-trust a clean/dirty scanner
result as the final word on whether the hardware is fundamentally fine.

## Architecture — screen selection follows the same "single owner" pattern as the LED
```c
uint8_t current_screen = 1;   // 1 = uptime, 2 = system info
```
One `display_timer`-gated block in the main loop is the only place that calls
`ssd1306_Fill` / `ssd1306_WriteString` / `ssd1306_UpdateScreen`; commands only set
`current_screen`, never touch the display API directly — same reasoning as the `led_mode`
enum: exactly one piece of code writes to a shared resource, everything else declares intent.

```c
if (timer_expired(&display_timer)) {
    ssd1306_Fill(Black);
    if (current_screen == 1) {
        // uptime screen
    } else if (current_screen == 2) {
        // static system info screen
    }
    ssd1306_UpdateScreen();
}
```

## Commands added
| Command | Effect |
|---|---|
| `SCREEN1` | Show uptime screen on OLED |
| `SCREEN2` | Show static system info screen on OLED |

## Bug log
- **Uptime stuck at 0s on screen** → `HAL_GetTick()` was read once, in a one-time init block
  before `while(1)`, then never again — same class of bug as the very first blocking `Tick:`
  printf example from the UART project. Fix: read it inside the `timer_expired(&display_timer)`
  block in the main loop so it's re-sampled on every refresh, not baked in at boot.
- **Inconsistent/ghost I2C scan results** → not a code bug; root cause was insufficient
  pull-up strength on SDA/SCL (see wiring section above). Resolved once the real driver's
  init sequence + adequate pull-up path were both in place.
- **`fatal error: stm32f0xx_hal.h: No such file or directory`** → third-party driver's
  default target was F0, not F4 — fix the include target for this MCU family before anything
  else in the driver will compile.

## Open follow-ups
- OLED currently shows static/independent text (`"I2C Working!"`, hardcoded MCU info) —
  natural next step is to have `SCREEN2` (or a new screen) read the same `led_mode`,
  `manual_duty`, and `button_press_count` variables the UART `STATUS` command already reports,
  so OLED and UART can never disagree about system state — same reasoning that fixed the
  earlier `STATUS` / `fade_enabled` desync bug.
- Only one 10kΩ pull-up resistor confirmed in use during bring-up (on SDA); a second
  resistor on SCL (ideally matched, 4.7kΩ–10kΩ) is still worth adding for margin once
  available, even though the display is currently working.
