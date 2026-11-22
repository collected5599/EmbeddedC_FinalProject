#include "qspi_flash.h"

#include <string.h>

extern IWDG_HandleTypeDef hiwdg;

enum {
    CMD_READ_ID = 0x9F,
    CMD_READ_STATUS = 0x05,
    CMD_WRITE_ENABLE = 0x06,
    CMD_READ_DATA = 0x03,
    CMD_PAGE_PROGRAM = 0x02,
    CMD_SECTOR_ERASE_4K = 0x20,
    STATUS_WIP = 0x01,
    FLASH_WAIT_READY_MS = 1200
};

static QSPI_HandleTypeDef *flash_qspi;
static bool flash_available;

static QSPI_CommandTypeDef make_command(uint8_t instruction)
{
    QSPI_CommandTypeDef command;
    memset(&command, 0, sizeof(command));
    command.Instruction = instruction;
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DataMode = QSPI_DATA_NONE;
    command.DummyCycles = 0u;
    command.DdrMode = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    command.NbData = 0u;
    return command;
}

static QspiFlashStatus_t send_simple(uint8_t instruction)
{
    QSPI_CommandTypeDef command = make_command(instruction);
    return (HAL_QSPI_Command(flash_qspi, &command, 100u) == HAL_OK) ?
           QSPI_FLASH_OK : QSPI_FLASH_ERROR;
}

static QspiFlashStatus_t wait_ready(uint32_t timeout_ms)
{
    const uint32_t started = HAL_GetTick();
    QSPI_CommandTypeDef command = make_command(CMD_READ_STATUS);
    uint8_t status_register;

    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = 1u;
    while ((uint32_t)(HAL_GetTick() - started) < timeout_ms) {
        if (HAL_QSPI_Command(flash_qspi, &command, 100u) != HAL_OK ||
            HAL_QSPI_Receive(flash_qspi, &status_register, 100u) != HAL_OK) {
            return QSPI_FLASH_ERROR;
        }
        if ((status_register & STATUS_WIP) == 0u) {
            return QSPI_FLASH_OK;
        }
        (void)HAL_IWDG_Refresh(&hiwdg);
        HAL_Delay(1u);
    }
    return QSPI_FLASH_TIMEOUT;
}

static QspiFlashStatus_t write_enable(void)
{
    QSPI_CommandTypeDef command = make_command(CMD_WRITE_ENABLE);
    QspiFlashStatus_t status = wait_ready(FLASH_WAIT_READY_MS);
    if (status != QSPI_FLASH_OK) return status;
    if (HAL_QSPI_Command(flash_qspi, &command, 100u) != HAL_OK) {
        return QSPI_FLASH_ERROR;
    }
    return QSPI_FLASH_OK;
}

QspiFlashStatus_t QspiFlash_Init(QSPI_HandleTypeDef *handle, uint32_t *jedec_id)
{
    QSPI_CommandTypeDef command;
    uint8_t id[3];

    flash_available = false;
    if ((handle == NULL) || (jedec_id == NULL)) return QSPI_FLASH_BAD_ARGUMENT;
    flash_qspi = handle;
    command = make_command(CMD_READ_ID);
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = sizeof(id);
    if (HAL_QSPI_Command(flash_qspi, &command, 100u) != HAL_OK ||
        HAL_QSPI_Receive(flash_qspi, id, 100u) != HAL_OK) {
        return QSPI_FLASH_ERROR;
    }
    *jedec_id = ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
    if (*jedec_id != 0xC22817u) return QSPI_FLASH_BAD_ID;
    if (wait_ready(FLASH_WAIT_READY_MS) != QSPI_FLASH_OK) return QSPI_FLASH_ERROR;
    flash_available = true;
    return QSPI_FLASH_OK;
}

QspiFlashStatus_t QspiFlash_Read(uint32_t address, uint8_t *data, uint32_t length)
{
    QSPI_CommandTypeDef command;
    if (!flash_available) return QSPI_FLASH_ERROR;
    if ((data == NULL) || (length == 0u) ||
        (address >= QSPI_FLASH_SIZE_BYTES) ||
        (length > QSPI_FLASH_SIZE_BYTES - address)) {
        return QSPI_FLASH_BAD_ARGUMENT;
    }
    command = make_command(CMD_READ_DATA);
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.AddressSize = QSPI_ADDRESS_24_BITS;
    command.Address = address;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = length;
    if (HAL_QSPI_Command(flash_qspi, &command, 100u) != HAL_OK ||
        HAL_QSPI_Receive(flash_qspi, data, 100u) != HAL_OK) {
        return QSPI_FLASH_ERROR;
    }
    return QSPI_FLASH_OK;
}

QspiFlashStatus_t QspiFlash_ProgramPage(uint32_t address,
                                        const uint8_t *data,
                                        uint16_t length)
{
    QSPI_CommandTypeDef command;
    QspiFlashStatus_t status;
    if (!flash_available) return QSPI_FLASH_ERROR;
    if ((data == NULL) || (length == 0u) || (length > QSPI_FLASH_PAGE_BYTES) ||
        (address >= QSPI_FLASH_SIZE_BYTES) ||
        (length > QSPI_FLASH_SIZE_BYTES - address) ||
        ((address / QSPI_FLASH_PAGE_BYTES) !=
         ((address + length - 1u) / QSPI_FLASH_PAGE_BYTES))) {
        return QSPI_FLASH_BAD_ARGUMENT;
    }
    status = write_enable();
    if (status != QSPI_FLASH_OK) return status;
    command = make_command(CMD_PAGE_PROGRAM);
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.AddressSize = QSPI_ADDRESS_24_BITS;
    command.Address = address;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = length;
    if (HAL_QSPI_Command(flash_qspi, &command, 100u) != HAL_OK ||
        HAL_QSPI_Transmit(flash_qspi, (uint8_t *)data, 100u) != HAL_OK) {
        return QSPI_FLASH_ERROR;
    }
    return wait_ready(FLASH_WAIT_READY_MS);
}

QspiFlashStatus_t QspiFlash_EraseSector(uint32_t address)
{
    QSPI_CommandTypeDef command;
    QspiFlashStatus_t status;
    if (!flash_available) return QSPI_FLASH_ERROR;
    if ((address >= QSPI_FLASH_SIZE_BYTES) ||
        ((address % QSPI_FLASH_SECTOR_BYTES) != 0u)) {
        return QSPI_FLASH_BAD_ARGUMENT;
    }
    status = write_enable();
    if (status != QSPI_FLASH_OK) return status;
    command = make_command(CMD_SECTOR_ERASE_4K);
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.AddressSize = QSPI_ADDRESS_24_BITS;
    command.Address = address;
    if (HAL_QSPI_Command(flash_qspi, &command, 100u) != HAL_OK) {
        return QSPI_FLASH_ERROR;
    }
    return wait_ready(FLASH_WAIT_READY_MS);
}

bool QspiFlash_IsAvailable(void)
{
    return flash_available;
}
