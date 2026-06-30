/**
 * ============================================================================
 * http_utils.c —— HTTP/JSON 工具函数
 * ============================================================================
 */
#include "http_utils.h"


const char *Json_FindValue(const char *haystack, const char *key)
{
    const char *p = strstr(haystack, key);
    if (!p) return NULL;
    p += strlen(key);
    p = strstr(p, ":");
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '"') p++;
    return p;
}


void Str_CopyDelim(const char *src, char *dst, uint8_t maxlen)
{
    uint8_t i = 0;
    while (src[i] && src[i] != '"' && src[i] != '\\' &&
           src[i] != '\r' && src[i] != '\n' && i < maxlen - 1)
        { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}
