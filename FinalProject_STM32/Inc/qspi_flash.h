#ifndef QSPI_FLASH_H
#define QSPI_FLASH_H

#include "stm32l4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#define QSPI_FLASH_SIZE_BYTES  0x00800000u
#define QSPI_FLASH_PAGE_BYTES  256u
#define QSPI_FLASH_SECTOR_BYTES 4096u
#define QSPI_FLASH_CALIBRATION_SLOT_A 0x00001000u /* Sector 1 */
#define QSPI_FLASH_CALIBRATION_SLOT_B 0x00002000u /* Sector 2 */
#define QSPI_FLASH_EVENT_BASE  0x00003000u
#define QSPI_FLASH_EVENT_END   0x00010000u

typedef enum {
    QSPI_FLASH_OK = 0,
    QSPI_FLASH_BUSY,
    QSPI_FLASH_ERROR,
    QSPI_FLASH_TIMEOUT,
    QSPI_FLASH_BAD_ARGUMENT,
    QSPI_FLASH_BAD_ID
} QspiFlashStatus_t;

QspiFlashStatus_t QspiFlash_Init(QSPI_HandleTypeDef *handle, uint32_t *jedec_id);
QspiFlashStatus_t QspiFlash_Read(uint32_t address, uint8_t *data, uint32_t length);
QspiFlashStatus_t QspiFlash_ProgramPage(uint32_t address,
                                        const uint8_t *data,
                                        uint16_t length);
QspiFlashStatus_t QspiFlash_EraseSector(uint32_t address);
bool QspiFlash_IsAvailable(void);

#endif /* QSPI_FLASH_H */
