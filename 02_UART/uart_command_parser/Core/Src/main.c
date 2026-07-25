/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
// Non-blocking timer pattern — the most important pattern in bare-metal embedded
typedef struct {
    uint32_t last_tick;
    uint32_t period_ms;
    uint8_t  enabled;
} SoftTimer;

// Check if timer has expired — non-blocking
uint8_t timer_expired(SoftTimer *t) {
    if (!t->enabled) return 0;
    if ((HAL_GetTick() - t->last_tick) >= t->period_ms) {
        t->last_tick = HAL_GetTick();
        return 1;
    }
    return 0;
}
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
/* Global variables */
uint8_t rx_byte;
char rx_buffer[64];
uint8_t rx_index = 0;
volatile uint8_t cmd_ready = 0;
volatile uint8_t button_flag = 0;
volatile uint32_t button_press_count = 0;
volatile uint8_t led_state = 0;

/* Software timers — global so process_command() can access them */
SoftTimer led_timer     = {0, 500,  1};
SoftTimer status_timer  = {0, 3000, 1};
SoftTimer counter_timer = {0, 1000, 1};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
void process_command(char *cmd);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
//UART Receive Callback
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        uint8_t received = rx_byte;

        // Re-arm FIRST
        HAL_UART_Receive_IT(&huart2, &rx_byte, 1);

        // NO unconditional echo here anymore

        if (received == '\r')
        {
            rx_buffer[rx_index] = '\0';

            if (rx_index > 0)
            {
                cmd_ready = 1;
            }

            rx_index = 0;

            // Move to new line
            while (!(USART2->SR & USART_SR_TXE));
            USART2->DR = '\r';
            while (!(USART2->SR & USART_SR_TXE));
            USART2->DR = '\n';
        }
        else if (received == '\n')
        {
            // Ignore
        }
        else if (received == '\b' || received == 0x7F)
        {
            if (rx_index > 0)
            {
                rx_index--;
                rx_buffer[rx_index] = '\0';

                // Erase on terminal — no echo of \b or 0x7F itself
                while (!(USART2->SR & USART_SR_TXE));
                USART2->DR = '\b';
                while (!(USART2->SR & USART_SR_TXE));
                USART2->DR = ' ';
                while (!(USART2->SR & USART_SR_TXE));
                USART2->DR = '\b';
            }
            // If rx_index == 0, do nothing — already at start of line
        }
        else if (received >= 0x20 && received <= 0x7E)
        {
            // Only echo printable ASCII characters
            while (!(USART2->SR & USART_SR_TXE));
            USART2->DR = received;

            if (rx_index < sizeof(rx_buffer) - 1)
            {
                rx_buffer[rx_index++] = received;
            }
            else
            {
                // Buffer full
                while (!(USART2->SR & USART_SR_TXE));
                USART2->DR = '\a';
            }
        }
        // All other control characters are silently ignored
    }
}
void process_command(char *cmd)
{
	printf("\r\n");  // newline after echoed command
    if (strcmp(cmd, "help") == 0)
    {
        printf("Available Commands\r\n");
        printf("------------------\r\n");
        printf("help      - Show command list\r\n");
        printf("led on    - Turn LED ON\r\n");
        printf("led off   - Turn LED OFF\r\n");
        printf("status    - Display system status\r\n");
        printf("version   - Firmware version\r\n");
        printf("fast      - LED blink: FAST (100ms)\r\n");
        printf("slow      - LED blink: SLOW (1000ms)\r\n");
        printf("stop      - LED blink: STOPPED\r\n");
        printf("start     - LED blink: STARTED\r\n");
    }
    else if (strcmp(cmd, "led on") == 0)
    {
        HAL_GPIO_WritePin(LD2_GPIO_Port,
                          LD2_Pin,
                          GPIO_PIN_SET);

        printf("LED is ON\r\n");
    }
    else if (strcmp(cmd, "led off") == 0)
    {
        HAL_GPIO_WritePin(LD2_GPIO_Port,
                          LD2_Pin,
                          GPIO_PIN_RESET);

        printf("LED is OFF\r\n");
    }
    else if (strcmp(cmd, "status") == 0)
    {
        GPIO_PinState led =
            HAL_GPIO_ReadPin(LD2_GPIO_Port, LD2_Pin);

        printf("System Status\r\n");
        printf("-------------\r\n");
        printf("Uptime        : %lu ms\r\n", HAL_GetTick());
        printf("LED State     : %s\r\n", led ? "ON" : "OFF");
    }
    else if (strcmp(cmd, "version") == 0)
    {
        printf("UART CLI Demo v1.0\r\n");
    }
    else if (strcmp(cmd, "fast") == 0) {
        led_timer.period_ms = 100;
        printf("LED blink: FAST (100ms)\r\n");
    }
    else if (strcmp(cmd, "slow") == 0) {
        led_timer.period_ms = 1000;
        printf("LED blink: SLOW (1000ms)\r\n");
    }
    else if (strcmp(cmd, "stop") == 0) {
        led_timer.enabled = 0;
        printf("LED blink: STOPPED\r\n");
    }
    else if (strcmp(cmd, "start") == 0) {
        led_timer.enabled = 1;
        led_timer.period_ms = 500;
        printf("LED blink: STARTED (500ms)\r\n");
    }
    else
    {
        printf("Unknown Command\r\n");
        printf("Type 'help' for available commands\r\n");
    }

    printf("> ");  // prompt for next command

}
/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

	/* USER CODE BEGIN 1 */

	/* USER CODE END 1 */

	/* MCU Configuration--------------------------------------------------------*/

	/* Reset of all peripherals, Initializes the Flash interface and the Systick. */
	HAL_Init();

	/* USER CODE BEGIN Init */

	/* USER CODE END Init */

	/* Configure the system clock */
	SystemClock_Config();

	/* USER CODE BEGIN SysInit */

	/* USER CODE END SysInit */

	/* Initialize all configured peripherals */
	MX_GPIO_Init();
	MX_USART2_UART_Init();
	/* USER CODE BEGIN 2 */
	// Arm the UART receive interrupt — receive 1 byte into rx_byte

	HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
	uint32_t counter = 0;
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("\r\n");
	printf("STM32 UART Command Line Interface\r\n");
	printf("Type 'help' for commands.\r\n");
	printf("\r\n ");
	printf("> ");  // initial prompt
	/* USER CODE END 2 */

	/* Infinite loop */
	/* USER CODE BEGIN WHILE */
	while (1) {

		if (cmd_ready) {
			process_command(rx_buffer);
			cmd_ready = 0;
			rx_index = 0;
		}
        // Blink LED every 500ms — no blocking
        if (timer_expired(&led_timer)) {
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        }

        // Only print auto-status if user is NOT mid-command
        if (timer_expired(&status_timer)) {
            if (rx_index == 0) {   // only print if buffer is empty (not mid-typing)
                printf("\r\n[AUTO] Uptime: %lus\r\n> ", HAL_GetTick()/1000);
            }
        }

        // Increment counter every second
        if (timer_expired(&counter_timer)) {
            counter++;
        }
	}
	/* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
	RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
	RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };

	/** Configure the main internal regulator output voltage
	 */
	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

	/** Initializes the RCC Oscillators according to the specified parameters
	 * in the RCC_OscInitTypeDef structure.
	 */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
	RCC_OscInitStruct.PLL.PLLM = 16;
	RCC_OscInitStruct.PLL.PLLN = 336;
	RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
	RCC_OscInitStruct.PLL.PLLQ = 7;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
		Error_Handler();
	}

	/** Initializes the CPU, AHB and APB buses clocks
	 */
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
			| RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
		Error_Handler();
	}
}

/**
 * @brief USART2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART2_UART_Init(void) {

	/* USER CODE BEGIN USART2_Init 0 */

	/* USER CODE END USART2_Init 0 */

	/* USER CODE BEGIN USART2_Init 1 */

	/* USER CODE END USART2_Init 1 */
	huart2.Instance = USART2;
	huart2.Init.BaudRate = 115200;
	huart2.Init.WordLength = UART_WORDLENGTH_8B;
	huart2.Init.StopBits = UART_STOPBITS_1;
	huart2.Init.Parity = UART_PARITY_NONE;
	huart2.Init.Mode = UART_MODE_TX_RX;
	huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart2.Init.OverSampling = UART_OVERSAMPLING_16;
	if (HAL_UART_Init(&huart2) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN USART2_Init 2 */

	/* USER CODE END USART2_Init 2 */

}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void) {
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };
	/* USER CODE BEGIN MX_GPIO_Init_1 */

	/* USER CODE END MX_GPIO_Init_1 */

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

	/*Configure GPIO pin : PC13 */
	GPIO_InitStruct.Pin = GPIO_PIN_13;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

	/*Configure GPIO pin : LD2_Pin */
	GPIO_InitStruct.Pin = LD2_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

	/* EXTI interrupt init*/
	HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

	/* USER CODE BEGIN MX_GPIO_Init_2 */

	/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
int __io_putchar(int ch) {
	HAL_UART_Transmit(&huart2, (uint8_t*) &ch, 1, HAL_MAX_DELAY);
	return ch;
}

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1) {
	}
	/* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
