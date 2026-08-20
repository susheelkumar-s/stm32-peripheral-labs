/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  *
  * Minimal OLED table demo — I2C1 + SSD1306 OLED only.
  * Alternates between two example tables every 2 seconds.
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
#include <stdlib.h>
#include "ssd1306.h"
#include "ssd1306_fonts.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    LED_OFF,
    LED_ON,
    LED_MANUAL,   // exact duty set via DUTY <value>
    LED_BLINK,    // period controlled by led_timer.period_ms
    LED_FADE
} LedMode;

volatile LedMode led_mode = LED_OFF;
volatile uint16_t manual_duty = 0;   // used only when led_mode == LED_MANUAL
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
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
/* Global variables */
uint8_t rx_byte;
char rx_buffer[64];
uint8_t rx_index = 0;
volatile uint8_t cmd_ready = 0;
volatile uint8_t button_flag = 0;
volatile uint32_t button_press_count = 0;
volatile uint32_t last_button_time = 0;
volatile uint8_t led_state = 0;
uint16_t duty = 0;
int8_t   direction = 1;      // 1 = increasing, -1 = decreasing
uint8_t  fade_enabled = 1;
volatile uint8_t auto_status_enabled = 1;   // on by default, matches current behavior
uint8_t current_screen = 1;

/* Software timers — global so process_command() can access them */
SoftTimer led_timer     = {0, 500,  1};
SoftTimer status_timer  = {0, 3000, 1};
SoftTimer counter_timer = {0, 1000, 1};
SoftTimer fade_timer = {0, 10, 1};  // update every 10ms
SoftTimer display_timer = {0, 1000, 1};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

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
	if (strcmp(cmd, "help") == 0) {
	    printf("Commands:\r\n");
	    printf("  led on / led off     		 - digital on/off\r\n");
	    printf("  fade on / fade off   		 - breathing fade effect\r\n");
	    printf("  duty <0-1000>        		 - set exact duty cycle\r\n");
	    printf("  duty max / duty off  		 - full brightness / off\r\n");
	    printf("  status               		 - system info\r\n");
	    printf("  fast / slow / stop / start     - blink control\r\n");
	    printf("  speed <1-100>        		 - Fade speed\r\n");
	    printf("  status quite    	         - Stop periodic uptime display\r\n");
	    printf("  status verbose                 - Resume periodic uptime display\r\n");
	    printf("  REGS                           - Print live TIM2 register values\r\n");
	    printf("  SCREEN1                        - Screen : Uptime on OLED\r\n");
	    printf("  SCREEN2                        - Screen : System Info on OLED\r\n");
		printf("\r\n ");

	}
    else if (strcmp(cmd, "status") == 0)
    {
        uint32_t ccr = __HAL_TIM_GET_COMPARE(&htim2, TIM_CHANNEL_1);
        uint32_t duty_percent = (ccr * 100) / 1000;

        printf("=== SYSTEM STATUS ===\r\n");
        printf("Uptime:        %lu ms\r\n", HAL_GetTick());
        printf("LED Duty:      %lu/1000 (%lu%%)\r\n", ccr, duty_percent);
        printf("LED State:     %s\r\n", ccr > 0 ? "ON" : "OFF");
        printf("Fade:          %s\r\n", (led_mode == LED_FADE) ? "ON" : "OFF");
        printf("Button Count:  %lu\r\n", button_press_count);
        printf("====================\r\n");
    }
    else if (strcmp(cmd, "led on") == 0) {
        led_mode = LED_ON;
        printf("LED ON (full brightness)\r\n");
    }
    else if (strcmp(cmd, "led off") == 0) {
        led_mode = LED_OFF;
        printf("LED is OFF\r\n");
    }
    else if (strcmp(cmd, "fade on") == 0) {
        led_mode = LED_FADE;
        duty = 0;
        direction = 1;
        printf("LED FADE: ON\r\n");
    }
    else if (strcmp(cmd, "fade off") == 0) {
        led_mode = LED_OFF;
        printf("LED FADE: OFF\r\n");
    }
    else if (strcmp(cmd, "fast") == 0) {
        led_mode = LED_BLINK;
        led_timer.period_ms = 100;
        printf("LED blink: FAST (100ms)\r\n");
    }
    else if (strcmp(cmd, "slow") == 0) {
        led_mode = LED_BLINK;
        led_timer.period_ms = 1000;
        printf("LED blink: SLOW (1000ms)\r\n");
    }
    else if (strcmp(cmd, "stop") == 0) {
        led_mode = LED_OFF;
        printf("LED blink: STOPPED\r\n");
    }
    else if (strcmp(cmd, "start") == 0) {
        led_mode = LED_BLINK;
        led_timer.period_ms = 500;
        printf("LED blink: STARTED (500ms)\r\n");
    }
    else if (strcmp(cmd, "duty max") == 0) {
        led_mode = LED_MANUAL;
        manual_duty = 1000;
        printf("Duty set to MAX (1000)\r\n");
    }
    else if (strcmp(cmd, "duty off") == 0) {
        led_mode = LED_MANUAL;
        manual_duty = 0;
        printf("Duty set to 0\r\n");
    }
    else if (strncmp(cmd, "duty duty", 5) == 0) {
        int value = atoi(cmd + 5);
        if (value < 0)    value = 0;
        if (value > 1000) value = 1000;
        led_mode = LED_MANUAL;
        manual_duty = (uint16_t)value;
        printf("Duty set to %d\r\n", value);
    }
    else if (strncmp(cmd, "speed ", 6) == 0) {
        // "SPEED 5"  = very fast fade (5ms steps)
        // "SPEED 20" = slow fade (20ms steps)
        int val = atoi(cmd + 6);
        if (val >= 1 && val <= 100) {
            fade_timer.period_ms = val;
            printf("Fade speed: %dms per step\r\n", val);
        } else {
            printf("Error: SPEED must be 1-100\r\n");
        }
    }
    else if (strcmp(cmd, "status quite") == 0) {
        auto_status_enabled = 0;
        printf("Auto-status: OFF\r\n");
    }
    else if (strcmp(cmd, "status verbose") == 0) {
        auto_status_enabled = 1;
        printf("Auto-status: ON\r\n");
    }
    else if (strcmp(cmd, "REGS") == 0) {
        printf("=== TIM2 Registers ===\r\n");
        printf("PSC  (Prescaler):      %lu\r\n", TIM2->PSC);
        printf("ARR  (Auto-reload):    %lu\r\n", TIM2->ARR);
        printf("CCR1 (Compare ch1):    %lu\r\n", TIM2->CCR1);
        printf("CNT  (Current count):  %lu\r\n", TIM2->CNT);
        printf("CR1  (Control):        0x%04lX\r\n", TIM2->CR1);
        printf("=====================\r\n");
    }
    else if (strcmp(cmd, "version") == 0)
    {
        printf("UART CLI Demo v1.0\r\n");
    }
    else if (strcmp(cmd, "SCREEN1") == 0) {
        current_screen = 1;
        printf("View Uptime on OLED Screen\r\n");
    }
    else if (strcmp(cmd, "SCREEN2") == 0) {
        current_screen = 2;
        printf("View System Info on OLED Screen\r\n");
    }
    else
    {
        printf("Unknown Command\r\n");
        printf("Type 'help' for available commands\r\n\r\n");
    }

    printf("> ");  // prompt for next command

}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

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
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  // Start PWM on TIM2 Channel 1
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  // Arm the UART receive interrupt — receive 1 byte into rx_byte
  HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
	uint32_t counter = 0;
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("\r\n");
	printf("STM32 UART Command Line Interface\r\n");
	printf("Type 'help' for commands.\r\n");
	printf("\r\n ");
	printf("> ");  // initial prompt
  // Initialize display
  ssd1306_Init();


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	    if (cmd_ready) {
	        process_command(rx_buffer);
			cmd_ready = 0;
			rx_index = 0;
	    }
	    if (timer_expired(&display_timer)) {
	        ssd1306_Fill(Black);

	        if (current_screen == 1) {
	            ssd1306_SetCursor(0, 0);
	            ssd1306_WriteString("STM32 Monitor", Font_7x10, White);
	            char buf[32];
	            ssd1306_SetCursor(0, 15);
	            sprintf(buf, "Uptime: %lus", HAL_GetTick()/1000);
	            ssd1306_WriteString(buf, Font_7x10, White);
	        }
	        else if (current_screen == 2) {
	            ssd1306_SetCursor(0, 0);
	            ssd1306_WriteString("System Info", Font_7x10, White);
	            ssd1306_SetCursor(0, 15);
	            ssd1306_WriteString("STM32F401RE", Font_7x10, White);
	            ssd1306_SetCursor(0, 30);
	            ssd1306_WriteString("84MHz Cortex-M4", Font_7x10, White);
	        }

	        ssd1306_UpdateScreen();
	    }

        // Only print auto-status if user is NOT mid-command
		if (timer_expired(&status_timer)) {
		    if (auto_status_enabled && rx_index == 0) {
		        printf("\r\n[AUTO] Uptime: %lus\r\n> ", HAL_GetTick()/1000);
		    }
		}
        // Increment counter every second
        if (timer_expired(&counter_timer)) {
            counter++;
        }

        switch (led_mode)
        {
            case LED_OFF:
                __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
                break;

            case LED_ON:
                __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 1000);
                break;

            case LED_MANUAL:
                __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, manual_duty);
                break;

            case LED_BLINK:
                if (timer_expired(&led_timer)) {
                    static uint8_t on = 0;
                    on = !on;
                    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, on ? 1000 : 0);
                }
                break;

            case LED_FADE:
                if (timer_expired(&fade_timer)) {
                    duty += direction * 10;
                    if (duty >= 1000) { duty = 1000; direction = -1; }
                    if (duty <= 0)    { duty = 0;    direction = 1;  }
                    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);
                }
                break;
        }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

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
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 83;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 500;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

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
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
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
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
//re target printf
int __io_putchar(int ch)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_13)
    {
        uint32_t now = HAL_GetTick();
        if (now - last_button_time > 50)   // debounce window
        {
            button_press_count++;
            last_button_time = now;
        }
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
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
