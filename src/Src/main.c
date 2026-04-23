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

/* USER CODE BEGIN PV */


// Font chữ "bạn chọn" (Cao 8 pixel, rộng 5 pixel)
// Được ánh xạ vào 8 LED đầu tiên (PA0 đến PA7), 2 LED PB0 PB1 để tắt làm viền dưới
const uint16_t FONT_CHU[8][5] = {
		 {0x000E, 0x001F, 0x003E, 0x001F, 0x000E}, // Trái tim (Heart)
		 {0x0000, 0x0000, 0x0000, 0x0000, 0x0000}, // Space (Khoảng trắng)
		 {0x003E, 0x0041, 0x0041, 0x0041, 0x0022}, // C
		 {0x007F, 0x0049, 0x0049, 0x0049, 0x0041}, // E
		 {0x007F, 0x0049, 0x0049, 0x0049, 0x0041}, // E
		 {0x003E, 0x0041, 0x0041, 0x0041, 0x0022}, // C
		 {0x0000, 0x0000, 0x0000, 0x0000, 0x0000}, // Space (Khoảng trắng)
		 {0x000E, 0x001F, 0x003E, 0x001F, 0x000E} // Trái tim (Heart)
};

// Font chữ số Đồng hồ (0-9 và dấu :)
const uint16_t FONT_CLOCK[11][5] = {
    {0x003E, 0x0051, 0x0049, 0x0045, 0x003E}, // 0
    {0x0000, 0x0042, 0x007F, 0x0040, 0x0000}, // 1
    {0x0042, 0x0061, 0x0051, 0x0049, 0x0046}, // 2
    {0x0021, 0x0041, 0x0045, 0x004B, 0x0031}, // 3
    {0x0018, 0x0014, 0x0012, 0x007F, 0x0010}, // 4
    {0x0027, 0x0045, 0x0045, 0x0045, 0x0039}, // 5
    {0x003C, 0x004A, 0x0049, 0x0049, 0x0030}, // 6
    {0x0001, 0x0071, 0x0009, 0x0005, 0x0003}, // 7
    {0x0036, 0x0049, 0x0049, 0x0049, 0x0036}, // 8
    {0x0006, 0x0049, 0x0049, 0x0029, 0x001E}, // 9
    {0x0000, 0x0036, 0x0036, 0x0000, 0x0000}  // 10 (hay dấu ":")
};

// --- CÔNG TẮC CHỌN CHẾ ĐỘ ---
// Thay đổi số này: 0 = Hiển thị chữ , 1 = Hiển thị Đồng hồ
uint8_t display_mode = 1;

volatile uint8_t sync_flag = 0; // báo hiệu hết 1 vòng
volatile uint32_t rotation_time_us = 0; // thời gian quay 1 vòng
volatile uint32_t last_interrupt_time = 0; // mốc thời gian từng vòng, hỗ trợ tính thời gian quay 1 vòng

uint32_t last_rtc_read = 0; // Biến lưu thời gian đọc từ RTC
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

void delay_us(uint16_t us);
uint8_t bcd_to_dec(uint8_t val);
void RTC_GetTime(uint8_t *h, uint8_t *m, uint8_t *s);
void POV_WriteColumn(uint16_t column_data);


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// 1. Hàm trễ micro-giây, tạo độ trễ chính xác
void delay_us(uint16_t us) {
    uint16_t start = __HAL_TIM_GET_COUNTER(&htim2);
    while ((uint16_t)(__HAL_TIM_GET_COUNTER(&htim2) - start) < us);
}

// 2. Hàm xuất dữ liệu siêu tốc bằng thanh ghi BSRR thay vì hàm HAL
void POV_WriteColumn(uint16_t column_data) {
    uint16_t portA_data = column_data & 0x00FF; // Lấy 8 bit thấp cho PA0-PA7
    uint16_t portB_data = (column_data & 0x0300) >> 8; // Lấy bit 8, 9 cho PB0-PB1

    // 16 bit cao dùng để tắt (Reset), 16 bit thấp dùng để bật (Set)
    GPIOA->BSRR = (0x00FF << 16) | portA_data;
    GPIOB->BSRR = (0x0003 << 16) | portB_data;
}

// 3. Hàm Ngắt Cảm Biến Hall (Kèm lọc nhiễu 5ms vì motor 4500 RPM quay cực nhanh)
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_8) {
    	uint16_t current_time = __HAL_TIM_GET_COUNTER(&htim2);
    	        uint16_t time_diff = current_time - last_interrupt_time;

    	        // Lọc nhiễu: Thời gian 1 vòng phải lớn hơn 5000us (5ms)
    	        if (time_diff > 5000) {
    	            rotation_time_us = time_diff; // Lưu thẳng ra micro-giây
    	            last_interrupt_time = current_time;
    	            sync_flag = 1;
        }
    }
}
// Hàm cần thiết để sử dụng RTC DS3231
// 1. Hàm chuyển đổi định dạng BCD (từ RTC) sang số thập phân bình thường
uint8_t bcdToDec(uint8_t val) {
    return (uint8_t)( (val/16*10) + (val%16) );
}

// 2. Hàm chuyển đổi số thập phân thành BCD (Dùng khi muốn CÀI ĐẶT giờ)
uint8_t decToBcd(uint8_t val) {
    return (uint8_t)( (val/10*16) + (val%10) );
}

// 3. Hàm cài đặt giờ (Chỉ chạy 1 lần để thiết lập thời gian cho mạch)
void RTC_SetTime(uint8_t hr, uint8_t min, uint8_t sec) {
    uint8_t time_data[3];
    time_data[0] = decToBcd(sec);
    time_data[1] = decToBcd(min);
    time_data[2] = decToBcd(hr);
    // Ghi dữ liệu vào thanh ghi 0x00 của chip DS3231 (Địa chỉ I2C là 0xD0)
    HAL_I2C_Mem_Write(&hi2c1, 0xD0, 0x00, 1, time_data, 3, 100);
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
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */

      HAL_TIM_Base_Start(&htim2); // khởi động bộ đếm

      // BỎ COMMENT DÒNG DƯỚI ĐÂY ĐỂ NẠP GIỜ VN HIỆN TẠI VÀO MẠCH
      // Sau khi nạp code chạy thử lần 1,comment dòng này lại (//) và nạp lại code lần 2
      // RTC_SetTime(giờ, phút, giây); - tự nhập số

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

      // Tạo mảng chứa 8 ký tự của đồng hồ: H H : M M : S S
        uint8_t clock_digits[8] = {0,0,10,0,0,10,0,0};

        while (1)
  {
	  // --- NẾU ĐANG Ở CHẾ ĐỘ ĐỒNG HỒ: ĐỌC RTC MỖI DƯỚI 1 GIÂY ---
	        if (display_mode == 1 && (HAL_GetTick() - last_rtc_read > 1000)) {
	            uint8_t rtcData[3];
	            // Đọc 3 byte thời gian (giờ, phút, giây) từ DS3231
	            if (HAL_I2C_Mem_Read(&hi2c1, 0xD0, 0x00, 1, rtcData, 3, 100) == HAL_OK) {
	                uint8_t sec = bcdToDec(rtcData[0] & 0x7F);
	                uint8_t min = bcdToDec(rtcData[1]);
	                uint8_t hr  = bcdToDec(rtcData[2] & 0x3F);

	                // Tách các con số để đưa vào mảng hiển thị
	                clock_digits[0] = hr / 10;
	                clock_digits[1] = hr % 10;
	                // clock_digits[2] = 10 (:); - đã cấu hình ở trên
	                clock_digits[3] = min / 10;
	                clock_digits[4] = min % 10;
	                // clock_digits[5] = 10 (:);
	                clock_digits[6] = sec / 10;
	                clock_digits[7] = sec % 10;
	            }
	            last_rtc_read = HAL_GetTick(); // Cập nhật lại mốc thời gian
	        }
	        // --- TIẾN HÀNH QUÉT LED KHI NAM CHÂM LƯỚT QUA ---
	        if (sync_flag == 1) {
	            sync_flag = 0;
	            // Tính toán độ rộng của nét chữ (Chia chu vi vòng tròn n lát  => rotation_time_us/ n;
	            uint32_t col_delay = rotation_time_us / 160; // trường hợp n = 160
	            uint32_t offset_delay = rotation_time_us / 6;
	            delay_us(offset_delay);


	            // ==========================================
	            // LỰA CHỌN 1: VẼ ĐỒNG HỒ hay display == 1
	            // ==========================================
	            if (display_mode == 1) {
	                for (int i = 0; i < 8; i++) { // Quét 8 ký tự
	                    for (int col = 0; col < 5; col++) {
	                        POV_WriteColumn(FONT_CLOCK[clock_digits[i]][col]);
	                        delay_us(col_delay);
	                        // Khử nhòe
	                        POV_WriteColumn(0x0000);
	                        delay_us(col_delay / 2);
	                    }
	                    // Khoảng cách giữa các số
	                    POV_WriteColumn(0x0000);
	                    delay_us(col_delay * 2);
	                }
	            }
	            // ==========================================
	            // LỰA CHỌN 2: VẼ CHỮ hay display == 0
	            // ==========================================
	            else {
	                for (int i = 0; i < 8; i++) { // i < số lượng ký tự
	                    for (int col = 0; col < 5; col++) {
	                        POV_WriteColumn(FONT_CHU[i][col]);
	                        delay_us(col_delay);
	                        POV_WriteColumn(0x0000);
	                        delay_us(col_delay / 2);
	                    }
	                    POV_WriteColumn(0x0000);
	                    delay_us(col_delay * 2);
	                }
	            }

	            POV_WriteColumn(0x0000); // Tắt sạch chờ vòng tiếp theo
	        }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
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

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 71;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
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
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

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
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1, GPIO_PIN_RESET);

  /*Configure GPIO pins : PA0 PA1 PA2 PA3
                           PA4 PA5 PA6 PA7 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PA8 */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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
