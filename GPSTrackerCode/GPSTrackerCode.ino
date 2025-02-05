/*
   ESP32 GPS Tracker with Offline Data Storage and Wi-Fi Upload

   Features:
   - Connects to a GPS module via UART1 (RX:16, TX:17)
   - Reads GPS data (latitude, longitude, altitude, timestamp, satellites)
   - Connects to Wi-Fi and uploads GPS data to a cloud server
   - Stores GPS data locally when Wi-Fi is unavailable and uploads later
   - Uses LEDs to indicate GPS signal and Wi-Fi connection status
   - Automatically reconnects to Wi-Fi if disconnected

   Functions:
   - setup(): Initializes Wi-Fi, GPS module, and status LEDs
   - loop(): Continuously reads GPS data, processes it, and uploads at intervals
   - processGPSData(): Extracts GPS information from raw NMEA sentences
   - sendGPSData(): Sends formatted GPS data to the cloud server via HTTP POST
   - parseGPGGA(), parseGPRMC(): Extracts relevant GPS details from NMEA sentences
   - convertAndPrintLocalDateTime(): Converts UTC time to local format
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>  // Include vector library for storing offline GPS data

// WiFi credentials
const char* ssid = "xxxx";      // WiFi SSID - Replace your own
const char* password = "xxxx";  // WiFi Password - Replace your own

// Server details for sending GPS data
const char* serverUrl = "https://www.circuitdigest.cloud/geolinker";  // Server URL
const char* apiKey = "xxxxxxxxxxxx";                                  //12 character API key for authentication

// GPS module connection using UART1
HardwareSerial gpsSerial(1);
const int RXPin = 16;  // GPS TX connected to ESP32 RX
const int TXPin = 17;  // GPS RX connected to ESP32 TX

// Structure to hold detailed GPS data
struct GPSRawData {
  double latitude;
  char latitudeDir;
  double longitude;
  char longitudeDir;
  int satellites;
  double altitude;
  int hours, minutes, seconds;
  int day, month, year;
  String timestamp;  // Combined timestamp with date and time
};

// Structure to store simplified GPS data for transmission
struct GPSData {
  double latitude;
  double longitude;
  String timestamp;
};

// Upload interval and last upload timestamp
const unsigned long uploadInterval = 10000;  // Data sent every 10 seconds
unsigned long lastUploadTime = 0;

// LED indicators for network and GPS status
const int networkStatusLED = 18;  // Indicates WiFi status
const int gpsStatusLED = 19;      // Indicates GPS data validity

bool gpsDataValid = false;    // Flag to check if GPS data is valid
GPSData latestGPSData;        // Stores the latest valid GPS data
GPSRawData latestGPSRawData;  // Stores raw GPS data

// Buffer to store GPS data when offline
std::vector<GPSData> offlineData;

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600, SERIAL_8N1, RXPin, TXPin);  // Initialize GPS module

  // Initialize LEDs
  pinMode(networkStatusLED, OUTPUT);
  pinMode(gpsStatusLED, OUTPUT);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(networkStatusLED, LOW);  // Turn off LED when not connected
    delay(500);
    digitalWrite(networkStatusLED, HIGH);  // Blink LED while connecting
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  digitalWrite(networkStatusLED, HIGH);  // Turn on LED when connected
}

void loop() {
  static String gpsData = "";

  // Read and process GPS data from the module
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    gpsData += c;
    if (c == '\n') {  // When a full GPS sentence is received
      processGPSData(gpsData);
      gpsData = "";
    }
  }

  // Periodically send GPS data to the server
  if (millis() - lastUploadTime >= uploadInterval) {
    lastUploadTime = millis();

    if (gpsDataValid) {  // Only send if GPS data is valid
      if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(networkStatusLED, HIGH);  // Indicate WiFi is active

        // Send any stored offline data first
        bool offlineUploadSuccess = true;
        for (auto& data : offlineData) {
          if (!sendGPSData(data)) {  // Stop if sending fails
            offlineUploadSuccess = false;
            break;
          }
        }

        if (offlineUploadSuccess) {
          offlineData.clear();  // Clear offline buffer if all data is uploaded
        }

        // Send the latest GPS data
        if (!sendGPSData(latestGPSData)) {
          Serial.println("Failed to upload latest GPS data. Storing locally.");
          offlineData.push_back(latestGPSData);  // Store data for later upload
          digitalWrite(networkStatusLED, LOW);
        } else {
          digitalWrite(networkStatusLED, HIGH);
          Serial.println("Latest GPS data uploaded successfully.");
        }
      } else {
        // If WiFi is offline, store GPS data for later upload
        Serial.println("WiFi not connected. Storing data locally.");
        offlineData.push_back(latestGPSData);
        digitalWrite(networkStatusLED, LOW);
        WiFi.disconnect();
        WiFi.reconnect();
      }
    } else {
      Serial.println("Invalid GPS data. Skipping upload.");
    }
  }
}

// Function to send GPS data to the server
bool sendGPSData(GPSData data) {
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", apiKey);

  // Create JSON payload for GPS data
  String payload = R"({
    "timestamp": [
      ")" + String(data.timestamp)
                   + R"("],
    "lat": [
      )" + String(data.latitude, 6)
                   + R"(],
    "long": [
      )" + String(data.longitude, 6)
                   + R"(]
  })";

  int httpResponseCode = http.POST(payload);  // Send data via HTTP POST request

  Serial.print("Server response code: ");
  Serial.println(httpResponseCode);

  http.end();
  return (httpResponseCode == 200 || httpResponseCode == 201);  // Success if 200 or 201
}

// Function to process received GPS data
void processGPSData(String raw) {
  if (raw.startsWith("$GPGGA")) {  // GPGGA contains location and satellite data
    parseGPGGA(raw);
    convertAndPrintLocalDateTime();       // Convert UTC to local time
  } else if (raw.startsWith("$GPRMC")) {  // GPRMC contains date and speed info
    parseGPRMC(raw);
  }
}

// Function to parse GPGGA data and extract latitude, longitude, and altitude
void parseGPGGA(String gpgga) {
  String tokens[15];
  int tokenIndex = 0;

  // Split GPGGA string into individual tokens
  int startIndex = 0;
  for (int i = 0; i < gpgga.length(); i++) {
    if (gpgga[i] == ',' || gpgga[i] == '*') {
      tokens[tokenIndex++] = gpgga.substring(startIndex, i);
      startIndex = i + 1;
    }
  }

  // Extract meaningful GPS data
  if (tokenIndex > 1) {
    String utcTime = tokens[1];
    latestGPSRawData.hours = utcTime.substring(0, 2).toInt();
    latestGPSRawData.minutes = utcTime.substring(2, 4).toInt();
    latestGPSRawData.seconds = utcTime.substring(4, 6).toInt();

    latestGPSRawData.latitude = nmeaToDecimal(tokens[2]);
    latestGPSData.latitude = nmeaToDecimal(tokens[2]);
    latestGPSRawData.latitudeDir = tokens[3].charAt(0);
    latestGPSRawData.longitude = nmeaToDecimal(tokens[4]);
    latestGPSData.longitude = nmeaToDecimal(tokens[4]);
    latestGPSRawData.longitudeDir = tokens[5].charAt(0);

    latestGPSRawData.satellites = tokens[7].toInt();
    latestGPSRawData.altitude = tokens[9].toDouble();

    if (latestGPSRawData.satellites >= 4) {  // Ensure enough satellites are available
      if (latestGPSData.latitude != 0 || latestGPSData.longitude != 0) {
        gpsDataValid = true;
        digitalWrite(gpsStatusLED, HIGH);  // Indicate valid GPS data
      } else {
        gpsDataValid = false;
        digitalWrite(gpsStatusLED, LOW);
      }
    } else {
      gpsDataValid = false;
      digitalWrite(gpsStatusLED, LOW);
    }
  }
}

// Function to parse GPRMC data and extract the date
void parseGPRMC(String gprmc) {
  String tokens[15];
  int tokenIndex = 0;

  // Split GPRMC string into tokens
  int startIndex = 0;
  for (int i = 0; i < gprmc.length(); i++) {
    if (gprmc[i] == ',' || gprmc[i] == '*') {
      tokens[tokenIndex++] = gprmc.substring(startIndex, i);
      startIndex = i + 1;
    }
  }

  // Extract date information
  if (tokenIndex > 9) {
    String utcDate = tokens[9];
    latestGPSRawData.day = utcDate.substring(0, 2).toInt();
    latestGPSRawData.month = utcDate.substring(2, 4).toInt();
    latestGPSRawData.year = 2000 + utcDate.substring(4, 6).toInt();
  }
}

// Function to convert UTC time and date to local time and update the timestamp
void convertAndPrintLocalDateTime() {
  int offsetHours = 5;
  int offsetMinutes = 30;

  latestGPSRawData.minutes += offsetMinutes;
  latestGPSRawData.hours += offsetHours;

  if (latestGPSRawData.minutes >= 60) {
    latestGPSRawData.minutes -= 60;
    latestGPSRawData.hours++;
  }
  if (latestGPSRawData.hours >= 24) {
    latestGPSRawData.hours -= 24;
    latestGPSRawData.day++;
  }

  char timeBuffer[20];
  snprintf(timeBuffer, sizeof(timeBuffer), "%04d-%02d-%02d %02d:%02d:%02d",
           latestGPSRawData.year, latestGPSRawData.month, latestGPSRawData.day,
           latestGPSRawData.hours, latestGPSRawData.minutes, latestGPSRawData.seconds);
  latestGPSRawData.timestamp = String(timeBuffer);
  latestGPSData.timestamp = String(timeBuffer);

  Serial.print("TimeStamp: ");
  Serial.println(latestGPSRawData.timestamp);
}

// Function to determine the number of days in a given month
int daysInMonth(int month, int year) {
  if (month == 2) {
    // Check for leap year
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28;
  }
  return (month == 4 || month == 6 || month == 9 || month == 11) ? 30 : 31;
}


// Function to convert NMEA coordinates to decimal degrees
double nmeaToDecimal(String nmeaCoord) {
  if (nmeaCoord == "") return 0.0;

  double raw = nmeaCoord.toDouble();
  int degrees = int(raw / 100);
  double minutes = raw - (degrees * 100);
  return degrees + (minutes / 60.0);
}

// Function to print the latest GPS data
void printLatestGPSData() {
  Serial.println("");
  Serial.print("Latitude: ");
  Serial.println(latestGPSData.latitude, 6);

  Serial.print("Longitude: ");
  Serial.println(latestGPSData.longitude, 6);

  Serial.print("Timestamp: ");
  Serial.println(latestGPSData.timestamp);
  Serial.println("");
}
