# Flash Memory Library with Wear Leveling

## Overview

This library provides an efficient way to manage the Pico's flash memory sectors with wear leveling, ensuring data persistence and even distribution of write/erase cycles across the memory.

**Note:** This library is a work in progress and is in early development. Features and functionality may change as development continues.

## Features

-   Wear leveling to extend the lifespan of flash memory.
-   Support for logical sectors, abstracting physical sector management.
-   On-demand validation of sectors to optimize initialization time.
-   Customizable sector grouping to balance performance and memory usage.

## Getting Started

### Installation

1. Clone the repository into project:

    ```sh
    git clone https://github.com/VictorGorgal/flash_lib.git
    cd flash_lib
    ```

2. Include the library in your project:
    - Include library in the CMameLists.txt file
    ```c
    add_subdirectory(flash_lib)
    target_link_libraries(YourProject
            flash_lib
    )
    ```
    - Include the header file in your source code:
    ```c
    #include "flash_lib.h"
    ```

### Initialization

To use the library, first initialize it with the `init_flash_lib` function:

```c
init_flash_lib(100, 10, 4);

// Reading and Writing Data
// Reading Data:
void read_from_sector(uint16_t logical_id, uint8_t *buffer, size_t size);

//Writing Data:
void write_to_sector(uint16_t logical_id, const uint8_t *buffer, size_t size);

//Erasing Data
//Erasing a Logical Sector:
void erase_logical_sector(uint16_t logical_id);

//Erasing a Physical Sector:
void erase_physical_sector(uint32_t physical_address);
```

A full example can be found in the source file on the flash_lib_example() function.
A more thurough explanation can be found in the header file

Notes
It is recommended to use large logical sector sizes to improve performance and decrease execution time for large amounts of data.
Ensure that the lower_bound does not intersect with your code area to avoid unpredictable behavior.

License
This project is licensed under the MIT License.

Contributing
Contributions are welcome! Please fork the repository and submit a pull request.
