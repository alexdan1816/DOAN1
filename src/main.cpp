/*
 
  ________  ________          ________  ___  ___  ________  _______   ________          _____ ______   ___  ________   ___     
 |\   ____\|\_____  \        |\   ____\|\  \|\  \|\   __  \|\  ___ \ |\   __  \        |\   _ \  _   \|\  \|\   ___  \|\  \    
 \ \  \___|\|____|\ /_       \ \  \___|\ \  \\\  \ \  \|\  \ \   __/|\ \  \|\  \       \ \  \\\__\ \  \ \  \ \  \\ \  \ \  \   
  \ \_____  \    \|\  \       \ \_____  \ \  \\\  \ \   ____\ \  \_|/_\ \   _  _\       \ \  \\|__| \  \ \  \ \  \\ \  \ \  \  
   \|____|\  \  __\_\  \       \|____|\  \ \  \\\  \ \  \___|\ \  \_|\ \ \  \\  \|       \ \  \    \ \  \ \  \ \  \\ \  \ \  \ 
     ____\_\  \|\_______\        ____\_\  \ \_______\ \__\    \ \_______\ \__\\ _\        \ \__\    \ \__\ \__\ \__\\ \__\ \__\
    |\_________\|_______|       |\_________\|_______|\|__|     \|_______|\|__|\|__|        \|__|     \|__|\|__|\|__| \|__|\|__|
    \|_________|                \|_________|                                                                                  
*/

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
#include <Wire.h>
#include "Protocentral_MAX30205.h"
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include "heartRate.h"
#undef BUFFER_SIZE
#undef FreqS
#include <WebSocketsClient.h>
#include <MQTTPubSubClient.h>

#if defined(ESP32)
  #include <WiFiMulti.h>
  WiFiMulti wifiMulti;
  #define DEVICE "ESP32"
  #elif defined(ESP8266)
  #include <ESP8266WiFiMulti.h>
  ESP8266WiFiMulti wifiMulti;
  #define DEVICE "ESP8266"
  #endif

// #include <InfluxDbClient.h>
// #include <InfluxDbCloud.h>
  
// WiFi AP SSID
#define WIFI_SSID "Trung Bom"
// WiFi password
#define WIFI_PASSWORD "0933459889"
  
// #define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
// #define INFLUXDB_TOKEN "870PSg8R7yihqlDKR7SPn0UA82_7UrrWPd1LlQwC3sOJn_qSX7uCsRpaUZprvv3L6lQuSMrUdNc_AhN4Dpur1g=="
// #define INFLUXDB_ORG "facfec2b685f58e4"
// #define INFLUXDB_BUCKET "DOAN1"
// #define TZ_INFO "UTC7"

// // Declare InfluxDB client instance with preconfigured InfluxCloud certificate
// InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
  
// // Declare Data point cho các chỉ số sức khoẻ
// Point vitals("vitals");   // "vitals" là tên measurement trong Influx
//-------------------PIN DEFINITIONS----------------
#define SDA_A 9
#define SCL_A 10
#define SDA_B 11
#define SCL_B 12

//-------------------SYSTEM SETUP-------------------
bool newphase = true;

//-------------------SCREEN SETUP-------------------
#define TFT_CS        7
#define TFT_RST       5 // Or set to -1 and connect to Arduino RESET pin
#define TFT_DC        8
#define TFT_MOSI      6 // Data out
#define TFT_SCLK      4 // Clock out

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

//-------------------HEARTRATE & SPO2 VARIABLES---------
bool avr_get_hr = false;
uint32_t count = 0;
uint32_t heartrate_sample_time = 0;
bool heartrate_init = false;
uint8_t prev_hr = 0;

const byte RATE_SIZE = 4; // Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE];    // Array of heart rates
byte rateSpot = 0;
long lastBeat = 0;        // Time at which the last beat occurred

float beatsPerMinute;
int beatAvg;

bool SPO2_init = false;
uint8_t SPO2_attempt = 0;
uint32_t irBuffer[100];   // infrared LED sensor data
uint32_t redBuffer[100];  // red LED sensor data

int32_t bufferLength;     // data length
int32_t spo2 = 0;         // SPO2 value
int8_t validSPO2;         // indicator to show if the SPO2 calculation is valid
int32_t heartRate;        // heart rate value
int8_t validHeartRate;    // indicator to show if the heart rate calculation is valid
uint32_t prev_spo2 = 0;
//}
//-------------------TEMPERATURE VARIABLES---------
MAX30205 tempSensor;
MAX30105 particleSensor;

float prev_temp;
float cur_temp;
bool temp_init;
uint32_t temp_last_time;
uint8_t temp_sample_time;
uint32_t temp_list[5];
float avr_temp;
float p = 3.1415926;

//-------------STATE MACHINE---------------
enum Phase {
  START,
  TEMPERATURE,
  HEARTRATE,
  SPO2,
  END
};
Phase cur_phase = START;
uint32_t last_hr_send = 0;

// ===== MQTT / HiveMQ Cloud =====
// Host HiveMQ Cloud của bạn
const char* mqtt_server = "e62b9d4643eb4dc384b92a72ccf79f3f.s1.eu.hivemq.cloud";
// WebSocket secure port mặc định của HiveMQ Cloud là 8884
const uint16_t mqtt_port = 8884;
const char* mqtt_path = "/mqtt";

WebSocketsClient wsClient;   // WebSocket client
MQTTPubSubClient mqtt;       // MQTT client chạy trên WebSocket

//-------------------FUNCTIONS---------------------
// void sendTempToInflux(float temp) {
//   vitals.clearTags();
//   vitals.clearFields();
//
//   vitals.addTag("device", DEVICE);   // "ESP32"
//   vitals.addTag("node", "1");        // nếu sau này có nhiều node thì đổi
//
//   vitals.addField("temp", temp);
//
//   if (!client.writePoint(vitals)) {
//     Serial.print("InfluxDB write temp failed: ");
//     Serial.println(client.getLastErrorMessage());
//   } else {
//     Serial.print("Temp sent: ");
//     Serial.println(temp);
//   }
// }
//
// void sendHrToInflux(int hr) {
//   vitals.clearTags();
//   vitals.clearFields();
//
//   vitals.addTag("device", DEVICE);
//   vitals.addTag("node", "1");
//
//   vitals.addField("heartrate", hr);
//
//   if (!client.writePoint(vitals)) {
//     Serial.print("InfluxDB write HR failed: ");
//     Serial.println(client.getLastErrorMessage());
//   } else {
//     Serial.print("HR sent: ");
//     Serial.println(hr);
//   }
// }
//
// void sendSpo2ToInflux(int spo2_val) {
//   vitals.clearTags();
//   vitals.clearFields();
//
//   vitals.addTag("device", DEVICE);
//   vitals.addTag("node", "1");
//
//   vitals.addField("spo2", spo2_val);
//
//   if (!client.writePoint(vitals)) {
//     Serial.print("InfluxDB write SpO2 failed: ");
//     Serial.println(client.getLastErrorMessage());
//   } else {
//     Serial.print("SpO2 sent: ");
//     Serial.println(spo2_val);
//   }
// }

void configMax30102()
{
  particleSensor.setup(); // Configure sensor with default settings
  particleSensor.setPulseAmplitudeRed(0x0A); // Turn Red LED to low to indicate sensor is running
  particleSensor.setPulseAmplitudeGreen(0);  // Turn off Green LED

  tft.setTextSize(4);
  tft.setTextColor(ST7735_CYAN);
  tft.setCursor(42, 146);
  tft.println("--");

  tft.setTextSize(2);
  tft.setCursor(124, 161);
  tft.println("BPM");

  tft.setTextSize(4);
  tft.setCursor(43, 243);
  tft.println("--");
}

void configSPO2()
{
  byte ledBrightness = 60; // Options: 0=Off to 255=50mA
  byte sampleAverage = 4;  // Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2;        // Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  byte sampleRate = 100;   // Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411;    // Options: 69, 118, 215, 411
  int adcRange = 4096;     // Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); // Configure sensor with these settings
}

void updatNumberHR(float new_hr)
{
  if (new_hr != prev_hr)
  {
    tft.setTextColor(ST7735_BLACK);
    tft.setTextSize(3);
    tft.setCursor(22, 161);
    tft.print(prev_hr);
    // 2. In số mới
    tft.setTextColor(ST7735_CYAN);
    tft.setTextSize(3);
    tft.setCursor(22, 161);
    tft.print(new_hr);

    prev_hr = new_hr;
    return;
  }
  else
    return;
}

void updatNumberTemp(float new_temp)
{
  if (new_temp != prev_temp)
  {
    tft.fillRect(42, 52, 72, 32, ST77XX_BLACK);  // toạ độ, kích thước, màu nền

    // 2. In số mới
    tft.setTextColor(ST7735_CYAN);
    tft.setTextSize(3);
    tft.setCursor(22, 52);
    tft.print(new_temp);

    prev_temp = new_temp;
    return;
  }
  else
    return;
}

void updateNumberSPO2(float new_SPO2)
{
  if(new_SPO2 == -999 || new_SPO2 == 0) {
    return;
  }
  else{
    tft.setTextColor(ST7735_BLACK);
    tft.setTextSize(3);
    tft.setCursor(22, 243);
    tft.print(prev_spo2);
    // 2. In số mới
    tft.setTextColor(ST7735_CYAN);
    tft.setTextSize(3);
    tft.setCursor(22, 243);
    tft.print(new_SPO2);
    mqtt.publish("/spo2", String(spo2).c_str());
    prev_spo2 = new_SPO2;
    return;
  }
  return;
}

void screenFrameSetUp()
{
  //---type of data
  tft.init(172, 320);
  tft.setSPISpeed(40000000);
  tft.setRotation(0);
  tft.fillScreen(ST7735_BLACK);
  tft.drawRoundRect(0, 0, 170, 320, 25, ST7735_WHITE);

  tft.setTextColor(ST7735_GREEN);
  tft.setTextSize(2);
  tft.setTextWrap(false);

  tft.setCursor(22, 24);
  tft.println("Temp");

  tft.setCursor(25, 115);
  tft.println("HeartRate");

  tft.setCursor(26, 204);
  tft.println("SpO2");

  // data display
  tft.setTextSize(4);
  tft.setTextColor(ST7735_RED);
  tft.setCursor(42, 52);
  tft.println("--");

  tft.setCursor(91, 60);
  tft.println("");   // dòng trống

  tft.drawEllipse(128, 54, 4, 4, ST7735_RED);
  tft.setCursor(141, 53);
  tft.println("C");

  tft.setCursor(42, 146);
  tft.println("--");

  tft.setTextSize(2);
  tft.setCursor(124, 161);
  tft.println("BPM");

  tft.setTextSize(4);
  tft.setCursor(43, 243);
  tft.println("--");
}

void tempatureAvailable()
{
  tft.setTextColor(ST7735_CYAN);
  tft.setCursor(42, 52);
  tft.println("--");

  tft.setCursor(91, 60);
  tft.println("");

  tft.drawEllipse(128, 54, 4, 4, ST7735_CYAN);
  tft.setCursor(141, 53);
  tft.println("C");
}

void get_tempature()
{
  cur_temp = tempSensor.getTemperature();
  if (cur_temp > 45 || cur_temp < 19)
    return;
  else {
    switch (temp_sample_time)
    {
      case 0:
        temp_list[0] = cur_temp;
        temp_sample_time = 1;
        Serial.print("temp1 = ");
        Serial.println(cur_temp);
        break;
      case 1:
        temp_list[1] = cur_temp;
        temp_sample_time = 2;
        Serial.print("temp1 = ");
        Serial.println(cur_temp);
        break;
      case 2:
        temp_list[2] = cur_temp;
        temp_sample_time = 3;
        Serial.print("temp2 = ");
        Serial.println(cur_temp);
        break;
      case 3:
        temp_list[3] = cur_temp;
        temp_sample_time = 0;
        Serial.print("temp3 = ");
        Serial.println(cur_temp);
        avr_temp = (temp_list[3] + temp_list[2] + temp_list[1] + temp_list[0] ) / 4.0;
        Serial.print("temp average = ");
        Serial.println(avr_temp);
        updatNumberTemp(avr_temp);
        newphase = true;
        mqtt.publish("/temp", String(avr_temp).c_str());
        cur_phase = HEARTRATE;
        break;
    }
  }
}

//-------------------MAIN SETUP---------------------
void setup(void) {
  Serial.begin(115200);
/* Wifi*/
Serial.println("CONNECTING TO WIFI...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WIFI CONNECTED!");
  Serial.print("IP ADDRESS: ");
  Serial.println(WiFi.localIP());

  //**********************************MQTT SETUP************************************
  //Configure MQTT on WebSocket
  mqtt.begin(wsClient);
  // --- Cấu hình WebSocket tới HiveMQ Cloud ---
  // wsClient.beginSSL(host, port, url);
  wsClient.beginSSL(mqtt_server, mqtt_port, mqtt_path);

  // Báo cho server biết mình dùng subprotocol MQTT
  wsClient.setReconnectInterval(2000);  // auto reconnect mỗi 2s nếu mất kết nối
  Serial.print("Connecting to MQTT broker...");
  // mqtt.connect(clientId, username, password)
  while (!mqtt.connect("ESP32", "Alexdan", "Abc@1234")) {
    Serial.print(".");
    mqtt.update(); // cho nó xử lý WebSocket trong lúc chờ
    delay(1000);
  }
  Serial.println(" connected!");

    
  //*****************************************SCREEN******************************************
  screenFrameSetUp();
  Serial.println("Frame done!");

  //*************************************HEARTRATE&SPO2**************************************
  Wire1.begin(SDA_A, SCL_A);
  while (!particleSensor.begin(Wire1, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found, retrying in 1s...");
    delay(1000);
  }
  configMax30102();
  Serial.println("MAX30102 READY TO ROLL IN!");

  //********************************TEMPARATURE*************************************
  Wire.begin(SDA_B, SCL_B);
  temp_init = false;
  while (!tempSensor.scanAvailableSensors()) {
    Serial.println("Couldn't find the temperature sensor, please connect the sensor." );
    delay(1000);
  }
  Serial.println("MAX30205 READY TO ROLL IN!");
  tempatureAvailable();
  tempSensor.begin();   // set continuos mode, active mode
  Serial.println("Initalized!");
}

//-------------------MAIN LOOP---------------------
void loop() {
  mqtt.update(); // maintain MQTT connection
  switch (cur_phase) {
    case START: {
        Serial.println("START PHASE!");
        for (int i = 1; i <= 5; i++)
        {
          switch (i) {
            case 1:
              tft.drawRoundRect(0, 0, 170, 320, 25, ST7735_WHITE);
              break;
            case 2:
              tft.drawRoundRect(0, 0, 170, 320, 25, ST7735_RED);
              break;
            case 3:
              tft.drawRoundRect(0, 0, 170, 320, 25, ST7735_GREEN);
              break;
            case 4:
              tft.drawRoundRect(0, 0, 170, 320, 25, ST7735_CYAN);
              break;
            case 5:
              tft.drawRoundRect(0, 0, 170, 320, 25, ST7735_YELLOW);
              break;
          }
          delay(500);
        }
        tft.drawRoundRect(0, 0, 170, 320, 25, ST7735_WHITE);
        cur_phase = TEMPERATURE;
        break;
      }
    case TEMPERATURE: {
        if (newphase)
        {
          newphase = false;
          Serial.println("TEMPERATURE PHASE!");
        }
        if (millis() - temp_last_time >= 1000) { // mỗi 1s lấy 1 mẫu
          temp_last_time = millis();
          get_tempature();
        }
        break;
      }
    case HEARTRATE: {
        if (newphase) {
          configMax30102();
          Serial.println("HEARTRATE PHASE");
          newphase = false;
          tft.setTextColor(ST7735_BLACK);
          tft.setTextSize(4);
          tft.setCursor(42, 146);
          tft.println("--");
          heartrate_sample_time = millis();
        }
        if (millis() - heartrate_sample_time <= 30000)  // đo trong 30s
        {
          long irValue = particleSensor.getIR();
          if (checkForBeat(irValue) == true)
          {
            Serial.println("Beat detected!");
            //We sensed a beat!
            long delta = millis() - lastBeat;
            lastBeat = millis();
            beatsPerMinute = 60 / (delta / 1000.0);
            if (beatsPerMinute < 255 && beatsPerMinute > 20)
            {
              rates[rateSpot++] = (byte)beatsPerMinute; //Store this reading in the array
              rateSpot %= RATE_SIZE; //Wrap variable

              //Take average of readings
              beatAvg = 0;
              for (byte x = 0 ; x < RATE_SIZE ; x++)
                beatAvg += rates[x];
              beatAvg /= RATE_SIZE;
              if(millis() - heartrate_sample_time >= 10000) { // gửi dữ liệu sau 10s đầu tiên
                mqtt.publish("/heartrate", String(beatAvg).c_str());
                }
            }
            if (millis() - last_hr_send >= 1000) {
              last_hr_send = millis();
              updatNumberHR(beatAvg);
            }
          }
        }
        else {
          cur_phase = SPO2;
          newphase = true;
          heartrate_sample_time = 0;
        }
        break;
    }
    case SPO2: {
        if (newphase)
        {
          Serial.println("SPO2 PHASE!");
          configSPO2();
          newphase = false;
          tft.setTextColor(ST7735_BLACK);
          tft.setTextSize(4);
          tft.setCursor(43, 243);
          tft.println("--");
        }
        if (SPO2_attempt == 4)
        {
          cur_phase = TEMPERATURE;
          SPO2_attempt = 0;
          return;
        }
        SPO2_attempt ++;
        bufferLength = 100; // buffer length of 100 stores 4 seconds of samples running at 25sps

        // read the first 100 samples, and determine the signal range
        for (byte i = 0 ; i < bufferLength ; i++)
        {
          while (particleSensor.available() == false) // do we have new data?
            particleSensor.check(); // Check the sensor for new data

          redBuffer[i] = particleSensor.getRed();
          irBuffer[i] = particleSensor.getIR();
          particleSensor.nextSample(); // We're finished with this sample so move to next sample
        }

        // calculate heart rate and SpO2 after first 100 samples (first 4 seconds of samples)
        maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

          // dumping the first 25 sets of samples in the memory and shift the last 75 sets of samples to the top
          for (byte i = 25; i < 100; i++)
          {
            redBuffer[i - 25] = redBuffer[i];
            irBuffer[i - 25] = irBuffer[i];
          }

          // take 25 sets of samples before calculating the heart rate.
          for (byte i = 75; i < 100; i++)
          {
            while (particleSensor.available() == false) // do we have new data?
              particleSensor.check(); // Check the sensor for new data

            // digitalWrite(readLED, !digitalRead(readLED)); // Blink onboard LED with every data read

            redBuffer[i] = particleSensor.getRed();
            irBuffer[i] = particleSensor.getIR();
            particleSensor.nextSample(); // We're finished with this sample so move to next sample

            updateNumberSPO2(spo2);
            // sendSpo2ToInflux(spo2);
          // After gathering 25 new samples recalculate HR and SP02
          maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
        } // End while(1)
        break;
      }
    case END:
      // TODO: xử lý cho phase END
      break;
    default:
      // TODO: xử lý nếu có lỗi phase
      break;
  }
}