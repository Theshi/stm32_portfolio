2026/7/13日晚上   天气晴非常热
  完结自己的第一个练习项目，虽然还是很粗糙，我也不知道学到了那些东西，最后网络的部分全靠AI帮助修改的，josn的解析我完全不想自己想，只好靠着AI帮忙做了，继续再接再厉吧，我希望自己的成长可以用这里出发吧。
距离15号还有三天，接下来的三天对这个项目进行学习总结复盘，然后将复盘的内容放到这里。
  继续加油呀！！！
# STM32 智能天气时钟 —— 项目复盘总结
> 从零搭建：FreeRTOS + LVGL + ESP8266 + DHT11 整合实战
---
## 一、最值得学习的内容
### 1.1 FreeRTOS 任务划分思想
**核心原则：一个任务做一件事。**
| 任务 | 职责 | 精髓 |
|------|------|------|
| `Boot_Task` | 硬件自检 → 通知开工 → 自删除 | 一次性任务，不阻塞主线 |
| `HTTP_Task` | WiFi + HTTP 全流程 | 独占外设，避免竞争 |
| `LVGL_Task` | 读共享数据 → 刷新显示 | 纯读取，不写共享变量 |

**位置参考：**
- 任务创建与优先级定义：[Task/task_http.h:18-19](Task/task_http.h#L18-L19)
- Boot_Task 自检流程主导航：[Task/task_boot.c:33-88](Task/task_boot.c#L33-L88)
- xTaskNotify 发通知给 HTTP_Task：[Task/task_boot.c:80](Task/task_boot.c#L80)
- HTTP_Task 接收通知并执行网络流：[Task/task_http.c:149-156](Task/task_http.c#L149-L156)
### 1.2 xTaskNotify 任务间通信模式
比全局变量轮询优雅得多。Boot_Task 不需要知道 HTTP_Task 什么时候做完，发完通知就退。
**关键代码：**
- 发送端（Boot_Task → HTTP_Task）：`xTaskNotify(HTTPTask_Handle, NET_CMD_START, eSetValueWithOverwrite)` — [Task/task_boot.c:80](Task/task_boot.c#L80)
- 接收端：`xTaskNotifyWait(0, 0xFFFFFFFF, &cmd, 0)` — [Task/task_http.c:149](Task/task_http.c#L149)
- 命令字定义：`#define NET_CMD_START 0x100` — [Task/task_boot.h](Task/task_boot.h)

**值得学习的写法：** xTaskNotify 比队列轻量（无需分配内存），适合"通知一下就行"的场景。

### 1.3 非阻塞 HTTP 状态机

没有用 `ESP8266_Cmd`（它用 `Delay_ms` 忙等），而是手写了一个状态机，每 50ms 跑一次，用 `vTaskDelay` 让出 CPU。

**位置：[Service/http_client.c:191-368](Service/http_client.c#L191-L368)**

状态流转：
```
IDLE → CIPSTART → WAIT_CONNECT → CIPSEND → WAIT_PROMPT → SEND_HTTP → WAIT_RESPONSE → DONE/ERROR → IDLE
```

**值得学习的写法：**
1. 状态机把一次 HTTP 请求拆成 6 个异步步骤，每个状态只做一件事
2. 等待时用 `vTaskDelay` + 超时计数，不阻塞其他任务
3. 响应地址存到 `http_resp_buf` 后统一解析，与状态机逻辑分离

### 1.4 ESP8266 驱动适应性封装

**位置：[Driver/ESP8266/bsp_esp8266.c:156-181](Driver/ESP8266/bsp_esp8266.c#L156-L181)**

**核心函数 `ESP8266_Cmd`：**
- 发 AT 命令 → 等指定时间 → 在缓冲区搜索应答关键字
- 简单可靠，但 `Delay_ms` 是忙等，在非网络任务里调用会饿死其他任务

**从中学到：** 理解现有驱动的工作方式，设计上层封装时避开它的弱点。`Net_JoinAP`（[Task/task_http.c:37-75](Task/task_http.c#L37-L75)）就绕过了 `ESP8266_JoinAP` 的忙等，改用 `vTaskDelay` 轮询。

### 1.5 跨任务共享数据的设计

**位置：[Task/task_http.h:23-28](Task/task_http.h#L23-L28)（定义），[Task/task_boot.h](Task/task_boot.h)（声明）**

两个共享结构体：

```c
// WIFI_Boot — HTTP_Task 写，LVGL 读
typedef struct {
    char  wifi_name[32];
    volatile int8_t wifi_sign;   // volatile 防优化
} WIFI_Boot;

// WeatherData — http_client 写，LVGL 读
typedef struct {
    int8_t  temp_C;
    int8_t  humidity;
    char    weather_desc[32];
    uint8_t valid;
} WeatherData;
```

**值得学习的写法：**
1. `volatile` 关键字：跨任务变量不加 volatile，编译器可能以错误的值做优化
2. `valid` 标志位：生产者先填数据再置 valid，消费者先检查 valid 再读数据 — 典型的生产者-消费者模式
3. 数据定义在一个头文件里，`.c` 中定义实例如 `g_weather`，其他地方 `extern` 引用

### 1.6 LVGL 定时刷新架构

**位置：[UI/UI_Main.c:137-173](UI/UI_Main.c#L137-L173)**

用 `lv_timer_create(LVGL_RefreshMainScreen, 30000, NULL)` 每 30 秒刷新一次屏幕。

刷新函数做的事：读 RTC → 读 DHT11 → 读 `g_wifi_boot` → 读 `g_weather` → 更新所有标签。

**值得学习的写法：** 显示层只消费数据，从不产生数据。LVGL_Task 不需要知道数据是谁更新的。

### 1.7 HTTP body 定位算法

**位置：[Service/http_client.c:161-177](Service/http_client.c#L161-L177)**

```
buffer = SEND OK\r\n\r\n+IPD,...\r\n\r\nbodyCLOSED
                           ↑第一个          ↑最后一个（正确）
```

用循环定位最后一个 `\r\n\r\n`，fallback `\n\n`。嵌入式 TCP 解析中常见的问题，算法虽小但很典型。

### 1.8 克服 DHT11 精度误差的显示写法

**位置：[UI/UI_Main.c:153-156](UI/UI_Main.c#L153-L156)**

DHT11 返回整数 + 小数，用 `%d.%d` 格式化显示，精度可控。

---

## 二、需要继续深入了解的知识点

### 2.1 FreeRTOS 内核

| 知识点 | 当前掌握 | 下次目标 |
|--------|----------|----------|
| 任务优先级与调度 | ✅ 能用 | 优先级反转、互斥量 |
| xTaskNotify | ✅ 会用了 | Notify 的四种模式区别 |
| vTaskDelay vs Delay_ms | ✅ 分清 | 任务状态切换细节 |
| 任务栈大小 | ✅ 会设 | 用 `uxTaskGetStackHighWaterMark` 监控实际使用 |
| 临界区 / 挂起调度器 | ❌ 踩过坑 | 什么时候需要用，什么时候不能用 |

**推荐深入学习：** FreeRTOS 官方文档中关于 **优先级反转** 和 **互斥量（Mutex）** 的部分。当前项目没有多任务竞争一个资源的情况（HTTP_Task 独占 ESP8266），但后续如果多个任务都要访问 SD 卡或 Flash，就需要 Mutex 来保护。

### 2.2 ESP8266 AT 固件

- AT 指令的 TCP/IP 协议栈工作原理
- CIPDNS / CIPDOMAIN 命令的固件版本差异
- 透传模式与非透传模式区别
- 多连接（CIPMUX=1 vs CIPMUX=0）的使用场景

### 2.3 HTTP 协议

- HTTP 响应格式（状态行 + 头 + body）
- Chunked Transfer Encoding（wttr.in 返回了 chunked 编码，当前没处理）
- Content-Length 与 Connection: close 两种结束判断方式

### 2.4 LVGL 深入

- 当前：静态标签 + 定时刷新
- 可以学：动画、样式系统、页面切换、字体管理

### 2.5 文件系统（即将学习的重点）

| 方向 | 说明 |
|------|------|
| **LittleFS** | 轻量级掉电安全，适合 NOR Flash（W25Q64） |
| **FATFS** | 标准 Windows 兼容，适合 SD 卡 |
| **区别** | 小文件 / 日志场景用 LittleFS，跨平台交换用 FATFS |

### 2.6 Bootloader + OTA

- 固件分区设计（Bootloader + APP1 + APP2）
- SD 卡读取固件 → 写入 Flash → 跳转
- 固件校验（CRC32 / MD5）
- 回滚机制

---

## 三、项目架构总览

### 当前架构图

```
┌──────────────────────────────────────────────────────────┐
│                   main() 启动顺序                          │
│  ① HAL 初始化 → ② FreeRTOS 启动 → ③ 任务开始运行          │
└──────────────────────────────────────────────────────────┘

┌─────────────┐    xTaskNotify    ┌─────────────────┐
│  Boot_Task  │ ──────────────►   │   HTTP_Task      │
│  (prio=2)   │                   │  (prio=3)        │
│             │                   │                  │
│ W25Q64 自检  │                   │ ① WiFi 连接      │
│ ESP AT 自检  │                   │ ② HTTP 对时→RTC  │
│ RTC 初始化   │                   │ ③ HTTP 天气      │
│ 通知后自删除  │                   │                  │
└─────────────┘                   └────────┬─────────┘
                                          │ 写入
                                    ┌──────▼───────┐
                                    │  g_wifi_boot │
                                    │  g_weather   │
                                    └──────┬───────┘
                                          │ 读取
┌──────────────────────────────────────────▼──────────┐
│                  LVGL_Task                           │
│                  (prio=1)                            │
│                                                      │
│  lv_timer 每 30 秒:                                   │
│    读 RTC → 读 DHT11 → 读 g_wifi_boot → 读 g_weather │
│    → lv_label_set_text 更新所有标签                    │
│                                                      │
│  lv_tick 每 1ms: LVGL 心跳                           │
└──────────────────────────────────────────────────────┘
```

### 数据流

```
DHT11 ─── SPI/GPIO ──→ LVGL_Task (直接读传感器)
RTC   ─── I2C   ──→ LVGL_Task (读时间)
ESP8266 ── USART3 ──→ HTTP_Task → 解析 → 写 g_wifi_boot + g_weather
                                              ↓
                                        LVGL_Task 读 → 显示
```

---

## 四、后续优化方向

### 4.1 功能性优化

| 优化项 | 说明 | 难度 |
|--------|------|------|
| **自动周期性刷新天气** | 当前只在启动时获取一次，HTTP_Task 可以加定时器每 30 分钟刷新 | ★☆☆ |
| **天气图标** | 根据 `weather_desc` 显示对应图标（晴/阴/雨） | ★★☆ |
| **掉电保存天气** | 把最后一次的天气数据存到 W25Q64，上电后先显示缓存 | ★★☆ |
| **WiFi 配置持久化** | SSID 和密码改成可修改，存到 Flash | ★★☆ |
| **DHT11 异常处理** | 读失败时显示 "--.-°C" 而不是上一次的值 | ★☆☆ |

### 4.2 架构优化

| 优化项 | 说明 | 难度 |
|--------|------|------|
| **ESP8266 驱动改造** | 去掉 `Delay_ms` 忙等，改为中断 + 信号量驱动 | ★★★ |
| **HTTP 响应缓冲区动态化** | 当前 1024 固定字节，可以等接收完再分配 | ★★☆ |
| **HTTP_Task 空闲时挂起** | 网络流完成后挂起，需要时再恢复 | ★★☆ |
| **LVGL 帧率优化** | 当前每 30 秒全刷新，可以改为只有数据变化才刷新 | ★★☆ |
| **错误恢复** | 网络超时后自动重试，而不是等待下次重启 | ★★☆ |

### 4.3 稳定性优化

| 优化项 | 说明 | 难度 |
|--------|------|------|
| **看门狗** | 开启 IWDG，防止任务卡死 | ★☆☆ |
| **ESP8266 断线重连** | WiFi 掉线后自动重连 | ★★☆ |
| **日志分级** | printf 加级别 (ERR/WARN/INFO/DBG)，串口不刷屏 | ★☆☆ |

---

## 五、下一步学习建议

根据你提到的 **文件系统 → Bootloader → OTA** 这个路线，我建议分三步走：

### 第一阶段：文件系统

**目标：** 用文件系统管理 W25Q64 和 SD 卡的数据

```
建议路线:
  LittleFS (W25Q64 NOR Flash)
    └→ 存储字库、配置参数、天气缓存
  FATFS (SD 卡)
    └→ 存储日志、固件文件、数据导出
```

**学习要点：**
1. SPI 驱动 W25Q64（已实现） → 在其上挂载 LittleFS
2. SPI/SDIO 驱动 SD 卡 → 挂载 FATFS
3. 文件读写 API：f_open / f_read / f_write

**推荐项目练手：**
- 把当前 W25Q64 的字库烧录逻辑改成文件读写
- 把天气数据每 30 分钟写入 SD 卡日志

### 第二阶段：Bootloader

**目标：** 编写 IAP Bootloader

```
Flash 分区:
  ┌────────────────┐
  │ Bootloader     │  0x08000000 (32KB)
  ├────────────────┤
  │ APP (当前程序)  │  0x08008000 (剩余)
  ├────────────────┤
  │ 字库 / 配置     │  后半部分
  └────────────────┘
```

**学习要点：**
1. STM32 启动流程与向量表重映射
2. 从 SD 卡读取固件 → 写入 Flash
3. 跳转到 APP 的汇编实现

### 第三阶段：OTA 升级

**目标：** 通过网络或 SD 卡远程升级固件

**学习要点：**
1. SD 卡 + FATFS 存放固件文件
2. CRC32/MD5 校验固件完整性
3. 双备份区 + 回滚机制

---

## 六、推荐学习资源

| 内容 | 资源 |
|------|------|
| FreeRTOS | FreeRTOS 官方文档 + 源码 `FreeRTOS/Source/include/task.h` |
| LVGL | [lvgl.io 官方文档](https://docs.lvgl.io) + lv_examples |
| LittleFS | GitHub: `littlefs-project/littlefs` |
| FATFS | [elm-chan.org/fatfs](http://elm-chan.org/fatfs) |
| IAP/Bootloader | STM32 AN4657 (应用笔记) |
| ESP8266 AT | Espressif AT 指令集文档 |

---

> **总结：** 这个项目把 STM32 嵌入式开发中最核心的几个领域（RTOS → GUI → 网络 → 传感器）第一次串了起来。第一次整合总是最难的，后面做类似的项目就有架构可以复用了。
>
> 文件系统和 Bootloader 是嵌入式进阶的必经之路，掌握了之后，你的 STM32 项目就不再是"点灯板"，而是可以独立运行、自我升级的完整产品。
