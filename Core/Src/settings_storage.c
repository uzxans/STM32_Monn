#include "settings_storage.h"
#include <string.h>

extern RTC_HandleTypeDef hrtc;

/* Backup-регистры STM32F2: RTC_BKP_DR0..RTC_BKP_DR19 (20 шт.) */
/* ВНИМАНИЕ: RTC_BKP_DR0 используется инициализацией RTC в MX_RTC_Init для флага, 
 * поэтому для сетевых параметров начинаем с DR1. Для SNMP сокращаем до 4 регистров на строку. */
#define BKP_IP_REG0          RTC_BKP_DR1
#define BKP_MASK_REG1        RTC_BKP_DR2
#define BKP_GW_REG2          RTC_BKP_DR3
#define BKP_DHCP_REG3        RTC_BKP_DR4
#define BKP_SNMP_BASE        RTC_BKP_DR5
#define BKP_SNMP_REG_COUNT   4   // 4 регистра на строку (~16 байт)
#define BKP_MAGIC_REG        RTC_BKP_DR19
#define BKP_MAGIC_VALUE      0xBEEFCAFE

static void bk_write_u32(uint32_t reg, uint32_t value) {
    HAL_RTCEx_BKUPWrite(&hrtc, reg, value);
}

static uint32_t bk_read_u32(uint32_t reg) {
    return HAL_RTCEx_BKUPRead(&hrtc, reg);
}

static void bk_write_string(uint32_t start_reg, const char *s, int count) {
    for (int i = 0; i < count; i++) {
        uint32_t word = 0;
        int offset = i * 4;
        for (int b = 0; b < 4; b++) {
            int idx = offset + b;
            uint8_t ch = (s && idx < (int)strlen(s)) ? (uint8_t)s[idx] : 0;
            word |= ((uint32_t)ch) << (8 * b);
        }
        HAL_RTCEx_BKUPWrite(&hrtc, start_reg + i, word);
    }
}

static void bk_read_string(uint32_t start_reg, char *out, int count) {
    int pos = 0;
    for (int i = 0; i < count; i++) {
        uint32_t word = HAL_RTCEx_BKUPRead(&hrtc, start_reg + i);
        for (int b = 0; b < 4; b++) {
            uint8_t ch = (word >> (8 * b)) & 0xFF;
            if (ch == 0) { out[pos] = 0; return; }
            out[pos++] = ch;
        }
    }
    out[pos] = 0;
}

/* Инициализация backup-доступа */
void Settings_Init(void) {
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
}

/* Сохранение параметров */
void Settings_Save_To_Backup(ip4_addr_t ip, ip4_addr_t mask, ip4_addr_t gw, uint8_t dhcp,
                             const char *snmp_read, const char *snmp_write, const char *snmp_trap)
{
    bk_write_u32(BKP_IP_REG0, ip.addr);
    bk_write_u32(BKP_MASK_REG1, mask.addr);
    bk_write_u32(BKP_GW_REG2, gw.addr);
    bk_write_u32(BKP_DHCP_REG3, dhcp ? 1 : 0);

    bk_write_string(BKP_SNMP_BASE + 0, snmp_read  ? snmp_read  : "", BKP_SNMP_REG_COUNT);
    bk_write_string(BKP_SNMP_BASE + BKP_SNMP_REG_COUNT, snmp_write ? snmp_write : "", BKP_SNMP_REG_COUNT);
    bk_write_string(BKP_SNMP_BASE + BKP_SNMP_REG_COUNT*2, snmp_trap  ? snmp_trap  : "", BKP_SNMP_REG_COUNT);

    bk_write_u32(BKP_MAGIC_REG, BKP_MAGIC_VALUE);
}

/* Загрузка параметров */
void Settings_Load_From_Backup(ip4_addr_t *ip, ip4_addr_t *mask, ip4_addr_t *gw, uint8_t *dhcp,
                              char *snmp_read, int snmp_read_size,
                              char *snmp_write, int snmp_write_size,
                              char *snmp_trap, int snmp_trap_size)
{
    if (bk_read_u32(BKP_MAGIC_REG) != BKP_MAGIC_VALUE) {
        if (ip) ip->addr = 0;
        if (mask) mask->addr = 0;
        if (gw) gw->addr = 0;
        if (dhcp) *dhcp = 0;
        if (snmp_read)  snmp_read[0]  = 0;
        if (snmp_write) snmp_write[0] = 0;
        if (snmp_trap)  snmp_trap[0]  = 0;
        return;
    }

    if (ip)    ip->addr   = bk_read_u32(BKP_IP_REG0);
    if (mask)  mask->addr = bk_read_u32(BKP_MASK_REG1);
    if (gw)    gw->addr   = bk_read_u32(BKP_GW_REG2);
    if (dhcp)  *dhcp      = (uint8_t)(bk_read_u32(BKP_DHCP_REG3) & 0xFF);

    char tmp[33];

    bk_read_string(BKP_SNMP_BASE + 0, tmp, BKP_SNMP_REG_COUNT);
    if (snmp_read) strncpy(snmp_read, tmp, snmp_read_size-1), snmp_read[snmp_read_size-1]=0;

    bk_read_string(BKP_SNMP_BASE + BKP_SNMP_REG_COUNT, tmp, BKP_SNMP_REG_COUNT);
    if (snmp_write) strncpy(snmp_write, tmp, snmp_write_size-1), snmp_write[snmp_write_size-1]=0;

    bk_read_string(BKP_SNMP_BASE + BKP_SNMP_REG_COUNT*2, tmp, BKP_SNMP_REG_COUNT);
    if (snmp_trap) strncpy(snmp_trap, tmp, snmp_trap_size-1), snmp_trap[snmp_trap_size-1]=0;
}


