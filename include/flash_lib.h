
/**
 * @brief Initializes the flash memory library with wear leveling.
 * 
 * *** Library Overview ***
 * This library manages flash memory sectors with wear leveling, ensuring data persistence
 * and distribution of write/erase cycles across the memory.
 * 
 * *** Initialization ***
 * - The first initialization may take several seconds to a few minutes depending on the 
 *   number of logical sectors. Subsequent initializations (after power down) will only 
 *   take a few milliseconds.
 * - The library can detect and correct changes in the lower bound or the number of sectors 
 *   between power-ups, skipping already initialized sectors.
 * 
 * *** Logical Sectors ***
 * - Flash memory is divided into physical sectors, each 4096 bytes in size.
 * - Logical sectors are how you interact with the library, providing a simplified interface for 
 *   managing flash memory.
 * - Logical sectors are an abstraction created by the library, consisting of multiple physical sectors 
 *   determined by the `group_by` attribute. For example, if `group_by` is 64, each logical sector 
 *   will be 4096 * 64 = 256 KB in size.
 * - The first 8 bytes of every logical sector are reserved for the header, so the usable size of 
 *   a logical sector is 4096 * `group_by` - 8 bytes.
 * - The library supports up to 65535 logical sectors, but using larger logical sector sizes is 
 *   recommended to reduce execution time.
 * - The library will use memory sectors starting from the `lower_bound` and extending upwards.
 *   The total number of sectors used is determined by `logical_sectors_count` multiplied by 
 *   `group_by`. For example, if `lower_bound` is 100, `logical_sectors_count` is 10, and 
 *   `group_by` is 4, the library will use sectors 100 to 139.
 * 
 * *** Usage ***
 * - Before writing or reading from a sector, an ID is required. This ID can be any number between
 *   0 and `total_sectors` - 1.
 * - The ID is fixed and never changes, even after shutdowns or physical sector changes due to 
 *   wear leveling. The library ensures data can be accessed with the same ID consistently.
 * - The user must keep track of the IDs being used, as the library does not manage or verify ID 
 *   uniqueness across different programs or functions.
 * 
 * *** Note ***
 * - It is recommended to use large logical sector sizes to improve performance and decrease 
 *   execution time for large amounts of data.
 * - When setting the `lower_bound`, ensure it does not intersect with the code section of the flash 
 *   memory, as this will cause the Pico to behave unpredictably. To determine a safe `lower_bound`, 
 *   check the .uf2 file with picotool to see where the code ends, or simply divide the size of the .uf2
 *   file size by 4096 (sector size) and add a safety zone.
 */

#ifndef FLASH_LIB_H
#define FLASH_LIB_H

#include "pico/stdlib.h"

#define GROUP_BY_1 1
#define GROUP_BY_8 8
#define GROUP_BY_16 16
#define GROUP_BY_64 64

void flash_lib_example();

#endif
