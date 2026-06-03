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

//UI界面图片源声明
LV_IMG_DECLARE(GitHub_Logo);
LV_IMG_DECLARE(WIFI_Logo);
LV_IMG_DECLARE(TEMP_Logo);
LV_IMG_DECLARE(LVGL_Logo);

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
	//主界面UI设计
	lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);//设置背景颜色为白色
	lv_obj_t* img_GitHub = lv_img_create(lv_scr_act());//创建GitHub图标图像
	lv_img_set_src(img_GitHub, &GitHub_Logo);
	lv_obj_set_pos(img_GitHub, 265, 213);
	lv_obj_update_layout(img_GitHub);
	lv_obj_t* img_WIFI = lv_img_create(lv_scr_act());//创建WIFI图标图像
	lv_img_set_src(img_WIFI,&WIFI_Logo);
	lv_obj_set_pos(img_WIFI, 142, 10);
	lv_obj_update_layout(img_WIFI);
	lv_obj_t* img_LVGL = lv_img_create(lv_scr_act());//创建LVGL_Logo图标图像
	lv_img_set_src(img_LVGL, &LVGL_Logo);
	lv_obj_set_pos(img_LVGL, 10, 10);
	lv_obj_set_style_bg_opa(img_LVGL, LV_OPA_0, LV_STATE_DEFAULT);
	lv_obj_update_layout(img_LVGL);
	lv_obj_t* img_TEMP = lv_img_create(lv_scr_act());//创建TEMP图标图像
	lv_img_set_src(img_TEMP, &TEMP_Logo);
	lv_obj_set_pos(img_TEMP, 189, 190);
	lv_obj_update_layout(img_TEMP);

	lv_obj_t* line = lv_line_create(lv_scr_act());//创建分割线
	static lv_point_t line_points[] = { {0, 136}, {320, 136} };//分割线的起点和终点坐标
	lv_line_set_points(line, line_points, 2);
	lv_obj_set_style_line_color(line, lv_color_hex(0x000000),0);
	lv_obj_set_style_line_width(line, 3, 0);
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

