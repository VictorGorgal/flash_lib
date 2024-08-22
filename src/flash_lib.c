#include "flash_lib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMORY_SIGNATURE 0x27062021
#define SIGNATURE_SIZE_BYTES 4
#define SIGNATURE_POSITION 0
#define LOGICAL_ID_POSITION 1
#define WRITE_COUNT_POSITION 2
#define PHYSICAL_ID_POSITION 3

typedef struct SectorHeader {
    uint32_t signature;
    uint16_t logicalID;
    uint16_t writeCount;
    uint8_t id;
} SectorHeader;

uint32_t _lower_bound;
uint32_t _upper_bound;
uint16_t _logical_sectors_count;
uint8_t _group_by;

uint16_t _get_random_physical_sector();
uint8_t *get_sector_read_pointer(uint32_t physical_sector_address);
void init_sectors();
void _write_sector_by_physical_addr(uint32_t physical_sector_address, const uint8_t *data);
void delete_sectors(uint32_t begin, uint32_t end);
void delete_sector(uint32_t physical_sector);
uint32_t get_header_attribute_from_sector(uint32_t physical_sector, uint8_t attribute_id);
bool get_first_sector_from_logical_id(uint16_t logical_id, uint32_t *physical_addr);
bool get_physical_sector_from_logical_id(uint16_t logical_id, uint8_t physical_sector_id, uint32_t *physical_addr);
uint32_t get_memory_addr_from_physical_sector(uint32_t physical_sector);
void prepare_buffer_to_write(uint8_t *buffer, const void *data, uint8_t data_size);
void read_and_update_header(uint32_t physical_sector_id, SectorHeader *sectorHeader);

/**
 * @brief Initializes the flash memory library.
 *
 * @param lower_bound The starting sector ID for the library.
 * @param logical_sectors_count The number of logical sectors to be managed.
 * @param group_by Number of physical sectors to group into one logical sector.
 */
void init_flash_lib(uint32_t lower_bound, uint16_t logical_sectors_count, uint8_t group_by) {
    _logical_sectors_count = logical_sectors_count;
    _lower_bound = lower_bound;
    _upper_bound = lower_bound + logical_sectors_count * group_by;
    _group_by = group_by;

    srand(time_us_32());

    init_sectors();
}

/**
 * @brief Initializes flash memory sectors during startup.
 *
 * This function performs the following operations:
 *
 * 1. **Validation Sweep**: Scans through all logical sector headers to validate their
 *    integrity by checking the sector signature and ID range. It also counts how many
 *    sectors require initialization.
 *
 * 2. **Initialization**: For sectors that need initialization:
 *    - Finds uninitialized logical IDs by checking the range from 0 to the maximum.
 *    - Configure headers for these uninitialized IDs.
 *
 * Note: The process of identifying uninitialized IDs has O(n^2) complexity, where n is
 * the number of logical sectors.
 */
void init_sectors() {
    uint16_t unitialized_sectors_count = 0;
    for (uint32_t physical_sector = _lower_bound; physical_sector < _upper_bound; physical_sector += _group_by) {
        if (get_header_attribute_from_sector(physical_sector, 0) != MEMORY_SIGNATURE) {
            unitialized_sectors_count++;
            continue;
        }

        // Invalidates the sector signature, making it available to be reinitialized
        if (get_header_attribute_from_sector(physical_sector, 1) >= _logical_sectors_count) {
            delete_sector(physical_sector);
            unitialized_sectors_count++;
        }
    }

    if (unitialized_sectors_count == 0) {
        return;
    }

    // Checks every logical ID to know which ones needs initialization
    for (uint32_t logical_id = 0; logical_id < _logical_sectors_count; logical_id++) {
        bool found_sector = get_first_sector_from_logical_id(logical_id, NULL);

        if (found_sector) {
            continue;
        }

        // Allocates new physical sector for the logical id
        SectorHeader sectorHeader = {
            .signature = MEMORY_SIGNATURE,
            .logicalID = logical_id,
            .writeCount = 1,
        };
        uint8_t headerBuffer[FLASH_PAGE_SIZE];
        prepare_buffer_to_write(headerBuffer, &sectorHeader, sizeof(SectorHeader));
        _write_sector_by_physical_addr(_get_random_physical_sector(), headerBuffer);

        // Finished initializing all sectors
        unitialized_sectors_count--;
        if (unitialized_sectors_count == 0) {
            break;
        }
    }
}

uint8_t *read_sector(uint16_t logical_sector, uint32_t offset_bytes) {
    uint32_t physical_sector_address;
    uint32_t physical_sector_id = offset_bytes / FLASH_SECTOR_SIZE;
    uint32_t physical_sector_offset = offset_bytes % FLASH_SECTOR_SIZE;
    get_physical_sector_from_logical_id(logical_sector, physical_sector_id, &physical_sector_address);
    return (uint8_t *)(get_memory_addr_from_physical_sector(physical_sector_address + physical_sector_offset) + XIP_BASE);
}

void erase_logical_sector(uint16_t logical_sector) {
    assert(logical_sector < _logical_sectors_count);

    uint32_t irq_status = save_and_disable_interrupts();

    SectorHeader *sectorHeaders = (SectorHeader *)malloc(_group_by * sizeof(SectorHeader));

    for (uint8_t i = 0; i < _group_by; ++i) {
        uint32_t physical_sector_address;
        get_physical_sector_from_logical_id(logical_sector, i, &physical_sector_address);
        read_and_update_header(physical_sector_address, &sectorHeaders[i]);
    }

    uint32_t physical_sector_address;
    get_first_sector_from_logical_id(logical_sector, &physical_sector_address);
    uint32_t memory_addr = get_memory_addr_from_physical_sector(physical_sector_address);
    flash_range_erase(memory_addr, FLASH_SECTOR_SIZE * _group_by);

    for (uint8_t i = 0; i < _group_by; ++i) {
        uint8_t headerBuffer[FLASH_PAGE_SIZE];
        prepare_buffer_to_write(headerBuffer, &sectorHeaders[i], sizeof(SectorHeader));

        get_physical_sector_from_logical_id(logical_sector, i, &physical_sector_address);
        memory_addr = get_memory_addr_from_physical_sector(physical_sector_address);
        flash_range_program(memory_addr, headerBuffer, FLASH_PAGE_SIZE);
    }

    free(sectorHeaders);
    restore_interrupts(irq_status);
}

void erase_physical_sector(uint16_t logical_sector, uint8_t physical_sector_id) {
    assert(logical_sector < _logical_sectors_count);
    assert(physical_sector_id < _group_by);

    uint32_t irq_status = save_and_disable_interrupts();

    uint32_t physical_sector_address;
    get_physical_sector_from_logical_id(logical_sector, physical_sector_id, &physical_sector_address);
    uint32_t memory_addr = get_memory_addr_from_physical_sector(physical_sector_address);
    flash_range_erase(memory_addr, FLASH_SECTOR_SIZE);

    SectorHeader sectorHeader;
    uint8_t headerBuffer[FLASH_PAGE_SIZE];
    read_and_update_header(physical_sector_address, &sectorHeader);
    prepare_buffer_to_write(headerBuffer, &sectorHeader, sizeof(SectorHeader));
    flash_range_program(memory_addr, headerBuffer, FLASH_PAGE_SIZE);

    restore_interrupts(irq_status);
}

// void write_sector(uint16_t sector, uint32_t logical_sector_offset, const uint8_t *data, uint32_t count) {
//     uint32_t physical_sector_address;
//     get_physical_sector_from_logical_id(sector, &physical_sector_address);

//     SectorHeader sectorHeader;
//     uint8_t headerBuffer[FLASH_PAGE_SIZE];
//     read_and_update_header(physical_sector_address, &sectorHeader);
//     prepare_buffer_to_write(headerBuffer, &sectorHeader, sizeof(SectorHeader));
//     memcpy(headerBuffer + sizeof(SectorHeader), data, count);

//     uint32_t memory_addr = get_memory_addr_from_physical_sector(physical_sector_address);

//     uint32_t irq_status = save_and_disable_interrupts();

//     flash_range_erase(memory_addr, FLASH_SECTOR_SIZE);
//     flash_range_program(memory_addr, headerBuffer, FLASH_PAGE_SIZE);

//     restore_interrupts(irq_status);
// }

uint32_t get_header_attribute_from_sector(uint32_t physical_sector, uint8_t attribute_id) {
    uint8_t *read_pointer = get_sector_read_pointer(physical_sector);
    uint32_t attribute = 0;
    if (attribute_id == 0) {
        memcpy(&attribute, read_pointer, SIGNATURE_SIZE_BYTES);
    } else if (attribute_id == 1) {
        memcpy(&attribute, read_pointer + SIGNATURE_SIZE_BYTES, sizeof(uint16_t));
    } else if (attribute_id == 2) {
        memcpy(&attribute, read_pointer + SIGNATURE_SIZE_BYTES + sizeof(uint16_t), sizeof(uint16_t));
    } else {
        memcpy(&attribute, read_pointer + SIGNATURE_SIZE_BYTES + 2 * sizeof(uint16_t), sizeof(uint8_t));
    }
    return attribute;
}

bool check_sector_signature(uint32_t physical_sector) {
    return get_header_attribute_from_sector(physical_sector, SIGNATURE_POSITION) == MEMORY_SIGNATURE;
}

bool get_first_sector_from_logical_id(uint16_t logical_id, uint32_t *physical_addr) {
    for (uint32_t physical_sector = _lower_bound; physical_sector < _upper_bound; physical_sector += _group_by) {
        if (!check_sector_signature(physical_sector)) {
            continue;
        }

        if (get_header_attribute_from_sector(physical_sector, LOGICAL_ID_POSITION) == logical_id) {
            if (physical_addr != NULL) {
                *physical_addr = physical_sector;
            }
            return true;
        }
    }

    return false;
}

bool get_physical_sector_from_logical_id(uint16_t logical_id, uint8_t physical_sector_id, uint32_t *physical_addr) {
    uint32_t physical_sector;
    bool found = get_first_sector_from_logical_id(logical_id, &physical_sector);

    for (uint8_t i = 0; i < _group_by; ++i) {
        if (get_header_attribute_from_sector(physical_sector, PHYSICAL_ID_POSITION) == physical_sector_id) {
            break;
        }

        physical_sector += FLASH_SECTOR_SIZE;
    }

    if (physical_addr != NULL) {
        *physical_addr = physical_sector;
    }
    return found;
}

void prepare_buffer_to_write(uint8_t *buffer, const void *data, uint8_t data_size) {
    memset(buffer, 0xFF, FLASH_PAGE_SIZE);
    memcpy(buffer, data, data_size);
}

/**
 * @brief Retrieves a random uninitialized sector address.
 *
 * This function first generates a random sector address within the valid range. If the
 * generated sector is already initialized, the function searches upwards from that address
 * until it finds an uninitialized sector. If no uninitialized sector is found going upwards,
 * it then searches downwards.
 *
 * @return An uninitialized sector address within the range defined by _lower_bound and _upper_bound.
 */
uint16_t _get_random_physical_sector() {
    uint16_t random_physical_sector = (rand() % (_upper_bound - _lower_bound)) + _lower_bound;

    // Check upwards
    for (uint16_t physical_sector = random_physical_sector; physical_sector < _upper_bound; ++physical_sector) {
        if (!check_sector_signature(physical_sector)) {
            return physical_sector;
        }
    }

    // Check downwards
    for (uint16_t physical_sector = random_physical_sector - 1; physical_sector >= _lower_bound; --physical_sector) {
        if (!check_sector_signature(physical_sector)) {
            return physical_sector;
        }
    }

    // No available sector found
}

uint32_t get_memory_addr_from_physical_sector(uint32_t physical_sector) {
    return physical_sector * FLASH_SECTOR_SIZE;
}

uint8_t *get_sector_read_pointer(uint32_t physical_sector) {
    return (uint8_t *)(get_memory_addr_from_physical_sector(physical_sector) + XIP_BASE);
}

void _write_sector_by_physical_addr(uint32_t physical_sector_address, const uint8_t *data) {
    physical_sector_address = get_memory_addr_from_physical_sector(physical_sector_address);

    uint32_t irq_status = save_and_disable_interrupts();

    flash_range_erase(physical_sector_address, FLASH_SECTOR_SIZE);
    flash_range_program(physical_sector_address, data, FLASH_PAGE_SIZE);

    restore_interrupts(irq_status);
}

void read_and_update_header(uint32_t physical_sector_id, SectorHeader *sectorHeader) {
    uint8_t *read_pointer = get_sector_read_pointer(physical_sector_id);
    memcpy(sectorHeader, read_pointer, sizeof(SectorHeader));
    sectorHeader->writeCount++;
}

// **************** DEBUG FUNCTIONS ****************

void print_buffer(uint8_t *buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        printf("%02x ", buffer[i]);
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    if (size % 16 != 0) {
        printf("\n");
    }
}

void delete_sectors(uint32_t begin, uint32_t end) {
    uint8_t cleanHeaderBuffer[FLASH_PAGE_SIZE];
    memset(cleanHeaderBuffer, 0x00, sizeof(SectorHeader));
    memset(cleanHeaderBuffer + sizeof(SectorHeader), 0xFF, FLASH_PAGE_SIZE - sizeof(SectorHeader));

    uint8_t *read = get_sector_read_pointer(_lower_bound);

    uint32_t irq_status = save_and_disable_interrupts();

    for (uint32_t physical_sector = begin; physical_sector < end; ++physical_sector) {
        flash_range_program(get_memory_addr_from_physical_sector(physical_sector), cleanHeaderBuffer, FLASH_PAGE_SIZE);
    }

    restore_interrupts(irq_status);
}

void delete_all_sectors() {
    delete_sectors(_lower_bound, _upper_bound);
}

void delete_sector(uint32_t physical_sector) {
    delete_sectors(physical_sector, physical_sector + 1);
}

void print_sector_header() {
    for (uint32_t physical_sector = _lower_bound; physical_sector < _upper_bound; physical_sector += _group_by) {
        uint8_t *read_pointer = get_sector_read_pointer(physical_sector);
        print_buffer(read_pointer, 12);
    }
}

#include "pico/time.h"
void flash_lib_example() {
    absolute_time_t start_time;
    absolute_time_t end_time;
    float elapsed_time;

    uint16_t sector_to_write = 0;
    uint32_t my_physical_sector;
    uint8_t writeBuffer[FLASH_PAGE_SIZE];
    uint8_t data_to_write[4] = {0x0A, 0xFA, 0xCA, 0xDA};
    prepare_buffer_to_write(writeBuffer, data_to_write, 4);

    uint16_t lower_bound = 100;
    uint16_t sectors_count = 10;

    start_time = get_absolute_time();
    // *** Code ***
    init_flash_lib(lower_bound, sectors_count, GROUP_BY_1);
    // *** Code ***
    end_time = get_absolute_time();
    elapsed_time = 1.0 * absolute_time_diff_us(start_time, end_time) / 1000;
    printf("Time to init library: %.3fms\n", elapsed_time);

    start_time = get_absolute_time();
    // *** Code ***
    get_first_sector_from_logical_id(sector_to_write, &my_physical_sector);
    // *** Code ***
    end_time = get_absolute_time();
    elapsed_time = 1.0 * absolute_time_diff_us(start_time, end_time);
    printf("Time to find sector: %.2fus\n", elapsed_time);
    printf("Logical id: %d At physical addr at: %d\n", sector_to_write, my_physical_sector);

    // start_time = get_absolute_time();
    // // *** Code ***
    // // write_sector(sector_to_write, 0, data_to_write, sizeof(data_to_write));
    // uint32_t memory_addr = get_memory_addr_from_physical_sector(109);

    // uint32_t irq_status = save_and_disable_interrupts();

    // flash_range_erase(memory_addr, FLASH_SECTOR_SIZE);

    // restore_interrupts(irq_status);
    // // *** Code ***
    // end_time = get_absolute_time();
    // elapsed_time = 1.0*absolute_time_diff_us(start_time, end_time)/1000;
    // printf("Time to erase to sector: %.2fms\n", elapsed_time);

    start_time = get_absolute_time();
    // *** Code ***
    // write_sector(sector_to_write, 0, data_to_write, sizeof(data_to_write));
    uint32_t memory_addr = get_memory_addr_from_physical_sector(109);

    uint32_t irq_status = save_and_disable_interrupts();

    // flash_range_program(memory_addr, writeBuffer, FLASH_PAGE_SIZE);

    restore_interrupts(irq_status);
    // *** Code ***
    end_time = get_absolute_time();
    elapsed_time = 1.0 * absolute_time_diff_us(start_time, end_time);
    printf("Time to write to sector: %.2fus\n", elapsed_time);

    start_time = get_absolute_time();
    // *** Code ***
    get_first_sector_from_logical_id(1 << 15, &my_physical_sector);
    // *** Code ***
    end_time = get_absolute_time();
    elapsed_time = 1.0 * absolute_time_diff_us(start_time, end_time);
    printf("Time to read all headers: %.2fus\n", elapsed_time);
}
