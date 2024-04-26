#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2CScd4x.h>
#include <Adafruit_PM25AQI.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

//BLE co2;temp;RH;pm10 env;pm2.5 env; pm100 env CHA = Characteristic
BLEServer* pServer = NULL;
BLECharacteristic* pChaCO2 = NULL;
BLECharacteristic* pChaTemp = NULL;
BLECharacteristic* pChaRH = NULL;
BLECharacteristic* pChaPM10 = NULL;
BLECharacteristic* pChaPM25 = NULL;
BLECharacteristic* pChaPM100 = NULL;

BLE2902 *pBLE2902CO2;
BLE2902 *pBLE2902Temp;
BLE2902 *pBLE2902RH;
BLE2902 *pBLE2902PM10;
BLE2902 *pBLE2902PM25;
BLE2902 *pBLE2902PM100;

bool deviceConnected = false;
bool oldDeviceConnected = false;

static BLEUUID BLESERVICE_UUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHACO2_UUID         "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHATEMP_UUID        "d7be7b90-2423-4d6e-926d-239bc96bb2fd"
#define CHARH_UUID          "47524f89-07c8-43b6-bf06-a21c77bfdee8"
#define CHAPM10_UUID        "f13163b4-cc7f-456b-9ea4-5c6d9cec8ee3"
#define CHAPM25_UUID        "97f57b70-9465-4c46-a2e2-38b604f76451"
#define CHAPM100_UUID       "81104088-d388-4fb9-84ba-427103a7b784"


class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};


SensirionI2CScd4x CO2TempRHSensor;
Adafruit_PM25AQI PM25Sensor = Adafruit_PM25AQI();

PM25_AQI_Data PM25Data;
uint16_t co2 = 0;
float temperature = 0.0f;
float humidity = 0.0f;

void printUint16Hex(uint16_t value) {
  Serial.print(value < 4096 ? "0" : "");
  Serial.print(value < 256 ? "0" : "");
  Serial.print(value < 16 ? "0" : "");
  Serial.print(value, HEX);
}

void printSerialNumber(uint16_t serial0, uint16_t serial1, uint16_t serial2) {
  Serial.print("Serial: 0x");
  printUint16Hex(serial0);
  printUint16Hex(serial1);
  printUint16Hex(serial2);
  Serial.println();
}

void BLEComm(){
  BLEDevice::init("ESP32");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // BLEService *pService = pServer->createService(SERVICE_UUID);
  BLEService *pService = pServer->createService(BLESERVICE_UUID, 30, 0);

  pChaCO2 = pService->createCharacteristic(
                      CHACO2_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pChaTemp = pService->createCharacteristic(
                      CHATEMP_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pChaRH = pService->createCharacteristic(
                      CHARH_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pChaPM10 = pService->createCharacteristic(
                      CHAPM10_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pChaPM25 = pService->createCharacteristic(
                      CHAPM25_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pChaPM100 = pService->createCharacteristic(
                      CHAPM100_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );           

  pBLE2902CO2 = new BLE2902();
  pBLE2902CO2->setNotifications(true);
  pChaCO2->addDescriptor(pBLE2902CO2);

  pBLE2902Temp = new BLE2902();
  pBLE2902Temp->setNotifications(true);
  pChaTemp->addDescriptor(pBLE2902Temp);

  pBLE2902RH = new BLE2902();
  pBLE2902RH->setNotifications(true);
  pChaRH->addDescriptor(pBLE2902RH);

  pBLE2902PM10 = new BLE2902();
  pBLE2902PM10->setNotifications(true);
  pChaPM10->addDescriptor(pBLE2902PM10);

  pBLE2902PM25 = new BLE2902();
  pBLE2902PM25->setNotifications(true);
  pChaPM25->addDescriptor(pBLE2902PM25);

  pBLE2902PM100 = new BLE2902();
  pBLE2902PM100->setNotifications(true);
  pChaPM100->addDescriptor(pBLE2902PM100);

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}



void CO2TempRHComm(){
  Wire.begin(6, 7);

  uint16_t error;
  char errorMessage[256];

  CO2TempRHSensor.begin(Wire);

  // stop potentially previously started measurement
  error = CO2TempRHSensor.stopPeriodicMeasurement();
  if (error) {
    Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }

  uint16_t serial0;
  uint16_t serial1;
  uint16_t serial2;
  error = CO2TempRHSensor.getSerialNumber(serial0, serial1, serial2);
  if (error) {
    Serial.print("Error trying to execute getSerialNumber(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  } else {
    printSerialNumber(serial0, serial1, serial2);
  }

  // Start Measurement
  error = CO2TempRHSensor.startPeriodicMeasurement();
  if (error) {
    Serial.print("Error trying to execute startPeriodicMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }

  Serial.println("Waiting for first measurement... (5 sec)");
}



void PMS5003Comm(){
  Serial1.begin(9600);

  if (!PM25Sensor.begin_UART(&Serial1)) {
    Serial1.begin(9600);
    Serial.println("Could not find PM 2.5 sensor!");
    while (1) delay(10);
  }
  Serial.println("PMS5003 found!");
}


void setup() {
  //Comm with pc
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  BLEComm();

  //Comm with SCD4x C02TempRH Sensor
  CO2TempRHComm();

  //COMM with PMS5003 PM sensor
  PMS5003Comm();
}

void CO2TempRHSensorReading(){
  uint16_t error;
  char errorMessage[256];
  
  // delay(100);

  // Read Measurement
  bool isDataReady = false;

  error = CO2TempRHSensor.getDataReadyFlag(isDataReady);
  if (error) {
    Serial.print("Error trying to execute getDataReadyFlag(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
    return;
  }
  if (!isDataReady) {
    return;
  }
  error = CO2TempRHSensor.readMeasurement(co2, temperature, humidity);
  if (error) {
    Serial.print("Error trying to execute readMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  } else if (co2 == 0) {
    Serial.println("Invalid sample detected, skipping.");
  } else {
    Serial.print("Co2:");
    Serial.print(co2);
    Serial.print(" ");
    Serial.print(co2,HEX);
    Serial.print(" ");
    Serial.print("Temperature:");
    Serial.print(temperature);
    Serial.print(" ");
    Serial.print(temperature,HEX);
    Serial.print(" ");
    Serial.print("Humidity:");
    Serial.print(humidity);
    Serial.print(" ");
    Serial.println(humidity,HEX);
  }
}

void PM25SensorReading(){
  
  
  if (! PM25Sensor.read(&PM25Data)) {
    Serial.println("Could not read from AQI");
    delay(500);  // try again in a bit!
    return;
  }
  Serial.println("AQI reading success");

  Serial.println();
  Serial.println(F("---------------------------------------"));
  Serial.println(F("Concentration Units (standard)"));
  Serial.println(F("---------------------------------------"));
  Serial.print(F("PM 1.0: ")); Serial.print(PM25Data.pm10_standard);
  Serial.print(F("\t\tPM 2.5: ")); Serial.print(PM25Data.pm25_standard);
  Serial.print(F("\t\tPM 10: ")); Serial.println(PM25Data.pm100_standard);
  Serial.println(F("Concentration Units (environmental)"));
  Serial.println(F("---------------------------------------"));
  Serial.print(F("PM 1.0: ")); Serial.print(PM25Data.pm10_env);
  Serial.print(F("\t\tPM 2.5: ")); Serial.print(PM25Data.pm25_env);
  Serial.print(F("\t\tPM 10: ")); Serial.println(PM25Data.pm100_env);
  Serial.println(F("---------------------------------------"));
  Serial.print(F("Particles > 0.3um / 0.1L air:")); Serial.println(PM25Data.particles_03um);
  Serial.print(F("Particles > 0.5um / 0.1L air:")); Serial.println(PM25Data.particles_05um);
  Serial.print(F("Particles > 1.0um / 0.1L air:")); Serial.println(PM25Data.particles_10um);
  Serial.print(F("Particles > 2.5um / 0.1L air:")); Serial.println(PM25Data.particles_25um);
  Serial.print(F("Particles > 5.0um / 0.1L air:")); Serial.println(PM25Data.particles_50um);
  Serial.print(F("Particles > 10 um / 0.1L air:")); Serial.println(PM25Data.particles_100um);
  Serial.println(F("---------------------------------------"));
}

void BLETransmission(){
  if (deviceConnected) {
      pChaCO2->setValue(co2);
      pChaCO2->notify();

      pChaTemp->setValue(temperature);
      pChaTemp->notify();

      pChaRH->setValue(humidity);
      pChaRH->notify();

      pChaPM10->setValue(PM25Data.pm10_env);
      pChaPM10->notify();

      pChaPM25->setValue(PM25Data.pm25_env);
      pChaPM25->notify();

      pChaPM100->setValue(PM25Data.pm100_env);
      pChaPM100->notify();

      delay(3); // bluetooth stack will go into congestion, if too many packets are sent, in 6 hours test i was able to go as low as 3ms
    }
    // disconnecting
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
        Serial.println("start advertising");
        oldDeviceConnected = deviceConnected;
    }
    // connecting
    if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        oldDeviceConnected = deviceConnected;
    }
}

void loop() {
  CO2TempRHSensorReading();
  PM25SensorReading();
  BLETransmission();
}
