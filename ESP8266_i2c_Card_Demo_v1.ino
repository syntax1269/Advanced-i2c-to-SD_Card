#include <Wire.h>

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
#define DIR_LIST      0x52
#define DIR_LIST_REC  0x53

// I2C address
#define I2C_ADDR 0x6E

class SDBridge {
private:
    bool sendCommand(uint8_t cmd) {
        Wire.beginTransmission(I2C_ADDR);
        Wire.write(cmd);
        return Wire.endTransmission() == 0;
    }
    
    bool sendString(const char* str) {
        Wire.beginTransmission(I2C_ADDR);
        Wire.write((uint8_t*)str, strlen(str));
        Wire.write(0); // Null terminator
        return Wire.endTransmission() == 0;
    }
    
    bool sendData(const char* data, size_t len) {
        const size_t MAX_CHUNK = 30;
        size_t sent = 0;
        
        while (sent < len) {
            size_t chunk = min(MAX_CHUNK, len - sent);
            Wire.beginTransmission(I2C_ADDR);
            Wire.write((uint8_t*)(data + sent), chunk);
            if (Wire.endTransmission() != 0) return false;
            sent += chunk;
        }
        
        return true;
    }
    
    // Update the waitReady method to handle the CMD_FILE_WRITE status
    // Update the waitReady method to handle command echoing
    bool waitReady(uint16_t timeout = 1000) {
        uint32_t start = millis();
        uint8_t retries = 0;
        uint8_t lastStatus = 0;
        
        while (millis() - start < timeout) {
            Wire.beginTransmission(I2C_ADDR);
            if (Wire.endTransmission() != 0) {
                Serial.println("Device not responding");
                delay(50);
                continue;
            }
            
            Wire.requestFrom((uint8_t)I2C_ADDR, (uint8_t)1);
            if (Wire.available()) {
                uint8_t status = Wire.read();
                Serial.print("Status response: 0x");
                Serial.println(status, HEX);
                
                if (status == STATUS_OK) return true;
                if (status == STATUS_ERROR) {
                    Serial.println("Device reported error");
                    return false;
                }
                
                // If the status is the same as the command we sent, the ATtiny is still processing
                // This is not an error, but we need to wait for a proper status response
                if (status == lastStatus) {
                    retries++;
                    if (retries >= 10) {
                        Serial.println("Command stuck in processing, sending reset");
                        // Send a reset command
                        Wire.beginTransmission(I2C_ADDR);
                        Wire.write(CMD_INIT);
                        Wire.endTransmission();
                        delay(500);
                        return false;
                    }
                } else {
                    lastStatus = status;
                    retries = 0;
                }
            }
            
            delay(100);
        }
        
        Serial.println("Timeout waiting for device");
        return false;
    }

public:
    bool begin() {
        Wire.begin();
        Wire.setClock(100000);
        return sendCommand(CMD_INIT); // Init command
    }
    
    bool createDirectory(const char* path) {
        Serial.print("Creating directory: ");
        Serial.println(path);
        
        if (!sendCommand(DIR_CREATE)) {
            Serial.println("Failed to send directory create command");
            return false;
        }
        
        if (!sendString(path)) {
            Serial.println("Failed to send directory path");
            return false;
        }
        
        // Wait longer for directory operations
        return waitReady(2000);
    }
    
    bool removeDirectory(const char* path) {
        if (!sendCommand(DIR_DELETE)) return false;
        if (!sendString(path)) return false;
        return waitReady();
    }
    
    // Simplify the createFile method
    bool createFile(const char* filename, const char* data) {
        Serial.print("Creating file: ");
        Serial.println(filename);
        
        // Send create file command
        if (!sendCommand(CMD_FILE_CREATE)) {
            Serial.println("Failed to send create file command");
            return false;
        }
        
        // Send filename
        if (!sendString(filename)) {
            Serial.println("Failed to send filename");
            return false;
        }
        
        // Wait for file to be created
        if (!waitReady(2000)) {
            Serial.println("Failed to create file");
            return false;
        }
        
        // Send file data
        Serial.print("Sending file data: ");
        Serial.println(data);
        
        // Send write command
        if (!sendCommand(CMD_FILE_WRITE)) {
            Serial.println("Failed to send write command");
            return false;
        }
        
        // Send data in one go
        if (!sendString(data)) {
            Serial.println("Failed to send data");
            return false;
        }
        
        // Wait for completion
        return waitReady(2000);
    }
    
    // Update the readFile method to add more debugging
    uint16_t readFile(const char* path, char* buffer, uint16_t maxLength) {
        Serial.print("Reading file: ");
        Serial.println(path);
        
        // Send read command
        Wire.beginTransmission(I2C_ADDR);
        Wire.write(CMD_READ);
        if (Wire.endTransmission() != 0) {
            Serial.println("Failed to send read command");
            return 0;
        }
        
        // Send path
        Wire.beginTransmission(I2C_ADDR);
        Wire.write((uint8_t*)path, strlen(path));
        Wire.write(0); // Null terminator
        if (Wire.endTransmission() != 0) {
            Serial.println("Failed to send path");
            return 0;
        }
        
        // Wait for file to be opened
        if (!waitReady(2000)) {
            Serial.println("Failed to open file for reading");
            return 0;
        }
        
        // Read data
        uint16_t bytesRead = 0;
        while (bytesRead < maxLength - 1) {
            Wire.requestFrom((uint8_t)I2C_ADDR, (uint8_t)1);
            if (!Wire.available()) {
                Serial.println("No data available");
                break;
            }
            
            uint8_t data = Wire.read();
            Serial.print("Read byte: 0x");
            Serial.println(data, HEX);
            
            if (data == STATUS_DONE) {
                Serial.println("End of file reached");
                break;
            }
            
            buffer[bytesRead++] = data;
            delay(10); // Small delay between reads
        }
        
        buffer[bytesRead] = 0;  // Null terminate
        return bytesRead;
    }
    
    bool deleteFile(const char* path) {
        if (!sendCommand(CMD_FILE_DELETE)) return false;
        if (!sendString(path)) return false;
        return waitReady();
    }
    
    bool fileExists(const char* path) {
        if (!sendCommand(CMD_FILE_EXISTS)) return false;
        if (!sendString(path)) return false;
        
        if (!waitReady()) return false;
        
        Wire.requestFrom((uint8_t)I2C_ADDR, (uint8_t)1);
        if (Wire.available()) {
            return Wire.read() == STATUS_OK;
        }
        return false;
    }
    
    bool isDirectory(const char* path) {
        if (!sendCommand(CMD_IS_DIRECTORY)) return false;
        if (!sendString(path)) return false;
        
        if (!waitReady()) return false;
        
        Wire.requestFrom((uint8_t)I2C_ADDR, (uint8_t)1);
        if (Wire.available()) {
            return Wire.read() == STATUS_OK;
        }
        return false;
    }
    
    void listDirectory(const char* path) {
        Serial.print("Requesting directory listing for: ");
        Serial.println(path);
        
        if (!sendCommand(DIR_LIST)) {
            Serial.println("Failed to send list command");
            return;
        }
        
        if (!sendString(path)) {
            Serial.println("Failed to send path");
            return;
        }
        
        if (!waitReady(2000)) {
            Serial.println("Directory listing failed");
            return;
        }
        
        char filename[64];
        int pos = 0;
        
        while (true) {
            Wire.requestFrom((uint8_t)I2C_ADDR, (uint8_t)1);
            if (!Wire.available()) break;
            
            uint8_t c = Wire.read();
            if (c == STATUS_DONE) break;
            
            if (c == 0) {
                filename[pos] = 0;
                Serial.println(filename);
                pos = 0;
            } else {
                if (pos < sizeof(filename) - 1) {
                    filename[pos++] = c;
                }
            }
            
            // Add a small delay between requests
            delay(5);
        }
    }
};

SDBridge sdCard;

// Update the setup function to focus on basic SD card operations
void setup() {
    Serial.begin(115200);
    delay(4000); // Give time for serial port to connect
    Serial.println(""); // indicates ESP8266 startup
    Serial.println("ESP8266 Started");
    
    // Initialize I2C
    Wire.begin();
    Wire.setClock(100000);
    
    // Check if ATtiny is responding
    Serial.println("Scanning I2C bus...");
    Wire.beginTransmission(I2C_ADDR);
    if (Wire.endTransmission() == 0) {
        Serial.print("ATtiny found at address 0x");
        Serial.println(I2C_ADDR, HEX);
    } else {
        Serial.println("ATtiny not found - check connections");
        return;
    }
    
    // Initialize SD card
    Serial.println("Initializing SD card...");
    if (!sdCard.begin()) {
        Serial.println("Failed to initialize SD card");
        return;
    }
    
    Serial.println("SD card initialized successfully!");
    delay(1000); // Give SD card time to stabilize
    
    // Try a very simple file operation
    Serial.println("Trying to create a simple file...");
    const char* testData = "Test";
    
    if (sdCard.createFile("test.txt", testData)) {
        Serial.println("File created successfully!");
    } else {
        Serial.println("Failed to create file");
    }
}

void loop() {
    // Nothing to do in loop
    delay(1000);
}