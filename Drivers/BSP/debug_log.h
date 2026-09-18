#ifndef __DEBUG_LOG_H__
#define __DEBUG_LOG_H__

#include <stdio.h>
#include <string.h>
/* ==========================================
 * 开关控制
 * 建议：在编译选项中定义 DBG_ENABLE 来开启
 * 或者取消下面一行的注释
 * ========================================== */
/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 裸机控制台完成前关闭日志后端，移除 RT-Thread rt_kprintf 依赖
 * ========================================================================== */
/* #define DBG_ENABLE */
#ifndef DBG_ENABLE
  #undef DBG_CMD_ENABLE
#endif

// Command line debug interface (msh commands) is enabled only if DBG_ENABLE is defined
#define DBG_CMD_ENABLE

#ifdef DBG_ENABLE

    // 核心日志宏
    // 使用 ##__VA_ARGS__ 兼容不带参数的情况 (GCC/Keil/ICCARM均支持)
    //Debug log
    #define LOG_D(fmt, ...)   printf("[DBG] " fmt "\n", ##__VA_ARGS__)
    //Info log
    #define LOG_I(fmt, ...)   printf("[INFO] " fmt "\n", ##__VA_ARGS__)
    //Warning log
    #define LOG_W(fmt, ...)   printf("[WARN] " fmt "\n", ##__VA_ARGS__)
    //Error log
    #define LOG_E(fmt, ...)   printf("[ERR] " fmt "\n", ##__VA_ARGS__)
    //Parameter log
    #define LOG_P(fmt, ...)   printf("[PARAM] " fmt "\n", ##__VA_ARGS__)
#else

    // 关闭时，宏定义为空，编译器会优化掉，不占空间
    /* ==========================================================================
     *  Change: 修改
     *  Editor: 谢峰
     *  Time: 2026-09-18
     *  Range: 关闭日志时仍进行格式参数语义检查，避免产生未使用变量警告
     * ========================================================================== */
    #define LOG_D(fmt, ...)   do { if (0) { printf((fmt), ##__VA_ARGS__); } } while (0)
    #define LOG_I(fmt, ...)   do { if (0) { printf((fmt), ##__VA_ARGS__); } } while (0)
    #define LOG_W(fmt, ...)   do { if (0) { printf((fmt), ##__VA_ARGS__); } } while (0)
    #define LOG_E(fmt, ...)   do { if (0) { printf((fmt), ##__VA_ARGS__); } } while (0)
    #define LOG_P(fmt, ...)   do { if (0) { printf((fmt), ##__VA_ARGS__); } } while (0)
#endif

#endif // __DEBUG_LOG_H__
