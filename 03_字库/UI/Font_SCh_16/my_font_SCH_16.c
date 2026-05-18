/*
 *---------------------------------------------------------------
 *                        Lvgl Font Tool
 *
 * 注:使用unicode编码
 * 注:本字体文件由Lvgl Font Tool V0.5测试版 生成
 * 作者:阿里(qq:617622104)
 *
 * 【修改说明】
 *  原 __user_font_getdata() 从 RAM 数组读取，现改为从 W25Q64 Flash 读取。
 *  字库 .bin 文件先通过串口烧录到 Flash，LVGL 渲染文字时实时从 Flash 读取位图。
 *
 *  数据流向：
 *    W25Q64 Flash (字库已烧录)
 *    → __user_font_getdata(offset, size)
 *    → W25Q64_Read(flash_base + offset, __g_font_buf, size)
 *    → 返回 __g_font_buf 指针 → LVGL 渲染引擎使用
 *---------------------------------------------------------------
 */


#include "lvgl.h"
#include "W25Q64.h"   /* 需要 W25Q64_Read() 和 W25Q64_GetFileAddr() */


typedef struct{
    uint16_t min;
    uint16_t max;
    uint8_t  bpp;
    uint8_t  reserved[3];
}x_header_t;
typedef struct{
    uint32_t pos;
}x_table_t;
typedef struct{
    uint8_t adv_w;
    uint8_t box_w;
    uint8_t box_h;
    int8_t  ofs_x;
    int8_t  ofs_y;
    uint8_t r;
}glyph_dsc_t;


static x_header_t __g_xbf_hd = {
    .min = 0x000a,
    .max = 0x9fa0,
    .bpp = 4,
};


/*
 * __g_font_buf[256]
 *   LVGL 每次要一个字形的数据，就先从 Flash 读到这个缓冲区，
 *   然后返回缓冲区指针给 LVGL。
 *   256 字节足够装下 16x16@4bpp 的最大位图 (128字节) + 描述符 (6字节)。
 */
static uint8_t   __g_font_buf[256];
static uint32_t  __g_font_flash_addr = 0;   /* 字库在 Flash 中的起始地址 */
static uint8_t   __g_font_inited     = 0;   /* 是否已从目录查到地址     */


/**
 * @brief  初始化：从 W25Q64 目录中查找 id=0 的字库文件，
 *         记录其在 Flash 中的起始地址。
 *
 *         必须在 W25Q64_FileSysInit() 之后、LVGL 渲染之前调用。
 */
void my_font_SCH_16_init(void)
{
    uint32_t addr, size;
    if (W25Q64_GetFileAddr(0, &addr, &size) == 0) {
        __g_font_flash_addr = addr;
        __g_font_inited = 1;
        printf("[Font] SCH_16 字库就绪，FlashAddr=0x%06X, Size=%lu\n",
               (unsigned int)addr, (unsigned long)size);
    } else {
        printf("[Font] ⚠️ SCH_16 字库未在 Flash 中找到 (id=0)\n");
    }
}


/**
 * @brief  从 W25Q64 Flash 读取字库数据到缓冲区
 * @param  offset  相对于字库文件起始位置的偏移量
 * @param  size    要读取的字节数
 * @return 指向 __g_font_buf 的指针（数据已从 Flash 读入）
 *
 * 【工作原理】
 *   LVGL 调用此函数时，我们把 Flash 中 "字库起始地址 + offset"
 *   开始的 size 字节读到 __g_font_buf，然后返回缓冲区指针。
 *
 *   因为 LVGL 使用完返回的指针后不会再回头看，所以下次调用
 *   直接覆盖 __g_font_buf 是安全的。
 */
static uint8_t *__user_font_getdata(int offset, int size){
    if (!__g_font_inited || size > (int)sizeof(__g_font_buf)) {
        return NULL;
    }

    /* 从 W25Q64 Flash 读取指定偏移处的数据 */
    W25Q64_Read(__g_font_flash_addr + offset, __g_font_buf, (uint32_t)size);
    return __g_font_buf;
}


static const uint8_t * __user_font_get_bitmap(const lv_font_t * font, uint32_t unicode_letter) {
    if( unicode_letter>__g_xbf_hd.max || unicode_letter<__g_xbf_hd.min ) {
        return NULL;
    }
    uint32_t unicode_offset = sizeof(x_header_t)+(unicode_letter-__g_xbf_hd.min)*4;
    uint32_t *p_pos = (uint32_t *)__user_font_getdata(unicode_offset, 4);
    if( p_pos[0] != 0 ) {
        uint32_t pos = p_pos[0];
        glyph_dsc_t * gdsc = (glyph_dsc_t*)__user_font_getdata(pos, sizeof(glyph_dsc_t));
        return __user_font_getdata(pos+sizeof(glyph_dsc_t), gdsc->box_w*gdsc->box_h*__g_xbf_hd.bpp/8);
    }
    return NULL;
}


static bool __user_font_get_glyph_dsc(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next) {
    if( unicode_letter>__g_xbf_hd.max || unicode_letter<__g_xbf_hd.min ) {
        return NULL;
    }
    uint32_t unicode_offset = sizeof(x_header_t)+(unicode_letter-__g_xbf_hd.min)*4;
    uint32_t *p_pos = (uint32_t *)__user_font_getdata(unicode_offset, 4);
    if( p_pos[0] != 0 ) {
        glyph_dsc_t * gdsc = (glyph_dsc_t*)__user_font_getdata(p_pos[0], sizeof(glyph_dsc_t));
        dsc_out->adv_w = gdsc->adv_w;
        dsc_out->box_h = gdsc->box_h;
        dsc_out->box_w = gdsc->box_w;
        dsc_out->ofs_x = gdsc->ofs_x;
        dsc_out->ofs_y = gdsc->ofs_y;
        dsc_out->bpp   = __g_xbf_hd.bpp;
        return true;
    }
    return false;
}


//宋体,常规,16
//字模高度：0
//XBF字体,外部bin文件
const lv_font_t my_font_SCH_16 = {
    .get_glyph_bitmap = __user_font_get_bitmap,
    .get_glyph_dsc = __user_font_get_glyph_dsc,
    .line_height = 0,
    .base_line = 0,
};