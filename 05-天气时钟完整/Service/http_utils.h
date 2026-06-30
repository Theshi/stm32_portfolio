/**
 * ============================================================================
 * http_utils.h —— HTTP/JSON 工具函数
 *
 * 将 main.c 中的 JSON 查找和字符串截取函数提取到 Service 层
 * ============================================================================
 */
#ifndef __HTTP_UTILS_H
#define __HTTP_UTILS_H

#include <string.h>
#include <stdint.h>

/* ---- JSON 字段查找 ------------------------------------------------
 * 在 haystack 中定位 key 后的值起始指针
 * 例如 Json_FindValue(str, "\"temp_C\"") → 返回 "25" 的 '2' 位置
 * 返回 NULL 表示未找到
 * ------------------------------------------------------------------ */
const char *Json_FindValue(const char *haystack, const char *key);

/* ---- 字符串截取 ----------------------------------------------------
 * 从 src 拷贝到 dst，遇到 "  \\  \\r  \\n 或满 maxlen-1 时停止
 * 始终在 dst 末尾加 '\\0'
 * ------------------------------------------------------------------ */
void Str_CopyDelim(const char *src, char *dst, uint8_t maxlen);

#endif /* __HTTP_UTILS_H */
