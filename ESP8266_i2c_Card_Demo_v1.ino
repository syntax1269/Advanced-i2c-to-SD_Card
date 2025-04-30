
#include <Wire.h>
#include <vector> // For directory listing storage
#include <string> // For directory listing storage

const int I2C_SDBRIDGE_ADDR = 0x55; // The I2C address of your ATtiny bridge
const size_t I2C_BUFFER_LIMIT = 32; // Standard Arduino Wire buffer size

// --- Helper Function to Send Filename ---
bool sendFilename(const char* filename) {
  Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
  Wire.write('F'); // Filename command
  Wire.write(filename);
  uint8_t error = Wire.endTransmission(true); // Send STOP after filename
  if (error != 0) {
    Serial.print("  [Error] Failed to send filename '"); Serial.print(filename);
    Serial.print("'. I2C Error: "); Serial.println(error);
    return false;
  }
  // Serial.print("  Filename '"); Serial.print(filename); Serial.println("' sent.");
  delay(5); // Short delay for bridge processing
  return true;
}

// --- Function to Query Card Type ('Q') ---
void queryCardType() {
  Serial.println("\n--- Querying Card Type ('Q') ---");
  Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
  Wire.write('Q');
  uint8_t error = Wire.endTransmission(false); // Send command, NO STOP
  if (error != 0) {
    Serial.print("  [Error] Failed to send 'Q' command. I2C Error: "); Serial.println(error);
    return;
  }

  uint8_t bytesReceived = Wire.requestFrom(I2C_SDBRIDGE_ADDR, 1, 1); // Request 1 byte, send STOP
  if (bytesReceived == 1) {
    uint8_t cardType = Wire.read();
    Serial.print("  Card Type Received: ");
    Serial.print(cardType);
    switch (cardType) {
      case 0: Serial.println(" (Unknown/Error)"); break;
      case 1: Serial.println(" (SD1)"); break;
      case 2: Serial.println(" (SD2)"); break;
      case 3: Serial.println(" (SDHC/SDXC)"); break;
      default: Serial.println(" (Invalid Response)"); break;
    }
  } else {
    Serial.println("  [Error] Did not receive expected byte for card type.");
  }
}

// --- Function to Get Volume Info ('V') ---
void getVolumeInfo() {
  Serial.println("\n--- Getting Volume Info ('V') ---");
  Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
  Wire.write('V');
  uint8_t error = Wire.endTransmission(false); // Send command, NO STOP
  if (error != 0) {
    Serial.print("  [Error] Failed to send 'V' command. I2C Error: "); Serial.println(error);
    return;
  }

  uint8_t bytesReceived = Wire.requestFrom(I2C_SDBRIDGE_ADDR, 10, 1); // Request 10 bytes (Status + FAT + Blocks + Clusters), send STOP
  if (bytesReceived == 10) {
    uint8_t status = Wire.read();
    if (status == 0x01) { // Success
      Serial.println("  Volume Init Success.");
      uint8_t fatType = Wire.read();
      uint32_t blocksPerCluster = 0;
      uint32_t clusterCount = 0;

      // Read Blocks per Cluster (4 bytes, LSB first)
      for (int i = 0; i < 4; i++) {
        blocksPerCluster |= ((uint32_t)Wire.read() << (8 * i));
      }
      // Read Cluster Count (4 bytes, LSB first)
      for (int i = 0; i < 4; i++) {
        clusterCount |= ((uint32_t)Wire.read() << (8 * i));
      }

      Serial.print("  FAT Type: FAT"); Serial.println(fatType);
      Serial.print("  Blocks Per Cluster: "); Serial.println(blocksPerCluster);
      Serial.print("  Cluster Count: "); Serial.println(clusterCount);
      Serial.print("  *Assuming 512 bytes/block");
      // Calculate Volume Size (approx)
      // Note: Requires block size, which we don't get directly. Assuming 512 bytes/block.
       uint64_t volumeSizeBytes = (uint64_t)clusterCount * blocksPerCluster * 512;
       Serial.print("  Approx Volume Size (MB): "); Serial.println((double)volumeSizeBytes / 1024.0 / 1024.0);
       Serial.print("\n  Approx Volume Size (GB): "); Serial.println((double)volumeSizeBytes / 1024.0 / 1024.0 / 1024.0);

    } else if (status == 0xFF) {
      Serial.println("  [Error] Bridge reported volume init failure.");
      // Read remaining dummy bytes
      for(int i=0; i<9; i++) Wire.read();
    } else {
       Serial.print("  [Error] Unexpected status byte: 0x"); Serial.println(status, HEX);
       // Read remaining dummy bytes
       for(int i=0; i<9; i++) Wire.read();
    }
  } else {
    Serial.print("  [Error] Did not receive expected 10 bytes for volume info. Received: ");
    Serial.println(bytesReceived);
    while(Wire.available()) Wire.read(); // Clear buffer
  }
}

// --- Function to Check if Path Exists ('E' for files, 'K' for directories) ---
bool checkExists(const char* path, bool isDirectory) {
  Serial.print("--- Checking if "); Serial.print(isDirectory ? "Directory" : "File");
  Serial.print(" '"); Serial.print(path); Serial.print("' exists ('");
  Serial.print(isDirectory ? 'K' : 'E'); Serial.println("') ---");

  if (!sendFilename(path)) return false; // Send filename first

  Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
  Wire.write(isDirectory ? 'K' : 'E'); // Send appropriate command
  uint8_t error = Wire.endTransmission(false); // Send command, NO STOP
  if (error != 0) {
    Serial.print("  [Error] Failed to send check command. I2C Error: "); Serial.println(error);
    Wire.endTransmission();
    return false; // Indicate uncertainty
  }

  uint8_t bytesReceived = Wire.requestFrom(I2C_SDBRIDGE_ADDR, 1, 1); // Request 1 byte, send STOP
  if (bytesReceived == 1) {
    uint8_t result = Wire.read();
    Serial.print("  Result: "); Serial.print(result);
    if (result == 1) {
      Serial.println(" (Exists)");
      Wire.endTransmission();
      return true;
    } else {
      Serial.println(" (Does Not Exist or Not a Dir)");
      Wire.endTransmission();
      return false;
    }
  } else {
    Wire.endTransmission();
    Serial.println("  [Error] Did not receive expected byte for existence check.");
    return false; // Indicate uncertainty
  }
}

// --- Function to Create Directory ('M') ---
bool makeDirectory(const char* dirname) {
  Serial.print("--- Creating Directory '"); Serial.print(dirname); Serial.println("' ('M') ---");
  if (!sendFilename(dirname)) return false;

  Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
  Wire.write('M');
  uint8_t error = Wire.endTransmission(false); // Send command, NO STOP
  if (error != 0) {
    Serial.print("  [Error] Failed to send 'M' command. I2C Error: "); Serial.println(error);
    return false;
  }

  uint8_t bytesReceived = Wire.requestFrom(I2C_SDBRIDGE_ADDR, 1, 1); // Request 1 byte, send STOP
  if (bytesReceived == 1) {
    uint8_t result = Wire.read();
    Serial.print("  Result: "); Serial.print(result);
    if (result == 1) {
      Serial.println(" (Success)");
      return true;
    } else {
      Serial.println(" (Failed)");
      return false;
    }
  } else {
    Serial.println("  [Error] Did not receive expected byte for mkdir result.");
    return false;
  }
}

// --- Function to Remove Directory ('D') ---
bool removeDirectory(const char* dirname) {
  Serial.print("--- Removing Directory '"); Serial.print(dirname); Serial.println("' ('D') ---");
  if (!sendFilename(dirname)) return false;

  Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
  Wire.write('D');
  uint8_t error = Wire.endTransmission(false); // Send command, NO STOP
  if (error != 0) {
    Serial.print("  [Error] Failed to send 'D' command. I2C Error: "); Serial.println(error);
    return false;
  }

  uint8_t bytesReceived = Wire.requestFrom(I2C_SDBRIDGE_ADDR, 1, 0); // Request 1 byte, send NO STOP
  if (bytesReceived == 1) {
    uint8_t result = Wire.read();
    Serial.print("  Result: "); Serial.print(result);
    if (result == 1) {
      Serial.println(" (Success)");
      Wire.endTransmission();
      return true;
    } else {
      Serial.println(" (Failed) Directory might not be empty.");
     // Wire.endTransmission();
      return false;
    }
  } else {
    Serial.println("  [Error] Did not receive expected byte for rmdir result.");
    //Wire.endTransmission();
    return false;
  }
}

// --- Function to Remove File ('X') ---
bool removeFile(const char* filename) {
  Serial.print("--- Removing File '"); Serial.print(filename); Serial.println("' ('X') ---");
  if (!sendFilename(filename)) return false;

  Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
  Wire.write('X');
  uint8_t error = Wire.endTransmission(false); // Send command, NO STOP
  if (error != 0) {
    Serial.print("  [Error] Failed to send 'X' command. I2C Error: "); Serial.println(error);
    return false;
  }

  uint8_t bytesReceived = Wire.requestFrom(I2C_SDBRIDGE_ADDR, 1, 1); // Request 1 byte, send STOP
  if (bytesReceived == 1) {
    uint8_t result = Wire.read();
    Serial.print("  Result: "); Serial.print(result);
    if (result == 1) {
      Serial.println(" (Success)");
      return true;
    } else {
      Serial.println(" (Failed)");
      return false;
    }
  } else {
    Serial.println("  [Error] Did not receive expected byte for remove file result.");
    return false;
  }
  Wire.endTransmission();
}

// --- Function to Write/Append Data ('W'/'A') - Byte-by-Byte ---
bool writeFile1(const char* filename, const char* data, char command) {
  Serial.print("--- Writing/Appending to File '"); Serial.print(filename);
  Serial.print("' ('"); Serial.print(command); Serial.println("') ---");
  if (!sendFilename(filename)) return false;

  const size_t dataLen = strlen(data);
  Serial.print("  Data Length: "); Serial.println(dataLen);

  if (dataLen == 0 && command == 'W') {
     // Handle empty write: Send command once to potentially truncate file
     Serial.println("  Sending empty 'W' command...");
     Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
     Wire.write(command); // Send 'W'
     uint8_t error = Wire.endTransmission(true); // Send STOP
     if (error != 0) {
         Serial.print("  [Error] I2C Error sending empty 'W' command: "); Serial.println(error);
         return false;
     } else {
         Serial.println("  Sent empty 'W' command (file may be truncated).");
         return true;
     }
  } else if (dataLen == 0 && command == 'A') {
     Serial.println("  Warning: writeFile called with empty data for append ('A'). No action taken.");
     return true; // Nothing to send for empty append
  }

  // Send each byte individually
  for (size_t i = 0; i < dataLen; ++i) {
      Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
      Wire.write(command); // Send 'W' or 'A' command
      Wire.write(data[i]); // Send the single data byte
      uint8_t error = Wire.endTransmission(true); // Send STOP after each byte

      if (error != 0) {
          Serial.print("\n  [Error] I2C Error during byte write (byte #"); Serial.print(i+1);
          Serial.print("): "); Serial.println(error);
          return false; // Exit function on error
      }
      if ((i+1) % 50 == 0) Serial.print("."); // Progress indicator
      delay(2); // Small delay between bytes
  }
  Serial.println("\n  Write/Append finished successfully.");
  return true;
}

bool writeFile(const char* filename, const char* msg, char command) {
  /* Command  Name  Description
     'Filename'  Specifies the filename [8.3 filename structure].
     'W'  Write data  Writes data to the file, overwriting if necessary.
     'A'  Append data Appends data to the end of the file, if it already exists.
  */

  // Send filename first
  Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
  Wire.write('F');
  if (strlen(filename) < 31){
    Serial.print("File name size: ");
    Serial.println(strlen(filename));
  Wire.write(filename, strlen(filename)); // Sends the entire string content (excluding the null terminator) in potentially a more optimized way.
  } else {
    Serial.print("File name size: ");
    Serial.println(strlen(filename));
  while (*filename) Wire.write(*filename++); // Sends the filename one character at a time, stopping just before the null terminator.
  }
  uint8_t error = Wire.endTransmission();  // Send STOP after filename
  if (error != 0) {
    Serial.print("I2C Error sending filename for storetoSD: ");
    Serial.println(error);
    return false;
  }
  //CustDelay(5);  // Small CustDelay after sending filename

  // Calculate message length
  const size_t msgLen = strlen(msg);
  if (msgLen == 0) {
    Serial.println("Warning: storetoSD called with empty message.");
    // If command is 'W', we might want to explicitly truncate the file here.
    // This requires a separate command on the ATtiny or handling 0-length write.
    // For now, just return.
    return false;
  }

  const size_t bufferSize = 31;  // Max I2C buffer size - 1 for command byte
  size_t offset = 0;

  // --- Strategy: Send first chunk with original command, subsequent chunks with 'A' ---

  // Send first chunk
  Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
  Wire.write(command);  // Use the initial command ('W' or 'A')
  size_t bytesToWrite = min(bufferSize, msgLen - offset);
  Wire.write(msg + offset, bytesToWrite);
  error = Wire.endTransmission(true);  // Send STOP
  if (error != 0) {
    Serial.print("I2C Error during first write chunk: ");
    Serial.println(error);
    return false;
  }
  offset += bytesToWrite;

  // Send subsequent chunks (if any) ALWAYS using 'A' (append)
  while (offset < msgLen) {
    Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
    Wire.write('A');  // <<< ALWAYS use Append for subsequent chunks
    bytesToWrite = min(bufferSize, msgLen - offset);
    Wire.write(msg + offset, bytesToWrite);
    error = Wire.endTransmission(true);  // Send STOP
    if (error != 0) {
      Serial.print("I2C Error during subsequent append chunk: ");
      Serial.println(error);
      return false;
    }
    offset += bytesToWrite;
  }
  return true;
}

// --- Function to Get File Size ('S') ---
uint32_t getFileSize(const char* filename) {
  Serial.print("--- Getting Size of File '"); Serial.print(filename); Serial.println("' ('S') ---");
  if (!sendFilename(filename)) return 0xFFFFFFFF; // Return error indicator

  Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
  Wire.write('S');
  uint8_t error = Wire.endTransmission(false); // Send command, NO STOP
  if (error != 0) {
    Serial.print("  [Error] Failed to send 'S' command. I2C Error: "); Serial.println(error);
    return 0xFFFFFFFF; // Return error indicator
  }

  uint8_t bytesReceived = Wire.requestFrom(I2C_SDBRIDGE_ADDR, 4, 0); // Request 4 bytes, send NO STOP
  if (bytesReceived == 4) {
    uint32_t fileSize = 0;
    // Read size (4 bytes, MSB first as per ATtiny code)
    uint8_t b3 = Wire.read();
    uint8_t b2 = Wire.read();
    uint8_t b1 = Wire.read();
    uint8_t b0 = Wire.read();
    fileSize = ((uint32_t)b3 << 24) | ((uint32_t)b2 << 16) | ((uint32_t)b1 << 8) | b0;

    Serial.print("  File Size Received: "); Serial.println(fileSize);
    return fileSize;
  } else {
    Serial.print("  [Error] Did not receive expected 4 bytes for file size. Received: ");
    Serial.println(bytesReceived);
    while(Wire.available()) Wire.read(); // Clear buffer
    Wire.endTransmission();
    return 0xFFFFFFFF; // Return error indicator
  }
}

// --- Function to Read File Content ('R') ---
void readFileContent(const char* filename) {
  Serial.print("--- Reading Content of File '"); Serial.print(filename); Serial.println("' ('R') ---");

  // First, get the file size to know how much to read
  uint32_t fileSize = getFileSize(filename);
  if (fileSize == 0xFFFFFFFF) {
    Serial.println("  [Error] Cannot read file because size could not be determined.");
    return;
  }
   if (fileSize == 0) {
    Serial.println("  File is empty. Nothing to read.");
    return;
  }

  // Send filename again (required before 'R' command)
  if (!sendFilename(filename)) return;

  Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
  Wire.write('R');
  uint8_t error = Wire.endTransmission(false); // Send command, NO STOP
  if (error != 0) {
    Serial.print("  [Error] Failed to send 'R' command. I2C Error: "); Serial.println(error);
    return;
  }

  Serial.println("  File Content:");
  Serial.print("  \""); // Start quote

  uint32_t bytesRead = 0;
  while (bytesRead < fileSize) {
    // Request data in chunks (respecting buffer limits)
    int bytesToRequest = min((size_t)(fileSize - bytesRead), I2C_BUFFER_LIMIT);
    if (bytesToRequest == 0) break; // Should not happen, but safety check

    uint8_t received = Wire.requestFrom(I2C_SDBRIDGE_ADDR, bytesToRequest, 0); // Do NOT Send STOP after each chunk

    if (received == 0) {
      Serial.print("\""); // End quote
      Serial.println("\n  [Error] Failed to receive data during read.");
      Wire.endTransmission();
      break;

    }

    for (uint8_t i = 0; i < received; i++) {
      Serial.print((char)Wire.read());
    }
    bytesRead += received;
  }
Wire.endTransmission();
  Serial.println("\""); // End quote
  Serial.print("  Total bytes read: "); Serial.println(bytesRead);
}


// --- Function to List Directory Contents ('L') ---
void listDirectory(const char* dirname) {
    Serial.print("--- Listing Directory '"); Serial.print(dirname); Serial.println("' ('L') ---");
    if (!sendFilename(dirname)) return;

    Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
    Wire.write('L');
    uint8_t error = Wire.endTransmission(false); // Send command, NO STOP
    if (error != 0) {
        Serial.print("  [Error] Failed to send 'L' command. I2C Error: "); Serial.println(error);
        return;
    }

    Serial.println("  Type | Size       | Name");
    Serial.println("  ----------------------------");

    bool firstEntry = true;
    while (true) {
        // Request Type byte
        uint8_t bytesReceived = Wire.requestFrom(I2C_SDBRIDGE_ADDR, 1, 0); // NO STOP yet
        if (bytesReceived != 1) {
            Serial.println("  [Error] Failed to receive Type byte.");
            Wire.endTransmission(true); // Send STOP to abort
            return;
        }
        uint8_t entryType = Wire.read();

        if (entryType == 0xFF) { // End of listing marker
            if (firstEntry) {
                Serial.println("  (Directory is empty or does not exist)");
            }
            Wire.endTransmission(true); // Send final STOP
            break;
        }
        firstEntry = false;

        // Read Name (null-terminated string)
        String entryName = "";
        while (true) {
            bytesReceived = Wire.requestFrom(I2C_SDBRIDGE_ADDR, 1, 0); // NO STOP
            if (bytesReceived != 1) {
                 Serial.println("\n  [Error] Failed to receive Name byte.");
                 Wire.endTransmission(true); // Send STOP to abort
                 return;
            }
            char c = Wire.read();
            if (c == '\0') break; // End of name
            entryName += c;
        }

        // Read Size (4 bytes, LSB first)
        uint32_t entrySize = 0;
        bytesReceived = Wire.requestFrom(I2C_SDBRIDGE_ADDR, 4, 0); // NO STOP
        if (bytesReceived == 4) {
             for (int i = 0; i < 4; i++) {
                entrySize |= ((uint32_t)Wire.read() << (8 * i));
             }
        } else {
             Serial.print("\n  [Error] Failed to receive Size bytes (received ");
             Serial.print(bytesReceived); Serial.println(").");
             Wire.endTransmission(true); // Send STOP to abort
             return;
        }

        // Print entry
        Serial.print("  ");
        Serial.print((char)entryType); // 'D' or 'F'
        Serial.print("    | ");
        if (entryType == 'F') {
            char sizeBuf[11];
            sprintf(sizeBuf, "%10lu", entrySize); // Format size right-aligned
            Serial.print(sizeBuf);
        } else {
            Serial.print("         -"); // Placeholder for directory size
        }
        Serial.print(" | ");
        Serial.println(entryName);

        // The ATtiny automatically prepares for the next entry type request
        // or sends 0xFF if done. The loop continues.
    }
     Serial.println("  ----------------------------");
     Wire.endTransmission();
}

// --- Function to Set Time ('C') ---
void setBridgeTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
  Serial.print("\n--- Setting Bridge Time ('C'): ");
  Serial.printf("%04d-%02d-%02d %02d:%02d:%02d ---\n", year, month, day, hour, minute, second);

  Wire.beginTransmission(I2C_SDBRIDGE_ADDR);
  Wire.write('C'); // Clock Set command

  // Bridge expects YY, MM, DD, HH, MM, SS
  uint8_t yy = year % 100; // Get last two digits of year

  Wire.write(yy);
  Wire.write(month);
  Wire.write(day);
  Wire.write(hour);
  Wire.write(minute);
  Wire.write(second);

  uint8_t error = Wire.endTransmission(true); // Send STOP

  if (error == 0) {
    Serial.println("  Time sent successfully.");
  } else {
    Serial.print("  [Error] Failed to send time. I2C Error: ");
    Serial.println(error);
  }
}


// --- Arduino Setup ---
void setup() {
  Serial.begin(115200);
  delay(6000); // Wait for Serial connection
  Serial.println("\n\nI2C SD Card Bridge Demo");
  Serial.print("Bridge Address: 0x"); Serial.println(I2C_SDBRIDGE_ADDR, HEX);

  Wire.begin(); // Join I2C bus as master

  // --- Test Sequence ---

  // 1. Basic Info
  queryCardType();
  getVolumeInfo();

  // 2. Directory Operations
  const char* testDir = "/TESTDIR";
  Serial.println();
  checkExists(testDir, 1); // Check if exists initially
  makeDirectory(testDir);     // Create it
  checkExists(testDir, 1); // Check again
  listDirectory("/");         // List root to see the new directory
  removeDirectory(testDir);   // Remove it
  checkExists(testDir, 1); // Check again

  // 3. File Operations
  const char* testFile = "/TEST.TXT";
  const char* fileContent1 = "Hello from ESP8266! Line 1.";
  const char* fileContent2 = "\nAppending Line 2.";
  const char* fileContent3 = "Hello from ESP8266! Line 1. \n12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890";
  Serial.println();
  checkExists(testFile, 0); // Check if exists initially
  writeFile(testFile, fileContent1, 'W'); // Write (create/overwrite)
  checkExists(testFile, 0); // Check again
  readFileContent(testFile);    // Read content
  writeFile(testFile, fileContent2, 'A'); // Append
  readFileContent(testFile);    // Read appended content
  removeFile(testFile);         // Remove it
  checkExists(testFile, 0); // Check again
  Serial.print("\nWriting lots of data now..");
  writeFile(testFile, fileContent3, 'W'); // Write (create/overwrite)
  checkExists(testFile, 0); // Check again
   Serial.print("\nReading lots of data now..");
  readFileContent(testFile);    // Read content
   Serial.print("\n--file end--");

  // 4. Nested Operations & Time Setting
  const char* nestedDir = "/NEST/SUB"; // Bridge should handle creating intermediate dirs if needed by SD lib
  const char* nestedFile = "/NEST/SUB/NESTFILE.TXT";
  const char* nestedContent = "Data in a nested directory.";

  Serial.println();
  setBridgeTime(2024, 7, 26, 10, 30, 00); // Set a specific time
  delay(100); // Give bridge time to process time set

  makeDirectory("/NEST");      // Create parent first
  makeDirectory(nestedDir);    // Create nested dir
  checkExists(nestedDir, 1);// Verify nested dir exists
  writeFile(nestedFile, nestedContent, 'W'); // Write file inside
  checkExists(nestedFile, 0); // Verify file exists
  listDirectory(nestedDir);     // List nested directory
  readFileContent(nestedFile);  // Read the nested file
  removeFile(nestedFile);       // Clean up file
  removeDirectory(nestedDir);   // Clean up nested dir
  removeDirectory("/NEST");     // Clean up parent dir
  checkExists("/NEST", 1);   // Verify parent dir removed

  Serial.println("\n--- Demo Finished ---");
}

// --- Arduino Loop ---
void loop() {
  // Nothing to do here, demo runs once in setup
  delay(10000);
}
