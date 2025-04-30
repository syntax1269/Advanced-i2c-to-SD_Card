# ATtiny1614 I2C SD Card Bridge (v14)
Based on the hardware design of http://www.technoblogy.com/show?3XEP
## Overview

This Arduino sketch runs on an ATtiny1614 microcontroller and acts as an I2C slave device. It provides an interface for an I2C master (like an ESP8266, Raspberry Pi, or another Arduino) to interact with an SD card connected to the ATtiny1614. This allows the master device to perform various file system operations on the SD card over the I2C bus.

## Features

*   Acts as an I2C slave (Default Address: `0x55`).
*   Supports standard SD card operations via the Arduino `SD.h` library.
*   Provides I2C commands for:
    *   Setting the target filename/path.
    *   Writing data to files (overwrite/create).
    *   Appending data to files.
    *   Reading data from files.
    *   Getting file size.
    *   Checking if a file or path exists.
    *   Checking if a path is a directory.
    *   Listing directory contents (streaming, with improved state machine for robust streaming).
    *   Creating directories.
    *   Removing empty directories.
    *   Removing files.
    *   Querying SD card type.
    *   Querying SD card volume information (FAT type, cluster size, total clusters).
    *   **Setting the SD card's real-time clock (RTC) via I2C (`C` command), allowing correct file timestamps.**
    *   **Automatic timestamping of files using the last set RTC value.**
    *   **Improved LED status indication for all major operations, including time set and error states.**
    *   **Robust handling of directory listing, including streaming of large directories and proper cleanup on interruption.**
    *   **Support for nested directories and long filenames (up to 63 characters).**
*   Uses two LEDs for status indication (Green for success/activity, Red for errors).

## Hardware Requirements

*   ATtiny1614 Microcontroller (or compatible megaTinyCore device).
*   SD Card Module (connected via SPI to the ATtiny1614 - specific pins depend on board/wiring, typically uses standard SPI pins MOSI, MISO, SCK, and a CS pin like PA4 as seen in command 'Q').
*   I2C Connection: Connect the ATtiny1614's SDA and SCL pins to the I2C master device. Ensure pull-up resistors are present on the I2C lines (often provided by the master or on breakout boards).
*   LEDs (Optional but recommended):
    *   Green LED connected to Pin 5 (PA1).
    *   Red LED connected to Pin 4 (PA2).

## Software Requirements

*   Arduino IDE.
*   [megaTinyCore](https://github.com/SpenceKonde/megaTinyCore) installed via the Arduino Board Manager to support the ATtiny1614.
*   Arduino `SD.h` library (usually included with the IDE or core).
*   Arduino `Wire.h` library (used implicitly by the TWI registers).

## I2C Communication Protocol

*   **Slave Address:** `0x55` (defined by `MyAddress`).
*   **General Workflow:** Most operations require setting the target filename/path first using the `F` command, followed by the desired action command (`W`, `R`, `S`, etc.).

### Command Details

The master device initiates communication by sending a command character to the slave address.

| Command | Name             | Master Action After Command                                     | Slave Action / Master Read Response                                                                                                                                                              |
| :------ | :--------------- | :-------------------------------------------------------------- | :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **`F`** | Filename         | Write filename bytes (up to `Namelength`-1, null-terminated)    | Sets the internal `Filename` buffer. Slave ACKs bytes.                                                                                                                                           |
| **`W`** | Write            | Write data bytes to be written to the file                      | Opens file (set by `F`) in write/create/truncate mode. Writes received bytes. Slave ACKs bytes.                                                                                                |
| **`A`** | Append           | Write data bytes to be appended to the file                     | Opens file (set by `F`) in write/create/append mode. Writes received bytes. Slave ACKs bytes.                                                                                                   |
| **`R`** | Read             | Read bytes from the slave                                       | Opens file (set by `F`) in read mode. Sends file content byte-by-byte until EOF or master NACKs/stops.                                                                                         |
| **`S`** | Size             | Read 4 bytes from the slave                                     | Opens file (set by `F`) in read mode. Sends the 32-bit file size (MSB first).                                                                                                                    |
| **`E`** | Exists           | Read 1 byte from the slave                                      | Checks if the path/file (set by `F`) exists. Sends `1` if it exists, `0` otherwise.                                                                                                              |
| **`K`** | Is Directory     | Read 1 byte from the slave                                      | Checks if the path (set by `F`) exists *and* is a directory. Sends `1` if true, `0` otherwise.                                                                                                   |
| **`L`** | List Directory   | Read bytes from the slave until `0xFF` marker is received       | Opens directory (set by `F`). Streams contents: `[Type][Name...\0][Size(4 bytes)]...[0xFF]`. Type='D' (Dir) or 'F' (File). Size=0 for dirs. Size is LSB first. Name is 8.3 format + null. |
| **`M`** | Make Directory   | Read 1 byte from the slave                                      | Creates directory (set by `F`). Sends `1` on success, `0` on failure.                                                                                                                            |
| **`D`** | Delete Directory | Read 1 byte from the slave                                      | Removes empty directory (set by `F`). Sends `1` on success, `0` on failure.                                                                                                                      |
| **`X`** | Delete File      | Read 1 byte from the slave                                      | Removes file (set by `F`). Sends `1` on success, `0` on failure.                                                                                                                                 |
| **`Q`** | Query Card Type  | Read 1 byte from the slave                                      | Initializes card if needed. Sends card type: `0`=None/Unknown, `1`=SD1, `2`=SD2, `3`=SDHC/XC.                                                                                                     |
| **`V`** | Volume Info      | Read 10 bytes from the slave                                    | Initializes volume if needed. Sends: `[Status(1)][FAT Type(1)][BlocksPerCluster(4 LSB)][ClusterCount(4 LSB)]`. Status: `1`=Success, `0xFF`=Fail.                                                |
| **`C`** | Set Clock        | Write 6 bytes: YY MM DD HH MM SS                                | Sets the internal RTC for timestamping files. Slave ACKs bytes and updates time for FAT file timestamps.                                                                                        |

**Note on `L` (List Directory) Stream:** The master should repeatedly request single bytes after sending the `L` command. The slave sends the directory entry type ('D' or 'F'), then the null-terminated filename, then the 4-byte size (LSB first). This repeats for each entry. The stream ends when the slave sends `0xFF`.

## LED Status Indicators

*   **Green LED (Pin 5):** Lit during successful file open (`W`, `R`, `A`, `S`), successful directory check (`K`), while actively streaming directory listing (`L`), and briefly after a successful clock set (`C`).
*   **Red LED (Pin 4):** Lit if a file/directory operation fails (e.g., cannot open file, path for `K` is not a directory, or invalid time data received).
*   **Both Off:** Idle state, or after an I2C STOP condition.

## Building and Flashing

1.  Install Arduino IDE and megaTinyCore.
2.  Select the correct ATtiny board (e.g., ATtiny1614) from the Tools menu.
3.  Configure board settings (Clock speed, etc.) as needed.
4.  Connect a suitable UPDI programmer.
5.  Open the `ATtiny1614-i2cSDBridge-v14.ino` sketch.
6.  Compile and upload the sketch to the ATtiny1614.

## Example Usage

Refer to example code for an I2C master device (like the corresponding `ESP8266-Demo-i2cSDBridge-v14.ino` sketch) to see how to send commands and interact with this slave device. The master code typically involves using the `Wire` library to:
1.  `Wire.beginTransmission(address)`
2.  `Wire.write('F')`
3.  `Wire.write("FILENAME.TXT")`
4.  `Wire.endTransmission()` (or `false` if followed immediately by another command)
5.  `Wire.beginTransmission(address)`
6.  `Wire.write('W')`
7.  `Wire.write("Data to write")`
8.  `Wire.endTransmission()`
Or for reading:
1.  Set filename with `F`.
2.  `Wire.beginTransmission(address)`
3.  `Wire.write('R')`
4.  `Wire.endTransmission(false)`
5.  `Wire.requestFrom(address, numBytes)`
6.  `Wire.read()` in a loop.

**To set the SD card's clock for correct file timestamps:**
1.  `Wire.beginTransmission(address)`
2.  `Wire.write('C')`
3.  `Wire.write(YY)` (year, 2 digits)
4.  `Wire.write(MM)` (month)
5.  `Wire.write(DD)` (day)
6.  `Wire.write(HH)` (hour)
7.  `Wire.write(MM)` (minute)
8.  `Wire.write(SS)` (second)
9.  `Wire.endTransmission()`

This ensures all files created or modified will have the correct FAT timestamp.
