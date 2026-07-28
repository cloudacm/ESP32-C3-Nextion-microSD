/*
  Header Comments
  
*/

/*
Sources:
https://randomnerdtutorials.com/esp32-cyd-display-touchscreen-microsd-card/
https://medium.com/@androidcrypto/how-to-use-touch-and-sd-card-at-the-same-time-on-an-esp32-cheap-yellow-display-cyd-45fa55d01ffe
https://github.com/AndroidCrypto/ESP32_CYD_Display_with_Touch_and_SD_Card
https://github.com/dazzor/photoframe/blob/main/CYDusingLovyanGFX
*/

#include <WiFi.h>
#include <esp_wifi.h>
#include <PubSubClient.h>
#include <math.h>
#include <vector>
#include <ESPping.h>
#include <HTTPClient.h>
#include <SPI.h>
#include "FS.h"
#include "SD.h"

// SD Reader pins (default VSPI pins)
#define SD_MISO  3
#define SD_MOSI  4
#define SD_SCK  2
#define SD_CS  5
SPIClass Sd_spi = SPIClass(SPI);
const char* FileName = "/ScanLog.txt";
#define LogData(FileContent) appendFile(SD, FileName, FileContent)

HTTPClient http;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

String DeviceName = "NextionScanner";
String BuildVersion = "20260718";

const char* host = "192.168.0.101";  // Test server IP, change with your server IP!
const int port = 80;              // Test server port
const char* path = "/8M_DataFile.tmp";         // Resource to download
int totalBytes = 0;
uint8_t buffer[1460];
String PublishHost = host;
String PublishPath = path;
String EpochTime = "";

// WiFi credentials 
struct WiFiCredentials {
  const char* myssid;
  const char* mypassword;
};

WiFiCredentials wifi_networks[] = {
  {"wifi1", "password1"},
  {"wifi2", "password2"},
  {"openwifi1", ""},
};

const char* mqtt_server = "192.168.0.101";
int mqtt_port = 1883; // MQTT Broker port 
int mqtt_status = 0;
int mqtt_misCount = 0;
int wifi_status = 0;
int wifi_misCount = 0;
int system_pause = 0;

const uint32_t scanDelay = 60000;         // Delay between scans (ms)
const uint32_t throughputDelay = 600000;         // Delay between scans (ms)
const uint32_t scanPeriod = scanDelay / 1000; // Scan cycle (sec)
int scanCount = 0;
unsigned long previousScan = 0; 
unsigned long currentScan = 0; 
int currentThroughput = 0; 
int checkThroughput = 0;  // Set this to "1" to have bandwidth checks enabled at startup
int checkPublicIPAddress = 1;  // Set this to "1" to check public IP address through api.ipify.org.

int pingloop = 0;
const int numPings = 4; // Total number of pings to be sent
float latencies[numPings]; // Array to store latency readings
float maxLat = 0;
float minLat = 0;
float sum = 0;
float average = 0;
float variance = 0;
float stdDeviation;

float CPUTemperatureC = 0; // It steps by around .43 degrees C

// Rudimentary calculation of process utilization on an ESP32
float StartTime = 0;
float IdleStartTime = 0;
float IdleEndTime = 0;
float IdleDurationTime = 0;
float EndTime = 0; 
float TotalDurationTime = 0; 
float cpuUtilization = 0; 
int StartTimeCounter = 1;

// Global variable to accumulate airtime (in microseconds)
volatile unsigned long totalAirtimeMicroseconds = 0;

// Structure to store AP data from a scan
struct APData {
  String ssid;
  int rssi;
  uint8_t encryption;
  int channel;
  String bssid;
};

// Function Prototypes
String encryptionTypeStr(uint8_t authmode);
void mqttCallback(char* topic, byte* message, unsigned int length);
bool tryReconnectMQTT(unsigned long timeout);
void safePublish(const char* topic, const String & payload);
void updateStats(const std::vector<APData>& apList);
void wifiConnect();
void checkForNetworks();
void signalNoiseRatio(const std::vector<APData>& apList);
void channelAirtimeUtilization();
void airtime_callback(void *buf, wifi_promiscuous_pkt_type_t type);
float getChannelAirtimeUtilization(unsigned long measurementPeriodMs);

// Returns a human-readable string for the WiFi encryption type.
String encryptionTypeStr(uint8_t authmode) {
  switch (authmode) {
    case WIFI_AUTH_OPEN:            return "Open";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:     return "WPA+WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:  return "WPA2-EAP";
    case WIFI_AUTH_WPA3_PSK:         return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:    return "WPA2+WPA3";
    case WIFI_AUTH_WAPI_PSK:         return "WAPI";
    default:                        return "Unknown";
  }
}

void writeFile(fs::FS &fs, const char * path, const char * message){

  File file = fs.open(path, FILE_WRITE);
  if(!file){
    return;
  }
  if(file.print(message)){
  } else {
  }
  file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message){

  File file = fs.open(path, FILE_APPEND);
  if(!file){
    return;
  }
  if(file.print(message)){
  } else {
  }
  file.close();
}

// Callback for incoming MQTT messages.
void mqttCallback(char* topic, byte* message, unsigned int length) {
  String messageTemp;
  for (unsigned int i = 0; i < length; i++) {
    messageTemp += (char)message[i];
  }
  if (String(topic) == String(DeviceName + "/Command")) {
    if(messageTemp == "Reboot System"){              
      mqttClient.publish(String(DeviceName + "/Response").c_str(), "Rebooting in 5 seconds");
      Serial.println("Rebooting in 5 seconds");
      delay (5000);
      esp_restart();
    }
  }
  if (String(topic) == String(DeviceName + "/Command")) {
    if(messageTemp == "Check Throughput"){              
      mqttClient.publish(String(DeviceName + "/Response").c_str(), "Checking Throughput");
      Serial.println("Checking Throughput");
      checkThroughput = 1;
    }
  }
  if (String(topic) == String(DeviceName + "/Command")) {
    if(messageTemp == "Skip Throughput"){              
      mqttClient.publish(String(DeviceName + "/Response").c_str(), "Not Checking Throughput");
      Serial.println("Not Checking Throughput");
      checkThroughput = 0;
    }
  }
  if (String(topic) == String(DeviceName + "/Command")) {
    if(messageTemp == "Pause System"){              
      mqttClient.publish(String(DeviceName + "/Response").c_str(), "Pausing System");
      Serial.println("Pausing System");
      system_pause = 1;
    }
  }
  if (String(topic) == String(DeviceName + "/Command")) {
    if(messageTemp == "Resume System"){              
      mqttClient.publish(String(DeviceName + "/Response").c_str(), "Resuming System");
      Serial.println("Resuming System");
      system_pause = 0;
    }
  }
  if (String(topic) == String(DeviceName + "/Command")) {
    if(messageTemp == "Check Public IP"){              
      mqttClient.publish(String(DeviceName + "/Response").c_str(), "Checking Public IP");
      Serial.println("Checking Public IP");
      checkPublicIPAddress = 1;
    }
  }
  if (String(topic) == String(DeviceName + "/CurrentTime")) {
      EpochTime = messageTemp;
      Serial.print("\xFF\xFF\xFF");
      Serial.print("t6.txt=\"");
      Serial.print("Current Epoch Time: ");
      Serial.print(EpochTime);
      Serial.print("\"");
      Serial.println("\xFF\xFF\xFF");  
      LogData("Current Epoch Time: ");
      LogData(String(EpochTime).c_str()); 
      LogData(" \n");
      LogData("Current Uptime: "); 
      LogData(String(millis()).c_str()); 
      LogData(" \n");
  }
}

// Try to reconnect to the MQTT server for the specified timeout (in ms).
// Returns true if connection was re-established, false otherwise.
bool tryReconnectMQTT(unsigned long timeout) {
  unsigned long MQTTstartTime = millis();
  while (!mqttClient.connected() && (millis() - MQTTstartTime < timeout)) {
    if (mqttClient.connect(String(DeviceName).c_str())) {
      mqttClient.subscribe(String(DeviceName + "/#").c_str());
      return true;
    }
    delay(250);
  }
  return mqttClient.connected();
}

// Publish to MQTT if connected. If not, print the message to Serial.
void safePublish(const char* topic, const String & payload) {
  if (mqttClient.connected()) {
    mqtt_status = 1;
    mqttClient.publish(topic, payload.c_str());
  } else {
    mqtt_status = 0;
  }
}

// Publish system stats and each AP's info using the cached scan data.
void updateStats(const std::vector<APData>& apList) {
  delay(250);
  
  safePublish(String(DeviceName + "/Header").c_str(), "-----------------( Device Info )---------------");
  Serial.println("-----------------( Device Info )---------------");

  if (wifi_status == 1) {
      Serial.print("\xFF\xFF\xFF");
      Serial.print("t16.txt=\"");
      Serial.print("WiFi Status: ");
      Serial.print("Connected");
      Serial.print("\"");
      Serial.println("\xFF\xFF\xFF"); 
      LogData("WiFi Status: "); 
      LogData("Connected"); 
      LogData(" \n");
  }
  else {
      wifi_misCount = wifi_misCount + 1;
      Serial.print("\xFF\xFF\xFF");
      Serial.print("t16.txt=\"");
      Serial.print("WiFi Status: ");
      Serial.print("Disconnected");
      Serial.print("\"");
      Serial.println("\xFF\xFF\xFF"); 
      LogData("WiFi Status: "); 
      LogData("Disconnected"); 
      LogData(" \n");
  }

  if (mqtt_status == 1) {
      Serial.print("\xFF\xFF\xFF");
      Serial.print("t8.txt=\"");
      Serial.print("MQTT Status: ");
      Serial.print("Online");
      Serial.print("\"");
      Serial.println("\xFF\xFF\xFF");  
      LogData("MQTT Status: "); 
      LogData("Online"); 
      LogData(" \n");
  }
  else {
      mqtt_misCount = mqtt_misCount + 1;
      Serial.print("\xFF\xFF\xFF");
      Serial.print("t8.txt=\"");
      Serial.print("MQTT Status: ");
      Serial.print("Offline");
      Serial.print("\"");
      Serial.println("\xFF\xFF\xFF");  
      LogData("MQTT Status: "); 
      LogData("Offline"); 
      LogData(" \n");
  }
  
  mqttClient.publish(String(DeviceName + "/WiFiMissCount").c_str(), String(wifi_misCount).c_str());
  Serial.print("\xFF\xFF\xFF");
  Serial.print("t17.txt=\"");
  Serial.print("WiFi Miss Count: ");
  Serial.print(wifi_misCount);
  Serial.print("\"");
  Serial.println("\xFF\xFF\xFF");  
  LogData("WiFi Miss Count: "); 
  LogData(String(wifi_misCount).c_str()); 
  LogData(" \n");
  
  mqttClient.publish(String(DeviceName + "/MQTTMissCount").c_str(), String(mqtt_misCount).c_str());
  Serial.print("\xFF\xFF\xFF");
  Serial.print("t9.txt=\"");
  Serial.print("MQTT Miss Count: ");
  Serial.print(mqtt_misCount);
  Serial.print("\"");
  Serial.println("\xFF\xFF\xFF");  
  LogData("MQTT Miss Count: "); 
  LogData(String(mqtt_misCount).c_str()); 
  LogData(" \n");

  scanCount = scanCount + 1;
  mqttClient.publish(String(DeviceName + "/ScanCount").c_str(), String(scanCount).c_str());
  Serial.print("ScanCount: ");
  Serial.println(scanCount);
  
  safePublish(String(DeviceName + "/ScanPeriod").c_str(), String(scanPeriod));
  Serial.print("ScanPeriod: ");
  Serial.println(scanPeriod);
  
  safePublish(String(DeviceName + "/Uptime").c_str(), String(millis()));
  Serial.print("\xFF\xFF\xFF");
  Serial.print("t7.txt=\"");
  Serial.print("Uptime: ");
  Serial.print(millis());
  Serial.print("\"");
  Serial.println("\xFF\xFF\xFF");  
  LogData("Uptime: "); 
  LogData(String(millis()).c_str()); 
  LogData(" \n");

  safePublish(String(DeviceName + "/CpuTemperatureC").c_str(), String(temperatureRead()));
  Serial.print("\xFF\xFF\xFF");
  Serial.print("t3.txt=\"");
  Serial.print("CpuTemperatureC: ");
  Serial.print(temperatureRead());
  Serial.print("\"");
  Serial.println("\xFF\xFF\xFF");  
  LogData("CpuTemperatureC: "); 
  LogData(String(temperatureRead()).c_str()); 
  LogData(" \n");
  
  safePublish(String(DeviceName + "/FreeHeapSize").c_str(), String(ESP.getFreeHeap()));
  Serial.print("\xFF\xFF\xFF");
  Serial.print("t4.txt=\"");
  Serial.print("FreeHeapSize: ");
  Serial.print(ESP.getFreeHeap());
  Serial.print("\"");
  Serial.println("\xFF\xFF\xFF");  
  LogData("FreeHeapSize: "); 
  LogData(String(ESP.getFreeHeap()).c_str()); 
  LogData(" \n");
  
  safePublish(String(DeviceName + "/MaxHeapSize").c_str(), String(ESP.getMaxAllocHeap()));
  Serial.print("\xFF\xFF\xFF");
  Serial.print("t5.txt=\"");
  Serial.print("MaxHeapSize: ");
  Serial.print(ESP.getMaxAllocHeap());
  Serial.print("\"");
  Serial.println("\xFF\xFF\xFF");  
  LogData("MaxHeapSize: "); 
  LogData(String(ESP.getMaxAllocHeap()).c_str()); 
  LogData(" \n");
  
  safePublish(String(DeviceName + "/Firmware").c_str(), String(DeviceName + " Build " + BuildVersion).c_str());
  Serial.println("Firmware: " + DeviceName + " Build " + BuildVersion);
    
  safePublish(String(DeviceName + "/HWAddress").c_str(), WiFi.macAddress());
  Serial.print("\xFF\xFF\xFF");
  Serial.print("t1.txt=\"");
  Serial.print("HWAddress: ");
  Serial.print(WiFi.macAddress());
  Serial.print("\"");
  Serial.println("\xFF\xFF\xFF");  
  LogData("HWAddress: "); 
  LogData(String(WiFi.macAddress()).c_str()); 
  LogData(" \n");

  Serial.println("Windows Client Subscription: mosquitto_sub -h " + String(mqtt_server) + " -p " + String(mqtt_port) + " -v -t '" + String(DeviceName) + "/#'");
  Serial.println("Linux Client Subscription: mosquitto_sub -h "+ String(mqtt_server) + " -p " + String(mqtt_port) + " -v -t '" + String(DeviceName) + "/#' -F '%I %t %p'");
  
  Serial.println("Global Client Commands: mosquitto_pub -h " + String(mqtt_server) + " -p " + String(mqtt_port) + " -t '" + String(DeviceName) + "/Command' -m 'Checking Public IP'");
  Serial.println("Global Client Commands: mosquitto_pub -h " + String(mqtt_server) + " -p " + String(mqtt_port) + " -t '" + String(DeviceName) + "/Command' -m 'Check Throughput'");
  Serial.println("Global Client Commands: mosquitto_pub -h " + String(mqtt_server) + " -p " + String(mqtt_port) + " -t '" + String(DeviceName) + "/Command' -m 'Skip Throughput'");
  Serial.println("Global Client Commands: mosquitto_pub -h " + String(mqtt_server) + " -p " + String(mqtt_port) + " -t '" + String(DeviceName) + "/Command' -m 'Pause System'");
  Serial.println("Global Client Commands: mosquitto_pub -h " + String(mqtt_server) + " -p " + String(mqtt_port) + " -t '" + String(DeviceName) + "/Command' -m 'Resume System'");
  Serial.println("Global Client Commands: mosquitto_pub -h " + String(mqtt_server) + " -p " + String(mqtt_port) + " -t '" + String(DeviceName) + "/Command' -m 'Reboot System'");
  
  safePublish(String(DeviceName + "/Header").c_str(), "----------------( AP Scan Info )--------------");
  Serial.println("----------------( AP Scan Info )--------------");
  
  safePublish(String(DeviceName + "/Networks-Found").c_str(), String(apList.size()));
  Serial.print("\xFF\xFF\xFF");
  Serial.print("t25.txt=\"");
  Serial.print("Networks-Found: ");
  Serial.print(apList.size());
  Serial.print("\"");
  Serial.println("\xFF\xFF\xFF"); 
  LogData("Networks-Found: "); 
  LogData(String(apList.size()).c_str()); 
  LogData(" \n");

  for (size_t i = 0; i < apList.size(); i++) {
    String displaySSID = (apList[i].ssid != "") ? apList[i].ssid : "*-Hidden-*";
    String encryption = encryptionTypeStr(apList[i].encryption);
    String apInfo = String(i + 1) + " " + String(apList[i].rssi) + " " +
                    apList[i].bssid + " " + String(apList[i].channel) + " " +
                    displaySSID + " " + encryption;
    safePublish(String(DeviceName + "/AP-Found").c_str(), apInfo);
    Serial.print("AP-Found: ");
    Serial.println(apInfo);
    LogData("AP-Found: "); 
    LogData(String(apInfo).c_str()); 
    LogData(" \n");
    delay(250);
  }
}

// Connect to the WiFi network and configure the MQTT client.
void wifiConnect() {
  
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  if (n == 0) {
    wifi_status = 0;
    return;
  }
  for (int i = 0; i < sizeof(wifi_networks) / sizeof(wifi_networks[0]); i++) {
    for (int j = 0; j < n; j++) {
      if (strcmp(wifi_networks[i].myssid, WiFi.SSID(j).c_str()) == 0) {
        WiFi.begin(wifi_networks[i].myssid, wifi_networks[i].mypassword);
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 25) {
          delay(200);
          attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
          wifi_status = 1;
        } else {
          wifi_status = 0;
        }
      }
    }
  }
  
  mqttClient.setServer(mqtt_server, mqtt_port); 
  mqttClient.setCallback(mqttCallback);
}

void checkForThroughput() {

totalBytes = 0;

if (espClient.connect(host, port)) {
  espClient.print(String("GET ") + path + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");

  while (espClient.connected() && !espClient.available()) {
    delay(10);
  }
  
  while (espClient.available() && espClient.read() != '\n') {
    // Something has to be here
  }
    
  unsigned long startiPerfTime = millis();

  while (espClient.connected() || espClient.available()) {
    int bytesAvailable = espClient.available();
    int len = espClient.readBytes(buffer, min(bytesAvailable, 1460));
    totalBytes += len;
  }

  espClient.stop();
  
  unsigned long endiPerfTime = millis();
  unsigned long iPerfduration = (endiPerfTime - startiPerfTime) / 1000;  // seconds
  
  if (!mqttClient.connected()) {
    if (!tryReconnectMQTT(5000)) {
    }
  }
  mqttClient.loop();
  
  safePublish(String(DeviceName + "/Header").c_str(), "--------------( Throughput Info )-------------");
  Serial.println("--------------( Throughput Info )-------------");
  safePublish(String(DeviceName + "/HTTPHost").c_str(), String("http://" + PublishHost + PublishPath).c_str());
  Serial.println("HTTPHost: http://" + PublishHost + PublishPath);

  if (iPerfduration == 0) iPerfduration = 1;
    // float speedMbitps = (totalBytes * 8.0 / (1000.0 * 1000.0)) / iPerfduration;
    float speedMbitps = (totalBytes * 8.0 / 100000) / iPerfduration;  // modified for discrpencies (use 100000 instead of (1000.0 * 1000.0))
    safePublish(String(DeviceName + "/ThroughputMbps").c_str(), String(speedMbitps, 2));
    Serial.print("Throughput Mbps: ");
    Serial.println(speedMbitps, 2);
    safePublish(String(DeviceName + "/ThroughputHost").c_str(), String("Online"));
    Serial.println("Throughput Host: Online");
  }
  else {
    safePublish(String(DeviceName + "/ThroughputMbps").c_str(), String("0"));
    Serial.println("Throughput Mbps: 0");
    safePublish(String(DeviceName + "/ThroughputHost").c_str(), String("Offline"));
    Serial.println("Throughput Host: Offline");
  }

}

void checkForPublicIPAddress() {
  
    // http.begin("https://api.ipify.org/?format=json"); //Specify the URL
    http.begin("https://api.ipify.org/"); //Specify the URL
    int httpCode = http.GET();                        //Make the request

    if (httpCode > 0) { //Check for the returning code

      String payload = http.getString();
      
      Serial.print("\xFF\xFF\xFF");
      Serial.print("t15.txt=\"");
      Serial.print("PublicIPAddress: ");
      Serial.print(payload);
      Serial.print("\"");
      Serial.println("\xFF\xFF\xFF"); 
      LogData("PublicIPAddress: "); 
      LogData(String(payload).c_str()); 
      LogData(" \n");
      safePublish(String(DeviceName + "/PublicIPAddress").c_str(), String(payload));
    }

    http.end(); //Free the resources
  
}

// Calculate the signal-to-noise ratio (SNR) using cached scan results.
void signalNoiseRatio(const std::vector<APData>& apList) {
  if (WiFi.status() == WL_CONNECTED) {
    float rssiConnected = WiFi.RSSI();
    int connectedChannel = WiFi.channel();
    String connectedBSSID = WiFi.BSSIDstr();
    String connectedSSID = WiFi.SSID(); // Get the SSID of the connected AP
    
    float totalNoise_mW = 0.0;
    std::vector<String> uniqueBSSIDs;
    
      
 for (size_t i = 0; i < apList.size(); i++) {
  
    // Skip if the BSSID matches the connected ones
    if (apList[i].bssid == connectedBSSID) {
        continue; 
    }

    // Extract parts of the BSSID
    String bssid = apList[i].bssid;
    String connected = connectedBSSID;

    // Split into components
    String bssidParts[6];
    String connectedParts[6];

    bssid.replace(":", " "); // Replace ':' with space
    connected.replace(":", " "); // Replace ':' with space

    // Parse the BSSID and connected BSSID
    int j = 0;
    char* token = strtok((char*)bssid.c_str(), " ");
    while (token != NULL && j < 6) {
        bssidParts[j++] = String(token);
        token = strtok(NULL, " ");
    }

    j = 0;
    token = strtok((char*)connected.c_str(), " ");
    while (token != NULL && j < 6) {
        connectedParts[j++] = String(token);
        token = strtok(NULL, " ");
    }
          
    if (bssidParts[0] == connectedParts[0] &&
        bssidParts[1] == connectedParts[1] &&
        bssidParts[2] == connectedParts[2] &&
        bssidParts[3] != connectedParts[3] && // The 4th Hex Pair of the Mac Address should be skipped in the match logic
        bssidParts[4] == connectedParts[4] &&
        bssidParts[5] == connectedParts[5]) {
         
            continue; // Skip if the BSSID matches the exclusion pattern
    }

      int diff = abs(apList[i].channel - connectedChannel);
      float weight = 0.0;
      if (diff == 0) {
        weight = 1.0;
      } else if (diff == 1) {
        weight = 0.7;
      } else if (diff == 2) {
        weight = 0.3;
      } else {
        continue;
      }
      bool duplicate = false;
      for (size_t j = 0; j < uniqueBSSIDs.size(); j++) {
        if (uniqueBSSIDs[j] == apList[i].bssid) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        uniqueBSSIDs.push_back(apList[i].bssid);
        float mW = pow(10, apList[i].rssi / 10.0);
        totalNoise_mW += weight * mW;
      }
    }
    
    float noiseFloor_dBm = (uniqueBSSIDs.size() > 0) ? (10 * log10(totalNoise_mW)) : -95;
    float snr = rssiConnected - noiseFloor_dBm;
    snr = ( snr / ( noiseFloor_dBm * (-1) ) * 100 );
    if (snr <= 0 ) {
      snr = 0;
    }
    
    safePublish(String(DeviceName + "/Header").c_str(), "==============================================");
    Serial.println("==============================================");
    
    safePublish(String(DeviceName + "/Header").c_str(), "------------------( IP Info )-----------------");
    Serial.println("------------------( IP Info )-----------------");
    
    safePublish(String(DeviceName + "/IPAddress").c_str(), WiFi.localIP().toString());
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t11.txt=\"");
    Serial.print("IPAddress: ");
    Serial.print(WiFi.localIP().toString());
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF");  
    LogData("IPAddress: "); 
    LogData(String(WiFi.localIP().toString()).c_str()); 
    LogData(" \n");
    
    safePublish(String(DeviceName + "/Subnet").c_str(), WiFi.subnetMask().toString());
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t12.txt=\"");
    Serial.print("Subnet: ");
    Serial.print(WiFi.subnetMask().toString());
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF"); 
    LogData("Subnet: "); 
    LogData(String(WiFi.subnetMask().toString()).c_str()); 
    LogData(" \n");
    
    safePublish(String(DeviceName + "/DNS").c_str(), WiFi.dnsIP().toString());
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t13.txt=\"");
    Serial.print("DNS: ");
    Serial.print(WiFi.dnsIP().toString());
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF"); 
    LogData("DNS: "); 
    LogData(String(WiFi.dnsIP().toString()).c_str()); 
    LogData(" \n");
    
    safePublish(String(DeviceName + "/GWAddress").c_str(), WiFi.gatewayIP().toString());
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t14.txt=\"");
    Serial.print("GWAddress: ");
    Serial.print(WiFi.gatewayIP().toString());
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF"); 
    LogData("GWAddress: "); 
    LogData(String(WiFi.gatewayIP().toString()).c_str()); 
    LogData(" \n");


    // Ping GWAddress
    while (pingloop < numPings) {
        if (Ping.ping(WiFi.gatewayIP(), 1) > 0) {
            latencies[pingloop] = Ping.averageTime(); // Store the average time
            pingloop++;
            delay(1000); // Delay to avoid flooding with requests (1 second)
        }
    }

    // Calculate statistics
    maxLat = latencies[0];
    minLat = latencies[0];
    sum = 0.0;

    for (int i = 0; i < numPings; i++) {
        sum += latencies[i];
        if (latencies[i] > maxLat) {
            maxLat = latencies[i];
        }
        if (latencies[i] < minLat) {
            minLat = latencies[i];
        }
        safePublish(String(DeviceName + "/GWAddressPing" + String(i + 1)).c_str(), String(latencies[i]));
        Serial.print("GWAddress Ping " + String(i + 1) + ": ");
        Serial.println(latencies[i]);
        delay(10);
    }

    average = sum / numPings;

    // Calculate standard deviation
    variance = 0.0;
    for (int i = 0; i < numPings; i++) {
        variance += pow(latencies[i] - average, 2);
    }
    variance /= numPings;
    stdDeviation = sqrt(variance);

    // Output the results
    safePublish(String(DeviceName + "/GWAddressPingMax").c_str(), String(maxLat));
    Serial.print("GWAddress Max: ");
    Serial.println(maxLat);
    safePublish(String(DeviceName + "/GWAddressPingMin").c_str(), String(minLat));
    Serial.print("GWAddress Min: ");
    Serial.println(minLat);
    safePublish(String(DeviceName + "/GWAddressPingAve").c_str(), String(average));
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t26.txt=\"");
    Serial.print("GWAddress Ave: ");
    Serial.print(average);
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF"); 
    LogData("GWAddress Ave: "); 
    LogData(String(average).c_str()); 
    LogData(" \n");
    safePublish(String(DeviceName + "/GWAddressPingDev").c_str(), String(stdDeviation));
    Serial.print("GWAddress Dev: ");
    Serial.println(stdDeviation);
    
    pingloop = 0; // Reset counter

    // Ping Google
    const IPAddress Google_ip(8,8,8,8);
    while (pingloop < numPings) {
        if (Ping.ping(Google_ip, 1) > 0) {
            latencies[pingloop] = Ping.averageTime(); // Store the average time
            pingloop++;
            delay(1000); // Delay to avoid flooding with requests (1 second)
        }
    }

    // Calculate statistics
    maxLat = latencies[0];
    minLat = latencies[0];
    sum = 0.0;

    for (int i = 0; i < numPings; i++) {
        sum += latencies[i];
        if (latencies[i] > maxLat) {
            maxLat = latencies[i];
        }
        if (latencies[i] < minLat) {
            minLat = latencies[i];
        }
        safePublish(String(DeviceName + "/GooglePing" + String(i + 1)).c_str(), String(latencies[i]));
        Serial.print("Google (8.8.8.8) Ping " + String(i + 1) + ": ");
        Serial.println(latencies[i]);
        delay(10);
    }

    average = sum / numPings;

    // Calculate standard deviation
    variance = 0.0;
    for (int i = 0; i < numPings; i++) {
        variance += pow(latencies[i] - average, 2);
    }
    variance /= numPings;
    stdDeviation = sqrt(variance);

    // Output the results
    safePublish(String(DeviceName + "/GooglePingMax").c_str(), String(maxLat));
    Serial.print("Google (8.8.8.8) Max: ");
    Serial.println(maxLat);
    safePublish(String(DeviceName + "/GooglePingMin").c_str(), String(minLat));
    Serial.print("Google (8.8.8.8) Min: ");
    Serial.println(minLat);
    safePublish(String(DeviceName + "/GooglePingAve").c_str(), String(average));
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t27.txt=\"");
    Serial.print("Google (8.8.8.8) Ave: ");
    Serial.print(average);
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF"); 
    LogData("Google (8.8.8.8) Ave: "); 
    LogData(String(average).c_str()); 
    LogData(" \n");
    safePublish(String(DeviceName + "/GooglePingDev").c_str(), String(stdDeviation));
    Serial.print("Google (8.8.8.8) Dev: ");
    Serial.println(stdDeviation);
    
    pingloop = 0; // Reset counter

    // Ping Cloudflare
    const IPAddress Cloudflare_ip(1,1,1,1);
    while (pingloop < numPings) {
        if (Ping.ping(Cloudflare_ip, 1) > 0) {
            latencies[pingloop] = Ping.averageTime(); // Store the average time
            pingloop++;
            delay(1000); // Delay to avoid flooding with requests (1 second)
        }
    }

    // Calculate statistics
    maxLat = latencies[0];
    minLat = latencies[0];
    sum = 0.0;

    for (int i = 0; i < numPings; i++) {
        sum += latencies[i];
        if (latencies[i] > maxLat) {
            maxLat = latencies[i];
        }
        if (latencies[i] < minLat) {
            minLat = latencies[i];
        }
        safePublish(String(DeviceName + "/CloudflarePing" + String(i + 1)).c_str(), String(latencies[i]));
        Serial.print("Cloudflare (1.1.1.1) Ping " + String(i + 1) + ": ");
        Serial.println(latencies[i]);
        delay(10);
    }

    average = sum / numPings;

    // Calculate standard deviation
    variance = 0.0;
    for (int i = 0; i < numPings; i++) {
        variance += pow(latencies[i] - average, 2);
    }
    variance /= numPings;
    stdDeviation = sqrt(variance);

    // Output the results
    safePublish(String(DeviceName + "/CloudflarePingMax").c_str(), String(maxLat));
    Serial.print("Cloudflare (1.1.1.1) Max: ");
    Serial.println(maxLat);
    safePublish(String(DeviceName + "/CloudflarePingMin").c_str(), String(minLat));
    Serial.print("Cloudflare (1.1.1.1) Min: ");
    Serial.println(minLat);
    safePublish(String(DeviceName + "/CloudflarePingAve").c_str(), String(average));
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t28.txt=\"");
    Serial.print("Cloudflare (1.1.1.1) Ave: ");
    Serial.print(average);
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF"); 
    LogData("Cloudflare (1.1.1.1) Ave: "); 
    LogData(String(average).c_str()); 
    LogData(" \n");
    safePublish(String(DeviceName + "/CloudflarePingDev").c_str(), String(stdDeviation));
    Serial.print("Cloudflare (1.1.1.1) Dev: ");
    Serial.println(stdDeviation);
    
    pingloop = 0; // Reset counter
    
    // Ping Comcast
    const IPAddress Comcast_ip(75,75,75,75);
    while (pingloop < numPings) {
        if (Ping.ping(Comcast_ip, 1) > 0) {
            latencies[pingloop] = Ping.averageTime(); // Store the average time
            pingloop++;
            delay(1000); // Delay to avoid flooding with requests (1 second)
        }
    }

    // Calculate statistics
    maxLat = latencies[0];
    minLat = latencies[0];
    sum = 0.0;

    for (int i = 0; i < numPings; i++) {
        sum += latencies[i];
        if (latencies[i] > maxLat) {
            maxLat = latencies[i];
        }
        if (latencies[i] < minLat) {
            minLat = latencies[i];
        }
        safePublish(String(DeviceName + "/ComcastPing" + String(i + 1)).c_str(), String(latencies[i]));
        Serial.print("Comcast (75.75.75.75) Ping " + String(i + 1) + ": ");
        Serial.println(latencies[i]);
        delay(10);
    }

    average = sum / numPings;

    // Calculate standard deviation
    variance = 0.0;
    for (int i = 0; i < numPings; i++) {
        variance += pow(latencies[i] - average, 2);
    }
    variance /= numPings;
    stdDeviation = sqrt(variance);

    // Output the results
    safePublish(String(DeviceName + "/ComcastPingMax").c_str(), String(maxLat));
    Serial.print("Comcast (75.75.75.75) Max: ");
    Serial.println(maxLat);
    safePublish(String(DeviceName + "/ComcastPingMin").c_str(), String(minLat));
    Serial.print("Comcast (75.75.75.75) Min: ");
    Serial.println(minLat);
    safePublish(String(DeviceName + "/ComcastPingAve").c_str(), String(average));
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t29.txt=\"");
    Serial.print("Comcast (75.75.75.75) Ave: ");
    Serial.print(average);
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF"); 
    LogData("Comcast (75.75.75.75) Ave: "); 
    LogData(String(average).c_str()); 
    LogData(" \n");
    safePublish(String(DeviceName + "/ComcastPingDev").c_str(), String(stdDeviation));
    Serial.print("Comcast (75.75.75.75) Dev: ");
    Serial.println(stdDeviation);
    
    pingloop = 0; // Reset counter
    
    safePublish(String(DeviceName + "/MQTT").c_str(), String(mqtt_server));
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t10.txt=\"");
    Serial.print("MQTT: ");
    Serial.print(mqtt_server);
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF");  
    LogData("MQTT: "); 
    LogData(String(mqtt_server).c_str()); 
    LogData(" \n");

    safePublish(String(DeviceName + "/Header").c_str(), "-----------------( WiFi Info )----------------");
    Serial.println("-----------------( WiFi Info )----------------");
    
    safePublish(String(DeviceName + "/WifiNetwork").c_str(), WiFi.SSID());
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t18.txt=\"");
    Serial.print("WifiNetwork: ");
    Serial.print(WiFi.SSID());
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF"); 
    LogData("WifiNetwork: "); 
    LogData(String(WiFi.SSID()).c_str()); 
    LogData(" \n");
    
    safePublish(String(DeviceName + "/WifiAP").c_str(), WiFi.BSSIDstr());
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t19.txt=\"");
    Serial.print("WifiAP: ");
    Serial.print(WiFi.BSSIDstr());
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF"); 
    LogData("WifiAP: "); 
    LogData(String(WiFi.BSSIDstr()).c_str()); 
    LogData(" \n");
    
    safePublish(String(DeviceName + "/WifiChannel").c_str(), String(WiFi.channel()));
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t20.txt=\"");
    Serial.print("WifiChannel: ");
    Serial.print(WiFi.channel());
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF"); 
    LogData("WifiChannel: "); 
    LogData(String(WiFi.channel()).c_str()); 
    LogData(" \n");
    
    safePublish(String(DeviceName + "/WifiSignal").c_str(), String(WiFi.RSSI()));
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t21.txt=\"");
    Serial.print("WifiSignal dBm: ");
    Serial.print(WiFi.RSSI());
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF"); 
    LogData("WifiSignal dBm: "); 
    LogData(String(WiFi.RSSI()).c_str()); 
    LogData(" \n");
    
    safePublish(String(DeviceName + "/WifiNoiseFloor").c_str(), String(noiseFloor_dBm));
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t22.txt=\"");
    Serial.print("WifiNoiseFloor dBm: ");
    Serial.print(noiseFloor_dBm);
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF"); 
    LogData("WifiNoiseFloor dBm: "); 
    LogData(String(noiseFloor_dBm).c_str()); 
    LogData(" \n");
    
    safePublish(String(DeviceName + "/WifiSNR").c_str(), String(snr));
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t23.txt=\"");
    Serial.print("WifiSNR Percent: ");
    Serial.print(snr, 2);
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF"); 
    LogData("WifiSNR Percent: "); 
    LogData(String(snr, 2).c_str()); 
    LogData(" \n");
    
    channelAirtimeUtilization();
    
  }
}

// Promiscuous mode callback to calculate airtime per packet.
void airtime_callback(void *buf, wifi_promiscuous_pkt_type_t type) {
  wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *) buf;
  uint8_t rateCode = pkt->rx_ctrl.rate;
  float dataRateMbps = 11; // Default fallback
  switch(rateCode) {
    case 0: dataRateMbps = 1; break;
    case 1: dataRateMbps = 2; break;
    case 2: dataRateMbps = 5.5; break;
    case 3: dataRateMbps = 11; break;
    default: break;
  }
  float airtimePerByte = 8.0 / dataRateMbps;
  uint16_t len = pkt->rx_ctrl.sig_len;
  totalAirtimeMicroseconds += (unsigned long)(len * airtimePerByte);
}

// Measures channel airtime utilization as a percentage.
float getChannelAirtimeUtilization(unsigned long measurementPeriodMs) {
  totalAirtimeMicroseconds = 0;
  esp_wifi_set_promiscuous_rx_cb(airtime_callback);
  esp_wifi_set_promiscuous(true);
  delay(measurementPeriodMs);
  esp_wifi_set_promiscuous(false);
  // Re-enable station mode so WiFi can reconnect if needed.
  WiFi.mode(WIFI_STA);
  unsigned long totalMeasurementMicroseconds = measurementPeriodMs * 1000;
  float utilization = (totalAirtimeMicroseconds / (float)totalMeasurementMicroseconds) * 100.0;
  return (utilization > 100.0) ? 100.0 : utilization;
}

// Publishes the channel airtime utilization.
void channelAirtimeUtilization() {
  float utilizationPercent = getChannelAirtimeUtilization(2000);
  safePublish(String(DeviceName + "/RadioUse").c_str(), String(utilizationPercent, 1));
  Serial.print("\xFF\xFF\xFF");
  Serial.print("t24.txt=\"");
  Serial.print("RadioUse: ");
  Serial.print(utilizationPercent, 1);
  Serial.print("\"");
  Serial.println("\xFF\xFF\xFF"); 
  LogData("RadioUse: "); 
  LogData(String(utilizationPercent, 1).c_str()); 
  LogData(" \n");
}

// Perform a single WiFi scan and cache the results.
void checkForNetworks() {
  int numAP = WiFi.scanNetworks(false, true);
  std::vector<APData> apList;
  for (int i = 0; i < numAP; i++) {
    APData ap;
    ap.ssid = WiFi.SSID(i);
    ap.rssi = WiFi.RSSI(i);
    ap.encryption = WiFi.encryptionType(i);
    ap.channel = WiFi.channel(i);
    ap.bssid = WiFi.BSSIDstr(i);
    apList.push_back(ap);
  }
  
  delay(100);

  if (WiFi.status() != WL_CONNECTED) {
      wifiConnect();
    }
 
  if (!mqttClient.connected()) {
    if (!tryReconnectMQTT(5000)) {
    }
  }
  mqttClient.loop();
  
  // Use the same scan data for SNR and AP stats.
  signalNoiseRatio(apList);
  updateStats(apList);
}

void setup() {
  Serial.begin(9600);  
  delay(250); // Wait for the Serial Monitor to open
  Sd_spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, Sd_spi, 55000000)) {
    Serial.println("Card Mount Failed");
  }
  File file = SD.open(FileName);
  if(!file) {
    Serial.println("File doesn't exist");
    Serial.println("Creating file...");
    writeFile(SD, FileName, "File Created \n");
  }
  else {
    Serial.println("WifiScanLog.txt file already exists");  
  }
  Serial.print("\xFF\xFF\xFF");
  Serial.print("t0.txt=\"");
  Serial.print("ESP32 Nextion WifiScan - 2026 CloudACM");
  Serial.print("\"");
  Serial.println("\xFF\xFF\xFF");  
  LogData("ESP32 Nextion WifiScan \n");
  delay(250); // Wait 
  wifiConnect();
  delay(1000);
}

void loop() {

  if (StartTimeCounter == 1) {
    StartTime = millis();
    IdleStartTime = millis();
  StartTimeCounter = 0;
  }
  
  if (!mqttClient.connected()) {
    if (!tryReconnectMQTT(5000)) {
    }
  }
  mqttClient.loop();

  currentScan = millis();  
  if (currentScan - previousScan >= scanDelay && system_pause == 0) {
  
    IdleEndTime = millis();
    IdleDurationTime = IdleEndTime - IdleStartTime;
    
    previousScan = currentScan;
    
    safePublish(String(DeviceName + "/ReadyState").c_str(), "0");
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t31.txt=\"");
    Serial.print("Not ready for Commands");
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF");  
    LogData("Not ready for Commands \n");
    
    checkForNetworks();
    currentThroughput++;  
    
    if (currentThroughput >= 5 && checkThroughput == 1) {
      
      currentThroughput = 0;
      checkForThroughput();
      
    }  
    
    EndTime = millis();
    StartTimeCounter = 1;
    TotalDurationTime = EndTime - StartTime;
    cpuUtilization = ( IdleDurationTime / TotalDurationTime ) * 100;
    cpuUtilization = 100 - cpuUtilization;
    safePublish(String(DeviceName + "/CpuUsage").c_str(), String(cpuUtilization, 1));
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t2.txt=\"");
    Serial.print("CpuUsage: ");
    Serial.print(cpuUtilization, 1);
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF");  
    LogData("CpuUsage: "); 
    LogData(String(cpuUtilization, 1).c_str()); 
    LogData(" \n");
    
    if (checkPublicIPAddress == 1) {
      
      checkPublicIPAddress = 0;
      checkForPublicIPAddress();
      
    }  

    safePublish(String(DeviceName + "/ReadyState").c_str(), "1"); 
    Serial.print("\xFF\xFF\xFF");
    Serial.print("t30.txt=\"");
    Serial.print("Ready for Commands");
    Serial.print("\"");
    Serial.println("\xFF\xFF\xFF");  
    LogData("Ready for Commands \n");
    
  }
 
}
