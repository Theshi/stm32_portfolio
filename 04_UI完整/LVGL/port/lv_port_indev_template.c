/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev_template.h"
#include "lvgl.h"
#include "bsp_xpt2046_lcd.h"
/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void touchpad_init(void);
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
static bool touchpad_is_pressed(void);
static void touchpad_get_xy(lv_coord_t * x, lv_coord_t * y);

lv_indev_t * indev_touchpad;

void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;
    touchpad_init();
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    indev_touchpad = lv_indev_drv_register(&indev_drv);
}
/*Initialize your touchpad*/
static void touchpad_init(void)
{
    /*Your code comes here*/
		XPT2046_Init();
}

/*Will be called by the library to read the touchpad*/
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
   static strType_XPT2046_Coordinate cinfo = {-1,-1,-1,-1};
	
	if(XPT2046_TouchDetect() == TOUCH_PRESSED)
	{
		//获取触摸坐标
		XPT2046_Get_TouchedPoint(&cinfo,strXPT2046_TouchPara);
		data->state = LV_INDEV_STATE_PR;
	}
	else
	{
		//调用触摸被释放时的处理函数，可在该函数编写自己的触摸释放处理过程
		data->state = LV_INDEV_STATE_REL;
	}
    /*Set the last pressed coordinates*/
    data->point.x = cinfo.x;
    data->point.y = cinfo.y;
}
