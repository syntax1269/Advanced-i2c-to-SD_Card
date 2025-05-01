/* I2C SC-Card Module specifically designed for the ATtiny1614

Sketch uses 15020 bytes (91%) of program storage space. 
Global variables use 1009 bytes of dynamic memory.

*/

#include <SD.h>
// No specific RTC library needed, we manage time components manually
#include <avr/io.h>  // For potential future direct RTC use if needed

const int I2CAddress = 0x55;

// LEDs **********************************************

const int LEDoff = 0;
const int LEDgreen = 1;
const int LEDred = 2;

void LightLED(int colour) {
  digitalWrite(PIN_PB2, colour & 1); //PIN 5, physical pin 7
  digitalWrite(PIN_PB3, colour >> 1 & 1); //PIN 4, physical pin 6
}

// I2C Interface **********************************************

const int Namelength = 64;
char Filename[Namelength];
static union {
  unsigned long Filesize;
  uint8_t Filebytes[4];
};
// Adjust union to store volume info + FAT type
// Option 1: Keep separate variables (simpler for this change)
static uint32_t volBlocks;
static uint32_t volClusters;
static uint8_t volFatType;
// Option 2: Use a larger union/struct if preferred


// RTC Time Storage *******************************************
// Global variables to hold the current date and time
// Initialized to default: Jan 1, 2025 00:00:00
uint16_t currentYear = 2025;
uint8_t currentMonth = 1;
uint8_t currentDay = 1;
uint8_t currentHour = 0;
uint8_t currentMinute = 0;
uint8_t currentSecond = 0;

// Buffer for receiving time data via I2C 'C' command
uint8_t rtc_buffer[6];
uint8_t rtc_buffer_ptr = 0;


// SD Card stuff ******************************************
// set up variables using the SD utility library functions:
File myFile;
Sd2Card card;
SdVolume volume;
SdFile root;
// char dirResult[300] = ""; // REMOVED - No longer needed

// Static variables for directory listing streaming state ('L' command)
static File dirList_currentDir;
static File dirList_currentEntry;
static enum {  // States for directory listing stream
  DIR_STREAM_IDLE,
  DIR_STREAM_SEND_NAME,
  DIR_STREAM_SEND_SIZE,
  DIR_STREAM_SEND_TYPE_NEXT,  // Sends type for the *next* entry
  DIR_STREAM_SEND_END         // Sends the final 0xFF marker
} dirList_state = DIR_STREAM_IDLE;
static uint8_t dirList_byteCounter = 0;  // Counter for name/size bytes
static char dirList_filenameBuffer[13];  // Buffer for current entry's name (8.3 + null)
static uint32_t dirList_entrySize = 0;   // Size of the current file entry
static bool dirList_dirOpen = false;     // Flag if directory is currently open for streaming

// TWI setup **********************************************

void I2CSetup() {
  TWI0.CTRLA = 0;               // Default timings
  TWI0.SADDR = I2CAddress << 1;  // Bottom bit is R/W bit
  // Enable address, data, and stop interrupts:
  TWI0.SCTRLA = TWI_APIEN_bm | TWI_DIEN_bm | TWI_PIEN_bm | TWI_ENABLE_bm;
}

// SD Timestamp Callback Function *************************
// Provides the date and time to the SD library when files are created/modified
void dateTimeCallback(uint16_t* date, uint16_t* time) {
  // Use the global variables holding the current time
  *date = FAT_DATE(currentYear, currentMonth, currentDay);
  *time = FAT_TIME(currentHour, currentMinute, currentSecond);
}

// Functions to handle each of the cases **********************************************

int command = 0;            // Currently active command
int ch = 0, ptr = 0;        // Filename and size pointers / General purpose pointer
boolean checknack = false;  // Don't check Host NACK first time

boolean AddressHostRead() {
  // Reset ptr if starting a read operation that uses it (like 'V' or 'S')
  // This prevents leftover ptr values from previous commands interfering.
  // Note: 'L' command manages its own state/pointers internally.
  if (command == 'V' || command == 'S') {
    ptr = 0;
  }
  return true;
}

boolean AddressHostWrite() {
  command = 0;
  ch = 0;
  ptr = 0;             // Reset these on writing
  rtc_buffer_ptr = 0;  // Reset RTC buffer pointer too
  LightLED(LEDoff);    // <<< ADDED: Turn LED off at the start of a write command sequence
  // Reset directory listing state if a new command comes
  if (dirList_dirOpen) {
    if (dirList_currentEntry) dirList_currentEntry.close();
    dirList_currentDir.close();
    dirList_dirOpen = false;
  }
  dirList_state = DIR_STREAM_IDLE;
  dirList_byteCounter = 0;
  return true;
}


void DataHostRead() {
  if (command == 'R') {
    TWI0.SDATA = myFile.read();  // Host read operation
  } else if (command == 'E') {
    TWI0.SDATA = SD.exists(Filename);  // does file exists
  } else if (command == 'K') {         // MODIFIED: Check if path is a directory
    File checkFile = SD.open(Filename);
    if (checkFile && checkFile.isDirectory()) {
      TWI0.SDATA = 1;  // Path exists and is a directory
      LightLED(LEDgreen);
    } else {
      TWI0.SDATA = 0;  // Path doesn't exist or is not a directory
      LightLED(LEDred);
    }
    if (checkFile) {
      checkFile.close();
    }
    // ptr = 0; // ptr not used here, removed reset
  } else if (command == 'L') {  // MODIFIED: Stream directory listing byte-by-byte
    uint8_t dataToSend = 0xFF;  // Default to end marker

    switch (dirList_state) {
      case DIR_STREAM_IDLE:
        {  // Added braces
          // Attempt to open the directory
          dirList_currentDir = SD.open(Filename);
          if (!dirList_currentDir || !dirList_currentDir.isDirectory()) {
            // Failed to open or not a directory
            if (dirList_currentDir) dirList_currentDir.close();
            dirList_dirOpen = false;
            dataToSend = 0xFF;  // Send end marker
                                // Stay IDLE
          } else {
            dirList_dirOpen = true;
            // Try to get the first entry
            dirList_currentEntry = dirList_currentDir.openNextFile();
            if (!dirList_currentEntry) {
              // Directory is empty
              dirList_currentDir.close();
              dirList_dirOpen = false;
              dataToSend = 0xFF;  // Send end marker
                                  // Stay IDLE
            } else {
              // First entry found, send its TYPE
              dataToSend = dirList_currentEntry.isDirectory() ? 'D' : 'F';
              // Prepare name buffer
              const char* name = dirList_currentEntry.name();
              const char* baseName = strrchr(name, '/');  // Get part after last '/'
              baseName = baseName ? baseName + 1 : name;  // Handle root files
              memset(dirList_filenameBuffer, 0, sizeof(dirList_filenameBuffer));
              strncpy(dirList_filenameBuffer, baseName, sizeof(dirList_filenameBuffer) - 1);
              // Transition to sending name
              dirList_state = DIR_STREAM_SEND_NAME;
              dirList_byteCounter = 0;
            }
          }
          break;
        }  // Added braces

      case DIR_STREAM_SEND_NAME:
        {  // Added braces
          // Send characters of the filename
          if (dirList_byteCounter < strlen(dirList_filenameBuffer)) {
            dataToSend = dirList_filenameBuffer[dirList_byteCounter++];
          } else {
            // Finished sending name, send null terminator
            dataToSend = '\0';
            // Prepare size for next state
            dirList_entrySize = dirList_currentEntry.isDirectory() ? 0 : dirList_currentEntry.size();
            dirList_state = DIR_STREAM_SEND_SIZE;
            dirList_byteCounter = 0;  // Reset counter for size bytes
          }
          break;
        }  // Added braces

      case DIR_STREAM_SEND_SIZE:
        {  // Added braces
          // Send size bytes (LSB first)
          dataToSend = (uint8_t)(dirList_entrySize >> (8 * dirList_byteCounter));
          dirList_byteCounter++;
          if (dirList_byteCounter >= 4) {
            // Just sent the last byte of size. Decide next state for the *next* read.
            dirList_currentEntry.close();                              // Close current entry object
            dirList_currentEntry = dirList_currentDir.openNextFile();  // Try to get next
            if (dirList_currentEntry) {
              // Next entry exists, prepare to send its type next
              dirList_state = DIR_STREAM_SEND_TYPE_NEXT;
            } else {
              // No more entries
              dirList_currentDir.close();  // Close the directory object
              dirList_dirOpen = false;
              dirList_state = DIR_STREAM_SEND_END;  // Prepare to send end marker next
            }
          }
          // If counter < 4, dataToSend is already set to the current size byte
          break;
        }  // Added braces

      case DIR_STREAM_SEND_TYPE_NEXT:
        {  // Added braces
          // Send TYPE of the new entry found in the previous step
          dataToSend = dirList_currentEntry.isDirectory() ? 'D' : 'F';
          // Prepare name buffer for the new entry
          const char* name = dirList_currentEntry.name();
          const char* baseName = strrchr(name, '/');
          baseName = baseName ? baseName + 1 : name;
          memset(dirList_filenameBuffer, 0, sizeof(dirList_filenameBuffer));
          strncpy(dirList_filenameBuffer, baseName, sizeof(dirList_filenameBuffer) - 1);
          // Transition to sending name
          dirList_state = DIR_STREAM_SEND_NAME;
          dirList_byteCounter = 0;
          break;
        }  // Added braces

      case DIR_STREAM_SEND_END:
        {  // Added braces and handled this case
          // Send the final end marker
          dataToSend = 0xFF;
          dirList_state = DIR_STREAM_IDLE;  // Reset state machine
          // Cleanup happens in Stop() if needed, or can be added here
          break;
        }  // Added braces
    }      // End switch(dirList_state)

    TWI0.SDATA = dataToSend;

    // Simplified LED logic
    if (dirList_state != DIR_STREAM_IDLE) {
      LightLED(LEDgreen);  // Green if actively streaming
    } else {
      LightLED(LEDoff);  // Off when idle/finished
    }

    // Update ptr status (used elsewhere?) - This might need review depending on 'ptr' usage
    ptr = (dirList_state != DIR_STREAM_IDLE);

  } else if (command == 'M') {
    TWI0.SDATA = SD.mkdir(Filename);  // Create directory
  } else if (command == 'Q') {
    //TWI0.SDATA = card.type(); // Query SD Card for type
    if (card.init(SPI_HALF_SPEED, PIN_PA4)) {
      TWI0.SDATA = card.type();
      // type will be 0, 1, 2, or 3 as described above
    }
  } else if (command == 'V') {
    // Handle Volume Info request - multi-byte response (10 bytes total)
    if (ptr == 0) {  // First read request for 'V' command
      if (volume.init(card)) {
        // Store volume info
        volFatType = volume.fatType();
        volBlocks = volume.blocksPerCluster();
        volClusters = volume.clusterCount();

        TWI0.SDATA = 0x01;  // Send success status byte
        ptr = 1;            // Set state for next byte (FAT Type)
      } else {
        TWI0.SDATA = 0xFF;  // Send failure status byte
        ptr = -1;           // Set state to indicate error
      }
    } else if (ptr == 1) {  // Second read request: Send FAT Type
      TWI0.SDATA = volFatType;
      ptr++;                            // Move to next state (first byte of blocks)
    } else if (ptr >= 2 && ptr <= 5) {  // Read requests 3-6: Send Blocks (4 bytes LSB first)
      // ptr=2 sends LSB of volBlocks (index 0)
      // ptr=3 sends byte 1 of volBlocks (index 1)
      // ...
      // ptr=5 sends MSB of volBlocks (index 3)
      TWI0.SDATA = (uint8_t)(volBlocks >> (8 * (ptr - 2)));
      ptr++;
    } else if (ptr >= 6 && ptr <= 9) {  // Read requests 7-10: Send Clusters (4 bytes LSB first)
      // ptr=6 sends LSB of volClusters (index 0)
      // ...
      // ptr=9 sends MSB of volClusters (index 3)
      TWI0.SDATA = (uint8_t)(volClusters >> (8 * (ptr - 6)));
      ptr++;
    } else {              // Error state (ptr == -1) or finished sending (ptr > 9)
      TWI0.SDATA = 0x00;  // Send 0 after error or completion
    }
  } else if (command == 'D') {
    TWI0.SDATA = SD.rmdir(Filename);  // Remove directory
  } else if (command == 'X') {
    TWI0.SDATA = SD.remove(Filename);  // remove file
  } else if (command == 'S') {
    if (ptr < 4) {
      if (ptr == 0) Filesize = myFile.size();
      TWI0.SDATA = Filebytes[3 - ptr];  // MSB first
      ptr++;
    } else TWI0.SDATA = 0;  // Host read too many bytes
  } else TWI0.SDATA = 0;    // Read in other situations
}

boolean DataHostWrite() {
  uint8_t received_data = TWI0.SDATA;  // Read data once at the start

  if (command == 0) {  // First byte received after address match -> This is the command byte
    command = received_data;
    ptr = 0;  // Reset pointers/counters for the new command
    ch = 0;
    rtc_buffer_ptr = 0;

    // Handle commands immediately if they don't need file setup here or are handled elsewhere
    if (command == 'Q' || command == 'V' || command == 'K' || command == 'M' || command == 'D' || command == 'X' || command == 'L' || command == 'E' || command == 'C' || command == 'F') {
      // ACK the command byte itself for these commands
      // File operations for K, M, D, X, L, E are done in DataHostRead or directly
      // F and C just set state for subsequent data bytes
      // Q and V are handled in DataHostRead
      return true;
    }
    // Handle commands that require opening a file *now*
    else if (command == 'W') {
      myFile = SD.open(Filename, O_RDWR | O_CREAT | O_TRUNC);
    } else if (command == 'R' || command == 'S') {  // S needs the file open to get size later
      myFile = SD.open(Filename, O_READ);
    } else if (command == 'A') {
      myFile = SD.open(Filename, O_RDWR | O_CREAT | O_APPEND);
    } else {
      // Unknown command received
      command = 0;   // Reset command state
      return false;  // NACK unknown command
    }

    // Check if file was opened successfully for W, R, S, A
    if (myFile) {
      LightLED(LEDgreen);
      return true;  // ACK command byte because file opened successfully
    } else {
      LightLED(LEDred);
      command = 0;   // Reset command state as it failed
      return false;  // NACK command byte because file failed to open
    }

  } else {  // Subsequent bytes received before STOP -> This is data for the current 'command'
    switch (command) {
      case 'F':  // Filename data bytes
        if (ch < Namelength) {
          Filename[ch++] = received_data;
          Filename[ch] = 0;  // Null-terminate
          return true;       // ACK filename byte
        } else {
          return false;  // NACK if filename too long
        }
        // break; // Unreachable due to return

      case 'W':  // Write data bytes
      case 'A':  // Append data bytes
        if (myFile) {
          myFile.write(received_data);
          return true;  // ACK data byte
        } else {
          // Should not happen if file opening logic above is correct
          LightLED(LEDred);
          return false;  // NACK if file not open
        }
        // break; // Unreachable

      case 'C':  // Clock set data bytes
        if (rtc_buffer_ptr < 6) {
          rtc_buffer[rtc_buffer_ptr++] = received_data;
          return true;  // ACK clock data byte
        } else {
          // Received more than 6 bytes for clock command
          return false;  // NACK extra clock data bytes
        }
        // break; // Unreachable

      default:
        // Host is writing data for a command that doesn't expect it
        // (e.g., R, S, V, Q, K, L, E, M, D, X)
        return false;  // NACK unexpected data write
    }
  }
  // Should be unreachable if all cases are handled
  return false;
}

void Stop() {
  checknack = false;  // Reset NACK check flag

  // Process received time data if 'C' command finished successfully
  if (command == 'C' && rtc_buffer_ptr == 6) {
    // Update the global time variables
    // Assuming YY, MM, DD, HH, MM, SS format from ESP8266
    // Correctly reconstruct year based on FAT timestamp epoch (1980)
    // YY values 00-79 correspond to 2000-2079
    // YY values 80-99 correspond to 1980-1999
    uint8_t yy = rtc_buffer[0];
    uint16_t year_rx = (yy < 80) ? (2000 + yy) : (1900 + yy);
    uint8_t month_rx = rtc_buffer[1];
    uint8_t day_rx = rtc_buffer[2];
    uint8_t hour_rx = rtc_buffer[3];
    uint8_t minute_rx = rtc_buffer[4];
    uint8_t second_rx = rtc_buffer[5];

    // Basic validation (optional but recommended)
    if (month_rx >= 1 && month_rx <= 12 && day_rx >= 1 && day_rx <= 31 && hour_rx <= 23 && minute_rx <= 59 && second_rx <= 59 && year_rx >= 1980)  // Add year validation
    {
      currentYear = year_rx;
      currentMonth = month_rx;
      currentDay = day_rx;
      currentHour = hour_rx;
      currentMinute = minute_rx;
      currentSecond = second_rx;
      LightLED(LEDgreen);  // Indicate time set success briefly
      delay(50);           // Short delay
      LightLED(LEDoff);
    } else {
      LightLED(LEDred);  // Indicate invalid time data received
      delay(50);
      LightLED(LEDoff);
    }
  }  // End of 'C' command processing

  // Close file if it was opened by W, R, A, or S command
  // Note: 'S' command opens the file in DataHostWrite to allow size reading in DataHostRead
  if (myFile && (command == 'W' || command == 'R' || command == 'A' || command == 'S')) {
    myFile.close();
    LightLED(LEDoff);  // Turn off LED after closing file
  }

  // Clean up directory listing resources if 'L' was interrupted or finished
  if (dirList_dirOpen) {
    if (dirList_currentEntry) dirList_currentEntry.close();
    dirList_currentDir.close();
    dirList_dirOpen = false;
  }
  dirList_state = DIR_STREAM_IDLE;  // Reset state on stop
  dirList_byteCounter = 0;

  // Reset command state variables AFTER potentially using 'command' above
  command = 0;
  ch = 0;
  ptr = 0;
  rtc_buffer_ptr = 0;  // Reset RTC buffer pointer

  // Ensure LED is off if not already turned off by file close
  // LightLED(LEDoff); // This might override the brief green/red flash from time set
}

void SendResponse(boolean succeed) {
  if (succeed) {
    TWI0.SCTRLB = TWI_ACKACT_ACK_gc | TWI_SCMD_RESPONSE_gc;  // Send ACK
  } else {
    TWI0.SCTRLB = TWI_ACKACT_NACK_gc | TWI_SCMD_RESPONSE_gc;  // Send NACK
  }
}

// TWI interrupt service routine **********************************************

// TWI interrupt
ISR(TWI0_TWIS_vect) {
  boolean succeed;

  // Address interrupt:
  if ((TWI0.SSTATUS & TWI_APIF_bm) && (TWI0.SSTATUS & TWI_AP_bm)) {
    if (TWI0.SSTATUS & TWI_DIR_bm) {  // Host reading from client
      succeed = AddressHostRead();
    } else {
      succeed = AddressHostWrite();  // Host writing to client
    }
    SendResponse(succeed);
    return;
  }

  // Data interrupt:
  if (TWI0.SSTATUS & TWI_DIF_bm) {
    if (TWI0.SSTATUS & TWI_DIR_bm) {  // Host reading from client
      if ((TWI0.SSTATUS & TWI_RXACK_bm) && checknack) {
        checknack = false;
      } else {
        DataHostRead();
        checknack = true;
      }
      TWI0.SCTRLB = TWI_SCMD_RESPONSE_gc;  // No ACK/NACK needed
    } else {                               // Host writing to client
      succeed = DataHostWrite();
      SendResponse(succeed);
    }
    return;
  }

  // Stop interrupt:
  if ((TWI0.SSTATUS & TWI_APIF_bm) && (!(TWI0.SSTATUS & TWI_AP_bm))) {
    Stop();
    TWI0.SCTRLB = TWI_SCMD_COMPTRANS_gc;  // Complete transaction
    return;
  }
}

// Setup **********************************************

void setup(void) {
  pinMode(PIN_PB3, OUTPUT);  // Red LED //Arduino PIN 4, physical pin 6
  pinMode(PIN_PB2, OUTPUT);  // Green LED //Arduino PIN 5, physical pin 7
  LightLED(LEDgreen);  // Green = Initialising
  I2CSetup();          // Setup I2C Slave
  // see if the card is present and can be initialized:
  // Use the correct CS pin for ATtiny1614 (often PA4/SS if using default SPI pins)
  // Check your specific board/wiring. Assuming default SS is PA4 (pin 3 in Arduino numbering for megaTinyCore)
  if (!SD.begin()) {   // Use Arduino pin number for CS
    LightLED(LEDred);  // Solid Red = SD Error
    while (1)
      ;  // don't do anything more
  }

  // --- Set the timestamp callback ---
  SdFile::dateTimeCallback(dateTimeCallback);  // <<< ADDED: Register the callback

  // Get volume info once at startup
  if (!volume.init(card)) {
    LightLED(LEDred);  // Solid Red = SD Error
    while (1)
      ;
  } else {
    volFatType = volume.fatType();          // Store FAT type
    volBlocks = volume.blocksPerCluster();  // Store blocks per cluster
    volClusters = volume.clusterCount();    // Store cluster count
  }

  LightLED(LEDoff);  // Turn off LED after successful init
}

void loop(void) {
  // Keep empty or add minimal delay
  delay(1);  // Small delay to prevent potential watchdog issues if loop is completely empty
}
