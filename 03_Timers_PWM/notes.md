\# 03\_Timers\_PWM — PWM LED Control on STM32 Nucleo F401RE with Command-line interface over UART



\## What I built

\- PWM on TIM2 CH1, PA5 (onboard LED)

\- Smooth breathing fade using non-blocking SoftTimer

\- UART command control: DUTY, FADE, SPEED, REGS commands

\- Live timer register readout via UART



\## PWM Configuration

\- Timer clock: 84MHz / (PSC+1) = 84MHz / 84 = 1MHz

\- PWM frequency: 1MHz / (ARR+1) = 1MHz / 1000 = 1kHz

\- Duty cycle: CCR / (ARR+1) × 100%



\## Key Formula

PWM freq = Timer\_clock / (ARR + 1)

Duty %   = CCR / (ARR + 1) × 100



\## Commands added

| Command      | Action                        |

|--------------|-------------------------------|

| FADE         | Start breathing fade effect   |

| FADE\_OFF     | Stop fade, hold brightness    |

| DUTY <0-1000>| Set exact duty cycle          |

| DUTY\_MAX     | Full brightness               |

| DUTY\_OFF     | LED off                       |

| SPEED <1-100>| Control fade step speed       |

| REGS         | Print live TIM2 register values|



\## Key insight

CCR > ARR = output stays HIGH permanently (100% duty, no compare trigger)

Non-blocking fade: SoftTimer checks HAL\_GetTick() — CPU never waits



Architecture decisions (and why)



Single LED owner (led\_mode enum). LED\_OFF / LED\_ON / LED\_MANUAL / LED\_BLINK / LED\_FADE. Exactly one switch block in the main loop writes to \_\_HAL\_TIM\_SET\_COMPARE. No command touches the timer register directly. This replaced an earlier design with independent flags (fade\_enabled, led\_timer.enabled, direct GPIO toggles) that silently overwrote each other and drifted out of sync with STATUS output. Rule of thumb: any hardware resource two or more code paths can write to needs a single designated owner, not per-feature flags.



PWM, not GPIO toggle, for blink. Once PA5 is in Alternate Function mode for TIM2, direct HAL\_GPIO\_TogglePin writes to the ODR register have no effect on the pin. Blink modes drive duty between 0 and 1000 through the same PWM path as fade/manual.



stdout is unbuffered. setvbuf(stdout, NULL, \_IONBF, 0); called before the main loop. Without it, newlib fully buffers stdout (because \_isatty() returns 0 on bare metal), so printf output only flushes when the internal buffer fills — causing prompt/banner text to appear late and out of order relative to what was actually printed first.



RX echo bypasses HAL\_UART\_Transmit. The echo writes directly to USART2->DR after polling TXE, instead of calling HAL\_UART\_Transmit. Two independent HAL\_UART\_Transmit calls sharing one handle (the echo, and any printf mid-transmission) can collide — HAL's busy-state flag causes the second call to silently return HAL\_BUSY and drop the byte. Register-level echo has no such shared state.



RX ISR re-arms before doing anything else. HAL\_UART\_Receive\_IT is called again as the first line of HAL\_UART\_RxCpltCallback, on a locally snapshotted copy of the byte (uint8\_t received = rx\_byte;), before any processing or echo. Minimizes the window where the UART is not listening for the next byte, and avoids acting on rx\_byte after it may have already been overwritten by a new interrupt.



Debounced button interrupt. HAL\_GPIO\_EXTI\_Callback ignores presses within 50ms of the last accepted one (HAL\_GetTick() diff), since mechanical contact bounce fires the interrupt 5-20× per physical press otherwise.



Shared ISR-modified variables are all volatile. rx\_byte, rx\_index, cmd\_ready, button\_press\_count, last\_button\_time — without volatile, the compiler can legally cache a stale value across loop iterations since it can't see the ISR-side writes.



Known-good debugging notes (bugs hit + root cause)

Startup prompt appears late, after first command → stdout fully buffered (\_isatty returns 0 by default) → fix: setvbuf(..., \_IONBF, ...).

First 1-2 typed characters missing from echo, but full command still parses correctly → echo HAL\_UART\_Transmit colliding with an in-flight printf on the same handle, returning HAL\_BUSY and dropping the echo byte → fix: direct register echo.

CLI stops responding to input after adding PWM → check for accidental TIM2 channel pin remap onto PA2/PA3 (USART2 pins) in CubeMX pinout view — always verify PA2/PA3 still show USART2\_TX/USART2\_RX after touching the pinout.

CLI silently stops receiving anything after adding a new peripheral → check USART2 global interrupt is still enabled in NVIC; easy to overlook during CubeMX regeneration when reconfiguring other peripherals.

LED state and STATUS output disagree / one feature overrides another → multiple independent flags/timers writing to the same PWM channel with no coordination → fix: single led\_mode enum, single owner block.

Button counter always 0 → no HAL\_GPIO\_EXTI\_Callback implemented yet; declaring the counter variable and printf line isn't enough, the increment logic has to actually exist.

atoi implicit declaration warning → missing #include <stdlib.h>.

Open / to verify

SPEED <n> intermittently falls through to the range-error branch — suspected stale/ offset buffer content after backspace editing mid-command. Needs a clean (no-backspace) retest of SPEED 50 plus a review of the backspace handling in HAL\_UART\_RxCpltCallback to confirm it fully clears edited characters rather than just decrementing rx\_index.

