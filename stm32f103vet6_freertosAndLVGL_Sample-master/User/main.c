#include "stm32f10x.h"                  // Device header
#include "SysTick.h"

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_usart.h"
#include "bsp_ili9341_lcd.h"

#include "lvgl.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"

//启动任务
#define StartUpTask_STACKSIZE 256
#define StartUpTask_PRIO			1
TaskHandle_t StartUpTask_Handle;
void StartUpTask(void * p);

//LVGL任务
#define LvglTask_STACKSIZE 512
#define LvglTask_PRIO			1
TaskHandle_t LvglTask_Handle;
void LvglTask(void * p);

//开启任务
void StartUpTask(void *p){
	while(1){
		vTaskDelay(1000);
		printf("run...\n");
	}
}
//LVGL任务
void LvglTask(void * p){
	while(1){
		vTaskDelay(5);
		lv_timer_handler();
	}
}

void LVGL_Test(){
	lv_obj_t * btn = lv_btn_create(lv_scr_act());
	lv_obj_t * label = lv_label_create(btn);
	lv_label_set_text(label,"hello");
	lv_obj_center(btn);
}

int main(){
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
	USART_Config();
	printf("窗口初始化完毕..\n");
	lv_init();
	lv_port_disp_init();
	lv_port_indev_init();
	LVGL_Test();
	printf("屏幕初始化完毕..\n");
	SysTick_Init();
	xTaskCreate(StartUpTask,"StartUpTask",StartUpTask_STACKSIZE,NULL,StartUpTask_PRIO,&StartUpTask_Handle);
	xTaskCreate(LvglTask,"LvglTask",LvglTask_STACKSIZE,NULL,LvglTask_PRIO,&LvglTask_Handle);
	vTaskStartScheduler();
}

