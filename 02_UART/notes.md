## UART `printf` on STM32 Nucleo-F401RE

### What I Built
- **Blocking `printf` over UART:** Standard C `printf` output directed to a serial console (PuTTY) via the USART2 peripheral.
- **Minimal "Hello World" and Counter:** Firmware prints a startup message and a continuously incrementing counter every 500 ms.
- **Blocking Transmission Only:** Simple polling-based UART with no interrupts or DMA. Good for debugging, bad for real-time performance.

### Hardware Setup
- **Board:** NUCLEO-F401RE
- **Connection:** On-board ST-LINK/V2-1 acts as USB-to-UART bridge. No external wires needed.
- **Pins Used:** PA2 (USART2_TX, AF07) and PA3 (USART2_RX, AF07 - configured but unused).
- **PC Driver:** STSW-LINK009 (Virtual COM Port driver for Windows).

### Software Architecture
- **Toolchain:** STM32CubeIDE, ARM GCC, newlib-nano.
- **Key Concept:** Overwrote the `_write()` syscall so the C standard library's `printf` sends characters directly to the USART2 Data Register.
- **UART Config:** 115200 baud, 8 data bits, 1 stop bit, no parity, no flow control.
- **Blocking Mechanism:** `_write()` polls the `TXE` (Transmit Data Register Empty) flag in `USART2->SR` before writing each byte. Waits for `TC` (Transmission Complete) at end of buffer.

### How to View the Output
1. Download and install PuTTY.
2. Ensure the ST-LINK VCP driver is installed (check Device Manager for "STMicroelectronics STLink Virtual COM Port").
3. Close any IDE terminal to free the COM port.
4. Open PuTTY:
   - **Connection type:** Serial
   - **Serial line:** COM3 (or your assigned port)
   - **Speed:** 115200
5. Reset the Nucleo board. The `printf` output should appear.
