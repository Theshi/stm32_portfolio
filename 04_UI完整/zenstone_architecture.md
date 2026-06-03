
# ZenStone 嵌入式项目架构设计（STM32 + FreeRTOS + LVGL + ESP8266）

## 一、系统总体架构

```
+----------------------+
|      LVGL_Task       |
| UI显示/动画/切屏     |
+----------+-----------+
           ↑
           | Task Notification
           ↓
+----------------------+
|      Boot_Task       |
| 硬件自检/系统初始化  |
+----------+-----------+
           |
           | 调用驱动函数
           ↓
+----------------------+
|   Driver Layer       |
| W25Q64 / ESP8266 / RTC
+----------------------+
```

---

## 二、核心设计原则

### 1. LVGL 单任务原则
所有 LVGL API 必须在 LVGL_Task 中调用：
- lv_scr_load()
- lv_label_set_text()
- lv_bar_set_value()

❌ 禁止在 Boot_Task 中直接操作 UI

---

### 2. Boot_Task 只负责逻辑
Boot_Task 职责：
- W25Q64 自检
- RTC 自检
- ESP8266 自检
- 系统状态判断
- 发送任务通知

---

### 3. LVGL_Task 只负责 UI
LVGL_Task 职责：
- lv_timer_handler()
- UI刷新
- 接收状态通知
- 界面切换

---

## 三、任务划分

### 1. Boot_Task（系统自检任务）

```c
void Boot_Task(void *arg)
{
    check_w25q64();
    xTaskNotify(LVGL_TaskHandle, BOOT_W25Q64_OK, eSetValueWithOverwrite);

    check_esp8266();
    xTaskNotify(LVGL_TaskHandle, BOOT_ESP_OK, eSetValueWithOverwrite);

    check_rtc();
    xTaskNotify(LVGL_TaskHandle, BOOT_RTC_OK, eSetValueWithOverwrite);

    xTaskNotify(LVGL_TaskHandle, BOOT_DONE, eSetValueWithOverwrite);

    vTaskDelete(NULL);
}
```

---

### 2. LVGL_Task（UI任务）

```c
void LVGL_Task(void *arg)
{
    uint32_t state;

    while (1)
    {
        lv_timer_handler();

        if (xTaskNotifyWait(0, 0xFFFFFFFF, &state, 0) == pdTRUE)
        {
            switch (state)
            {
                case BOOT_W25Q64_OK:
                    lv_label_set_text(label_boot, "W25Q64 OK");
                    break;

                case BOOT_ESP_OK:
                    lv_label_set_text(label_boot, "ESP8266 OK");
                    break;

                case BOOT_DONE:
                    lv_scr_load(main_screen);
                    break;
            }
        }

        vTaskDelay(5);
    }
}
```

---

## 四、界面设计结构（Screen模型）

### 1. Boot Screen（开机界面）

```
boot_screen
├── label_logo
├── label_status
└── progress_bar
```

用途：
- 显示自检过程
- 显示进度

---

### 2. Main Screen（主界面）

```
main_screen
├── label_time
├── label_weather
├── label_wifi
└── buttons
```

用途：
- 时间显示
- 天气显示
- 控制按钮

---

## 五、状态驱动设计（核心思想）

```c
typedef enum {
    BOOT_START,
    BOOT_W25Q64_OK,
    BOOT_ESP_OK,
    BOOT_RTC_OK,
    BOOT_DONE
} boot_state_t;
```

---

## 六、通信机制（Task Notification）

### 优点：
- 零内存开销
- 高速
- 适合状态通知

### 流程：

```
Boot_Task ---> Notify ---> LVGL_Task ---> UI更新
```

---

## 七、LVGL切屏机制

### 显示Boot界面
```c
lv_scr_load(boot_screen);
```

### 切换主界面
```c
lv_scr_load_anim(main_screen,
                 LV_SCR_LOAD_ANIM_FADE_ON,
                 500,
                 0,
                 false);
```

---

## 八、系统运行流程

```
系统启动
   ↓
创建 Boot_Task
   ↓
创建 LVGL_Task
   ↓
显示 Boot Screen
   ↓
Boot_Task 开始自检
   ↓
不断发送状态通知
   ↓
LVGL_Task 更新UI
   ↓
BOOT_DONE
   ↓
切换 Main Screen
   ↓
Boot_Task 删除
```

---

## 九、设计总结

### ✔ 正确点
- UI与逻辑解耦
- RTOS任务职责清晰
- 状态驱动设计
- LVGL单线程安全模型

### ✔ 架构特点
- 事件驱动
- 轻量通信
- 易扩展（天气/MQTT/OTA）

---

## 十、后续扩展方向

- ESP8266 网络任务独立化
- 天气/时间服务模块化
- JSON解析层
- UI动画系统
- OTA升级支持
