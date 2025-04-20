/* i2c SD-Card Module inspired by David Johnson-Davies' i2c SD-Card Module
    Original code by David Johnson-Davies I2C SC-Card Module - see http://www.technoblogy.com/show?1LJI
    
   This example demonstrates how to use the ATtiny1614 as an I2C slave to communicate with an SD card.
   The ATtiny1614 is configured as an I2C slave with address 0x6E.
   The SD card is initialized using the SD library.
   The ATtiny1614 can be used as an I2C slave to communicate with an SD card.
   
   CC BY 4.0
   Licensed under a Creative Commons Attribution 4.0 International license: 
   http://creativecommons.org/licenses/by/4.0/
*/

#include <avr/wdt.h>
#include <SD.h>

// Define buffer size
#define BUFFER_SIZE 64

// Update the SD card pin definitions to match the schematic
#define SD_CS 10  // Chip Select pin connected to PA3
#define SD_CLK PA2 // Clock pin connected to PA2
#define SD_DO PA1  // Data Out pin connected to PA1 (MISO)
#define SD_DI PA0  // Data In pin connected to PA0 (MOSI)

// Update the LED pin definitions
#define LED_GREEN 5  // Green LED connected to PB2
#define LED_RED 4    // Red LED connected to PB3

// Status codes
#define STATUS_OK     0x00
#define STATUS_ERROR  0xFF
#define STATUS_BUSY   0x01
#define STATUS_DONE   0x02

// Command definitions
#define CMD_NONE      0x00
#define CMD_INIT      0x10
#define CMD_READ      0x20
#define CMD_STATUS    0x80

// File commands
#define CMD_FILE_CREATE   0x31
#define CMD_FILE_APPEND   0x32
#define CMD_FILE_OVERWRITE 0x33
#define CMD_FILE_DELETE   0x34
#define CMD_FILE_WRITE    0x35
#define CMD_FILE_EXISTS   0x40
#define CMD_IS_DIRECTORY  0x41

// Directory commands
#define DIR_CREATE    0x60
#define DIR_DELETE    0x61
#define DIR_LIST      0x52  // Changed from 0x50
#define DIR_LIST_REC  0x53  // Changed from 0x51

// Global variables
File currentFile;
File dir;
File currentEntry;
uint8_t cmdState = CMD_NONE;
uint8_t dataBuffer[64];
uint8_t bufferPos = 0;
uint8_t ptr = 0;

// Function declarations
void LightLED(uint8_t color);
uint8_t handleReading();
uint8_t handleListing();
uint8_t handleCommand();
void processData(uint8_t data);
//void executeCommand();

void LightLED(uint8_t color) {
    digitalWrite(LED_GREEN, color & 1);
    digitalWrite(LED_RED, (color >> 1) & 1);
}

uint8_t handleReading() {
    if (!currentFile) return STATUS_ERROR;
    
    if (currentFile.available()) {
        return currentFile.read();
    } else {
        currentFile.close();
        return STATUS_DONE;
    }
}

uint8_t handleListing() {
    if (!dir) {
        return STATUS_ERROR;
    }
    
    if (!currentEntry) {
        currentEntry = dir.openNextFile();
        if (!currentEntry) {
            dir.close();
            dir = File();
            return STATUS_DONE;
        }
        ptr = 0;
    }
    
    const char* name = currentEntry.name();
    if (!name || !name[ptr]) {
        File nextFile = dir.openNextFile();
        currentEntry.close();
        
        if (!nextFile) {
            dir.close();
            dir = File();
            return STATUS_DONE;
        }
        
        currentEntry = nextFile;
        ptr = 0;
        return 0;  // End of current entry
    }
    
    return name[ptr++];  // Return next character of filename
}

// Update the existing handleCommand function
uint8_t handleCommand() {
    switch(cmdState) {
        case CMD_READ:
            if (!currentFile) return STATUS_ERROR;
            
            if (currentFile.available()) {
                return currentFile.read();
            } else {
                currentFile.close();
                currentFile = File();
                return STATUS_DONE;
            }
            
        case DIR_LIST:
            return handleListing();
            
        case CMD_STATUS:
            return STATUS_OK;
            
        case CMD_FILE_EXISTS:
        case CMD_IS_DIRECTORY:
            return cmdState;  // Return the status set in executeCommand
            
        default:
            return cmdState;  // Return current state for status requests
    }
}

// Add setup_watchdog function to the beginning of setup
void setup() {
    // Enable watchdog timer with 8s timeout
    wdt_enable(WDT_PERIOD_8KCLK_gc);
    
    // Configure LED pins
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_RED, OUTPUT);
    
    // Turn off both LEDs initially
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_RED, LOW);
    
    // Blink both LEDs to indicate startup
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_GREEN, HIGH);
        digitalWrite(LED_RED, HIGH);
        delay(100);
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, LOW);
        delay(100);
    }
    
    // Setup I2C slave using TWI
    TWI0.SADDR = 0x6E << 1;  // Set slave address to 0x6E
    TWI0.SCTRLA = TWI_DIEN_bm | TWI_APIEN_bm | TWI_PIEN_bm | TWI_ENABLE_bm;
    TWI0.SCTRLB = TWI_ACKACT_ACK_gc;  // Set ACK as default action
    
    // Blink green LED to indicate I2C is ready
    for (int i = 0; i < 2; i++) {
        digitalWrite(LED_GREEN, HIGH);
        delay(100);
        digitalWrite(LED_GREEN, LOW);
        delay(100);
    }
    
    // Configure SD card pins explicitly
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH); // Deselect SD card initially
    
    // Try to initialize SD card with multiple attempts
    bool sdInitialized = false;
    for (int attempt = 0; attempt < 5; attempt++) {
        // Blink red LED to indicate SD init attempt
        digitalWrite(LED_RED, HIGH);
        delay(50);
        digitalWrite(LED_RED, LOW);
        delay(50);
        
        // Try to initialize SD card with the correct CS pin
        if (SD.begin(SD_CS)) {
            sdInitialized = true;
            break;
        }
        delay(200);
    }
    
    if (!sdInitialized) {
        // SD card initialization failed
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, HIGH);  // Keep RED on to indicate error
    } else {
        // SD card initialization succeeded
        digitalWrite(LED_GREEN, HIGH);
        digitalWrite(LED_RED, LOW);
        
        // Test SD card by creating a simple file
        File testFile = SD.open("test.txt", FILE_WRITE);
        if (testFile) {
            testFile.println("SD Card initialized successfully");
            testFile.close();
            
            // Verify file was created
            if (SD.exists("test.txt")) {
                // Success - file created and verified
                digitalWrite(LED_GREEN, HIGH);
                digitalWrite(LED_RED, LOW);
            } else {
                // File verification failed
                digitalWrite(LED_GREEN, LOW);
                digitalWrite(LED_RED, HIGH);
            }
        } else {
            // Failed to create test file
            digitalWrite(LED_GREEN, LOW);
            digitalWrite(LED_RED, HIGH);
        }
    }
}

// Update the ISR to properly handle command status
ISR(TWI0_TWIS_vect) {
    uint8_t status = TWI0.SSTATUS;
    
    if (status & TWI_APIF_bm) {
        // Address match
        TWI0.SSTATUS = TWI_APIF_bm;  // Clear the flag
        TWI0.SCTRLB = TWI_ACKACT_ACK_gc | TWI_SCMD_RESPONSE_gc;
    }
    
    if (status & TWI_DIF_bm) {
        if (status & TWI_DIR_bm) {
            // Master reading from slave (we need to send data)
            uint8_t response;
            
            // If we're in a command state, handle it
            if (cmdState == CMD_NONE) {
                response = STATUS_OK;  // Default to OK if no command is active
            } else if (cmdState == STATUS_OK || cmdState == STATUS_ERROR) {
                // Return the status and reset command state
                response = cmdState;
                cmdState = CMD_NONE;  // Reset command state after reporting status
            } else if (cmdState == CMD_READ) {
                // Handle reading data
                response = handleReading();
            } else if (cmdState == DIR_LIST) {
                // Handle directory listing
                response = handleListing();
            } else {
                // For other commands, return the current state
                response = cmdState;
            }
            
            TWI0.SDATA = response;
            TWI0.SSTATUS = TWI_DIF_bm;  // Clear the flag
            TWI0.SCTRLB = TWI_ACKACT_ACK_gc | TWI_SCMD_RESPONSE_gc;
        } else {
            // Master writing to slave (we need to receive data)
            uint8_t data = TWI0.SDATA;
            
            // Process the data
            processData(data);
            
            TWI0.SSTATUS = TWI_DIF_bm;  // Clear the flag
            TWI0.SCTRLB = TWI_ACKACT_ACK_gc | TWI_SCMD_RESPONSE_gc;
        }
    }
}

// Update the executeCommand function to properly set status codes
void executeCommand() {
    // Ensure the data buffer is null-terminated
    dataBuffer[bufferPos] = 0;
    
    switch(cmdState) {
        case CMD_INIT:
            // Re-initialize SD card
            LightLED(3);  // Both LEDs on during operation
            
            // Close any open files
            if (currentFile) currentFile.close();
            currentFile = File();
            
            if (SD.begin(SD_CS)) {
                LightLED(1);  // Success - Green LED
                cmdState = STATUS_OK;
            } else {
                LightLED(2);  // Error - Red LED
                cmdState = STATUS_ERROR;
            }
            break;
            
        case CMD_FILE_CREATE:
            // Close any open files
            if (currentFile) currentFile.close();
            currentFile = File();
            
            // Try to create the file with a simpler path
            char simplePath[64];
            strcpy(simplePath, (char*)dataBuffer);
            
            // Remove leading slash if present
            if (simplePath[0] == '/') {
                memmove(simplePath, simplePath + 1, strlen(simplePath));
            }
            
            // Try to create the file - use FILE_WRITE mode which creates or truncates
            LightLED(3);  // Both LEDs on during operation
            
            // Try with SD.open
            currentFile = SD.open(simplePath, FILE_WRITE);
            
            if (currentFile) {
                // File opened successfully
                LightLED(1);  // Success - Green LED
                cmdState = STATUS_OK;  // Set status to OK
            } else {
                // Failed to open file
                LightLED(2);  // Error - Red LED
                cmdState = STATUS_ERROR;  // Set status to ERROR
            }
            break;
            
        case CMD_FILE_WRITE:
            if (!currentFile) {
                LightLED(2);  // Error - Red LED
                cmdState = STATUS_ERROR;
            } else {
                // Write the data to the file
                if (bufferPos > 0) {
                    LightLED(3);  // Both LEDs on during write
                    size_t bytesWritten = currentFile.write(dataBuffer, bufferPos);
                    currentFile.flush();  // Make sure data is written to the card
                    
                    if (bytesWritten == bufferPos) {
                        LightLED(1);  // Success - Green LED
                        cmdState = STATUS_OK;
                    } else {
                        LightLED(2);  // Error - Red LED
                        cmdState = STATUS_ERROR;
                    }
                } else {
                    // Empty buffer or null byte received, close the file
                    currentFile.close();
                    currentFile = File();
                    LightLED(1);  // Success - Green LED
                    cmdState = STATUS_OK;
                }
            }
            break;
            
        case CMD_READ:
            if (currentFile) currentFile.close();
            
            // Try to open the file for reading
            currentFile = SD.open((char*)dataBuffer);
            
            if (currentFile) {
                LightLED(1);  // Success - Green LED
                cmdState = CMD_READ;  // Keep in read mode
            } else {
                LightLED(2);  // Error - Red LED
                cmdState = STATUS_ERROR;
            }
            break;
    }
    
    // Reset buffer position for next command
    bufferPos = 0;
}

// Update the processData function to execute commands immediately when null terminator is received
void processData(uint8_t data) {
    if (cmdState == CMD_NONE) {
        // This is a new command
        cmdState = data;
        bufferPos = 0;
    } else {
        // This is data for the current command
        if (bufferPos < BUFFER_SIZE - 1) {
            dataBuffer[bufferPos++] = data;
        }
        
        // If we received a null terminator, execute the command immediately
        if (data == 0) {
            dataBuffer[bufferPos] = 0;  // Ensure null termination
            executeCommand();  // Execute command immediately in the ISR
        }
    }
}

// Remove the command execution from the loop function since we're doing it in the ISR
void loop() {
    // Reset watchdog timer
    wdt_reset();
    
    // Check if we need to recover from a hang
    static unsigned long lastActivity = 0;
    static bool wasActive = false;
    
    if (cmdState != CMD_NONE) {
        lastActivity = millis();
        wasActive = true;
    } else if (wasActive && (millis() - lastActivity > 5000)) {
        // If we were active but haven't received a command in 5 seconds,
        // reset the system state
        if (currentFile) currentFile.close();
        if (dir) dir.close();
        if (currentEntry) currentEntry.close();
        
        currentFile = File();
        dir = File();
        currentEntry = File();
        
        // Reinitialize SD card
        SD.begin(SD_CS);
        
        // Reset activity flag
        wasActive = false;
        
        // Blink both LEDs to indicate recovery
        digitalWrite(LED_GREEN, HIGH);
        digitalWrite(LED_RED, HIGH);
        delay(100);
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, LOW);
    }
}
