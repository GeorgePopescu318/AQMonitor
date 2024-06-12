/*

  AirQualityMonitor

  The following code represents the software side of a device that reads the values of Particulate Matter and CO2 present in your area,
  the values are displayed on a screen and plots are drawn to represent the changes in value. The device can also show the local temperature
  and relative humidity. The data has been logged using a Filesystem present on the device's brain (microcontroller).The data is stored using
  CSV files. You can cycle through thediffrent mesurments by using a button,a shortpress will change the current mesurement while a long press
  will change from displaying the current value to drawing the current mesurement's plot.

  The circuit:
  - ESP32-C6-DebKit-1 - microcontroller 
  - PMS5003 - PM sensor
  - SCD-40 - CO2, temperature and realtive humidity sensor 
  - 1.14" 240x135 Color TFT Display
  - a button 

  https://github.com/GeorgePopescu318/AQMonitor


*/
#include <LittleFS.h>           // Filesystem library
#include <Adafruit_GFX.h>       // Core graphics library
#include <Adafruit_ST7735.h>    // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h>    // Hardware-specific library for ST7789
#include <SPI.h>                // SPI communication with screen library
#include <Arduino.h>            // Default Arduino library
#include <Wire.h>               // Genral i2C communication7 library
#include <SensirionI2CScd4x.h>  // Specific i2c communication with SCD-40 library
#include <Adafruit_PM25AQI.h>   // Specific UART communication with PMS5003 library
#include <EasyButton.h>         // Plug and play button library

//Screen
#define YELLOW 0xFE80
#define PURPLE 0x629B

#define NR_COLUMNS 8

#define TFT_CS 3
#define TFT_RST 11
#define TFT_DC 10

#define TFT_MOSI 19
#define TFT_SCLK 21
//originally I was going to use a SD card for logging data, it didn't go well and I went for littleFS, the display has a SD slot so you can use it
// #define SD_CS 18
// #define MISO 20

#define MAX_POINTS_IN_PLOT_X 75
#define MAX_POINTS_IN_PLOT_Y 125

#define BUTTON_NEXT_PIN 0

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

const String plotName[6] = { "CO2", "Temp", "RHumidity", "PM1.0", "PM2.5", "PM10.0" };

byte currentMesurement = 1;
byte lastMesurement = 1;

unsigned long long drawingTime = 0;
unsigned long long lastDrawingTime = 0;

byte drawingRate = 20;

bool drawAxis = false;
bool lastDrawAxis = false;

const byte screenWidth = 240;
const byte screenHeight = 135;

bool hasDrawnBlack = false;

//FileSystem
char fileName[] = "/xx.csv";

bool createNewFile = false;

int currentEntryIndex = 0;

unsigned int currentCO2 = 1;
unsigned int currentTemperature = 1;
unsigned int currentRelativeHumidity = 1;

int currentPM10 = 1;
int currentPM25 = 1;
int currentPM100 = 1;

File root;
File file;

std::vector<std::vector<int>> plotValueMatrix(8, std::vector<int>(0));

//Sensors
SensirionI2CScd4x CO2TempRHSensor;

Adafruit_PM25AQI PM25Sensor = Adafruit_PM25AQI();

PM25_AQI_Data PM25Data;

unsigned long long samplingTime = 0;
unsigned long long lastSamplingTime = 0;

byte samplingRate = 5;

//Button
const int buttonNextLongDuration = 1000;

EasyButton button(BUTTON_NEXT_PIN);

void setup() {
  Serial.begin(9600);

  button.begin();
  button.onPressedFor(buttonNextLongDuration, OnPressedForDurationLongPress);
  button.onPressed(OnPressedForDurationShortPress);

  InitializeCO2TemperatureRelativeHumidity();

  InitializePMS5003();

  InitializeScreen();

  Serial.println("FS begining");
  if (!LittleFS.begin()) {
    Serial.println("LittleFS init fail");
    
    return;
  }

  InitializeLoggingFileSystem();

  Serial.println("Init done");

  if (!findIndex()) {
    Serial.println("Finding index failed");
  }

  lastSamplingTime = millis();
  lastDrawingTime = millis();

  if (!updatePlotValueMatrix()) {
    Serial.println("reading error");

    file.close();
    root.close();

    exit(0);
  }

  if (drawAxis == true) {
    InitializeAxis(plotName[currentMesurement - 1]);
    DrawPlot(plotValueMatrix[currentMesurement]);
  }

  printValue();
}

void loop() {
  button.read();
  if (currentMesurement != lastMesurement) {
    lastMesurement = currentMesurement;

    tft.fillScreen(ST77XX_BLACK);

    Serial.println("BLACK CHANGE");

    hasDrawnBlack = true;

    if (drawAxis == true) {
      InitializeAxis(plotName[currentMesurement - 1]);

      DrawPlot(plotValueMatrix[currentMesurement]);
    } else {
      printValue();
    }
  }

  if (drawAxis != lastDrawAxis) {
    tft.fillScreen(ST77XX_BLACK);

    if (drawAxis == true) {
      InitializeAxis(plotName[currentMesurement - 1]);

      DrawPlot(plotValueMatrix[currentMesurement]);
    } else {
      printValue();
    }
    lastDrawAxis = drawAxis;
  }

  ActionRate();
}

//FileSystem
//Initializing the file for writing in it
void InitializeLoggingFileSystem() {

  if (!BuildFile()) {
    Serial.println("Filename can't be created");

    file.close();
    root.close();
    LittleFS.end();
    exit(1);
  }

  if (createNewFile) {
    if (!WriteHeader()) {
      Serial.println("Header can't be written");

      file.close();
      root.close();
      LittleFS.end();
      exit(1);
    }
  }

  if (!findIndex()) {
    Serial.println("Can't find index");

    file.close();
    root.close();
    LittleFS.end();
    exit(1);
  }

  Serial.println("Saving to: ");
  Serial.println(fileName);
}
// Building the file, if createNewFile is true a new empty file is created and written in it
// otherwise the logging will happen in the last created file
bool BuildFile() {

  byte fileNumber = 0;

  bool searchedFiles = false;

  File entry = LittleFS.open("/");

  if (!entry) {
    Serial.println("Failed to open root directory!");

    return false;
  }

  while (File file = entry.openNextFile()) {

    const char *localFileName = file.name();

    Serial.println("FILES");
    Serial.println(localFileName);
    // Check if the file name matches the "NN.CSV" format
    if (localFileName[2] == '.' && localFileName[3] == 'c' && localFileName[4] == 's' && localFileName[5] == 'v') {
      int lastFileNumber = (localFileName[0] - '0') * 10 + (localFileName[1] - '0');
      if (lastFileNumber > fileNumber) {
        fileNumber = lastFileNumber;
      }
    }

    file.close();

    searchedFiles = true;
  }

  Serial.println(fileNumber + 1);

  if (searchedFiles == false) {
    Serial.println("No files found");

  } else {
    Serial.println("Files found");
  }

  if (createNewFile) {
    fileNumber = fileNumber + 1;

    if (fileNumber > 9) {
      fileName[1] = (int)((fileNumber / 10) % 10) + '0';
      fileName[2] = (int)(fileNumber % 10) + '0';
    } else {
      fileName[1] = '0';
      fileName[2] = (int)(fileNumber % 10) + '0';
    }
    Serial.println("Creating new file");

    Serial.println(fileName);
    if (LittleFS.exists(fileName)) {
      Serial.println("File already exists, filename: ");
      Serial.print(fileName);

      return false;
    }

    return true;

  } else {
    Serial.println("Writing to existing file");

    if (fileNumber > 9) {
      fileName[1] = (int)((fileNumber / 10) % 10) + '0';
      fileName[2] = (int)(fileNumber % 10) + '0';
    } else {
      fileName[1] = '0';
      fileName[2] = (int)(fileNumber % 10) + '0';
    }

    return true;
  }

  Serial.println("Not in if");
  return false;
}

//if a new empty file is created  a header with all the mesurements' names will pe placed at the top of the file
bool WriteHeader() {
  File file = LittleFS.open(fileName, "w");
  if (!file) {
    Serial.println("Writing header failed, can't open file for writing");
    file.close();
    return false;
  }
  file.print(F("RelTime,CO2,Temp,RH,PM1.0,PM2.5,PM10.0,Index"));
  file.print("\n");

  file.close();

  delay(50);
  return true;
}

//function that writes the data in the current file, one line at a time, each value separted by a comma (CSV)
bool LoggingDataFileSystem() {
  if (currentCO2 > 0 && currentRelativeHumidity > 0 && currentPM10 > 0 && currentPM25 > 0 && currentPM100 > 0) {
    if (LittleFS.exists(fileName)) {
      File file = LittleFS.open(fileName, "a");

      if (!file) {
        Serial.println("Opening file for logging failed");
        file.close();
        return false;
      }


      file.print(millis() / 1000);
      file.print(F(","));
      file.print(currentCO2);
      file.print(F(","));
      file.print(currentTemperature);
      file.print(F(","));
      file.print(currentRelativeHumidity);
      file.print(F(","));
      file.print(currentPM10);
      file.print(F(","));
      file.print(currentPM25);
      file.print(F(","));
      file.print(currentPM100);
      file.print(F(","));
      file.print(currentEntryIndex);
      file.print("\n");

      currentEntryIndex++;

      Serial.println("Logging worked");
      file.close();

      return true;
    }
  }
  return false;
}
void deleteFile(const char *path) {

  Serial.printf("Deleting file: %s\r\n", path);

  if (LittleFS.remove(path)) {
    Serial.println("- file deleted");
  } else {
    Serial.println("- delete failed");
  }
}

bool readFile() {
  File file = LittleFS.open(fileName, "r");

  if (!file) {
    Serial.println("Writing header failed, can't open file for writing");

    file.close();

    return false;
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');
    Serial.println(line);
  }

  file.close();

  delay(50);
  return true;
}

bool findIndex() {
  File file = LittleFS.open(fileName, "r");

  if (!file) {
    Serial.println("Failed to open file for reading");
    return false;
  }

  if (!file.seek(0, SeekEnd)) {
    Serial.println("Failed to seek to end of file");

    file.close();

    return false;
  }

  long fileSize = file.size();
  long position = fileSize - 1;
  char lastChar;
  bool foundComma = false;

  while (position > 0) {
    file.seek(position);

    lastChar = file.read();
    if (lastChar == ',') {

      foundComma = true;

      break;
    }

    position--;
  }

  if (!foundComma) {
    Serial.println("No comma found, cannot determine the last number");

    file.close();

    return false;
  }

  Serial.println(position);
  file.seek(position + 1);

  String lastRow = file.readStringUntil('\n');

  Serial.print("Last row: ");
  Serial.println(lastRow);

  int commaIndex = lastRow.indexOf(',');
  String firstValue = lastRow.substring(0, commaIndex);

  Serial.print("First value from the last row: ");
  Serial.println(firstValue);

  currentEntryIndex = firstValue.toInt() + 1;

  file.close();

  return true;
}
//a local matrix containing the maximum number of values (75) that can be plotted is updated here
bool updatePlotValueMatrix() {

  file = LittleFS.open(fileName, "r");

  if (!file) {
    Serial.println("Failed to open file for reading");

    return false;
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');

    extractDataFromCSV(line);
  }

  file.close();
  Serial.println("plotValueMatrix update done");

  printPlotValueMatrix();

  return true;
}
//extracts each value from the csv
void extractDataFromCSV(String line) {
  int column = 0;
  int valueStart = 0;

  for (int i = 0; i <= line.length(); i++) {

    if (line[i] == ',' || i == line.length()) {
      int value = line.substring(valueStart, i).toInt();
      // Ensure column index is within bounds
      if (column < plotValueMatrix.size()) {
        if (value > 0) {
          addToVector(column, value);
        }
      } else {
        Serial.print("Column index out of bounds: ");
        Serial.println(column);
      }

      column++;

      valueStart = i + 1;
    }
  }
}

//adds each new value to the matrix
void addToVector(int column, int value) {
  if (plotValueMatrix[column].size() >= MAX_POINTS_IN_PLOT_X) {
    // Shift values to the left
    for (int i = 1; i < MAX_POINTS_IN_PLOT_X; i++) {
      plotValueMatrix[column][i - 1] = plotValueMatrix[column][i];
    }
    // Add new value at the end
    plotValueMatrix[column][MAX_POINTS_IN_PLOT_X - 1] = value;
  } else {
    // Add value normally
    plotValueMatrix[column].push_back(value);
  }
}

void printPlotValueMatrix() {
  for (int i = 0; i < plotValueMatrix.size(); i++) {
    Serial.print("Column ");
    Serial.print(i + 1);
    Serial.print(": ");
    for (int j = 0; j < plotValueMatrix[i].size(); j++) {
      Serial.print(plotValueMatrix[i][j]);
      Serial.print(" ");
    }
    Serial.println();
  }
}
//Screen
void InitializeScreen() {
  tft.init(screenHeight, screenWidth);

  tft.fillScreen(ST77XX_BLACK);
  //Rotation, you can change this value depending of the orientation of the display
  tft.setRotation(3);

  //Using the classic cp437 value
  tft.cp437(true);
}
//printing the current values to the display
void printValue() {
  tft.setTextSize(4);

  tft.setCursor(5, 5);

  tft.setTextColor(YELLOW);

  tft.print(plotName[currentMesurement - 1]);

  tft.print("\n");

  switch (currentMesurement) {
    case 1:
      {
        tft.print(currentCO2);
      }
      break;
    case 2:
      {
        tft.print(currentTemperature);
        tft.write(0xF8);
        tft.print("C");
      }
      break;
    case 3:
      {
        tft.print(currentRelativeHumidity);
        tft.print("%");
      }
      break;
    case 4:
      {
        tft.print(currentPM10);
        tft.write(0xE6);
        tft.print("g/m3");
      }
      break;
    case 5:
      {
        tft.print(currentPM25);
        tft.write(0xE6);
        tft.print("g/m3");
      }
      break;
    case 6:
      {
        tft.print(currentPM100);
        tft.write(0xE6);
        tft.print("g/m3");
      }
      break;
  }
}

//initializing the axis for the plot
void InitializeAxis(const String plotName) {

  // the values plotted will not be scalled so the Y axis has values on the side so you know the actual values from the plot
  const byte verticalScaleForPM[9] = { 1, 0, 0, 7, 5, 5, 0, 2, 5 };

  tft.setTextSize(1);

  byte nextScale = 0;
  byte position = 25;
  byte chrSize = 8;

  for (int i = 8; i < 10; i++) {
    tft.drawFastVLine(i, 0, screenHeight, PURPLE);
  }

  for (int i = 8; i < 10; i++) {
    tft.drawFastHLine(0, screenHeight - i, screenWidth, PURPLE);
  }

  tft.setCursor(screenWidth / 2 - plotName.length() / 2, screenHeight - 8);
  tft.setTextColor(YELLOW);
  tft.print(plotName);

  tft.setCursor(0, position);
  tft.print(verticalScaleForPM[nextScale]);
  nextScale++;
  tft.setCursor(0, position + chrSize);
  tft.print(verticalScaleForPM[nextScale]);
  nextScale++;
  tft.setCursor(0, position + 2 * chrSize);
  tft.print(verticalScaleForPM[nextScale]);
  nextScale++;
  position += 25;

  tft.setCursor(0, position);
  tft.print(verticalScaleForPM[nextScale]);
  nextScale++;
  tft.setCursor(0, position + chrSize);
  tft.print(verticalScaleForPM[nextScale]);
  nextScale++;
  position += 25;

  tft.setCursor(0, position);
  tft.print(verticalScaleForPM[nextScale]);
  nextScale++;
  tft.setCursor(0, position + chrSize);
  tft.print(verticalScaleForPM[nextScale]);
  nextScale++;
  position += 25;

  tft.setCursor(0, position);
  tft.print(verticalScaleForPM[nextScale]);
  nextScale++;
  tft.setCursor(0, position + chrSize);
  tft.print(verticalScaleForPM[nextScale]);
  nextScale++;

  Serial.println("Axis done");
}

void DrawPlot(std::vector<int> mesurementsValue) {
  int XAxisValue = 5;

  const byte firstYShift = 8;
  const byte nextYSgift = 11;
  const byte pointShift = 3;

  byte pointsPloted = MAX_POINTS_IN_PLOT_X;

  if (mesurementsValue.size() < MAX_POINTS_IN_PLOT_X) {
    pointsPloted = mesurementsValue.size();
  } else {
    if (hasDrawnBlack == false) {
      tft.fillScreen(ST77XX_BLACK);
      Serial.println("BLACK DRAW");
    } else {
      hasDrawnBlack = false;
    }
    InitializeAxis(plotName[currentMesurement - 1]);
  }

  int firstY;
  int nextY;

  for (int i = 1; i < pointsPloted; i++) {
    if (currentMesurement == 1) {
      int co2ValueFirst = map(mesurementsValue[i - 1], 0, 2000, 0, 125);
      int co2ValueNext = map(mesurementsValue[i], 0, 2000, 0, 125);

      firstY = map(co2ValueFirst, 0, MAX_POINTS_IN_PLOT_Y, MAX_POINTS_IN_PLOT_Y, 0);
      nextY = map(co2ValueNext, 0, MAX_POINTS_IN_PLOT_Y, MAX_POINTS_IN_PLOT_Y, 0);
    } else {
      firstY = map(mesurementsValue[i - 1], 0, MAX_POINTS_IN_PLOT_Y, MAX_POINTS_IN_PLOT_Y, 0);
      nextY = map(mesurementsValue[i], 0, MAX_POINTS_IN_PLOT_Y, MAX_POINTS_IN_PLOT_Y, 0);
    }


    if (firstY > 125) {
      firstY = 125;
    }

    if (nextY > 125) {
      nextY = 125;
    }
    tft.drawLine(XAxisValue + firstYShift, firstY, XAxisValue + nextYSgift, nextY, YELLOW);

    XAxisValue += pointShift;
  }

  Serial.println("Drawing done");
}
//Sensors
//PMS5003
void InitializePMS5003() {
  Serial1.begin(9600);

  if (!PM25Sensor.begin_UART(&Serial1)) {
    Serial1.begin(9600);

    Serial.println("Could not find PM 2.5 sensor!");
    while (1) delay(10);
  }
  Serial.println("PMS5003 found!");
}

void PM25SensorReading() {

  if (!PM25Sensor.read(&PM25Data)) {
    Serial.println("Could not read from AQI");
    delay(500);  // try again in a bit!
    return;
  }
  Serial.println("AQI reading success");
  if (PM25Data.pm10_env > 0 && currentPM10 != PM25Data.pm10_env) {
    currentPM10 = PM25Data.pm10_env;
  }
  if (PM25Data.pm25_env > 0 && currentPM25 != PM25Data.pm25_env) {
    currentPM25 = PM25Data.pm25_env;
  }
  if (PM25Data.pm100_env > 0 && currentPM100 != PM25Data.pm100_env) {
    currentPM100 = PM25Data.pm100_env;
  }
}
//this function can calculate the pm values by taking into consideration the current relative humidity
//I didn't call the function because the PM sensor has a way of doing this on his own, but I am not
//sure how, if you take the standard values and pass them through the correction function you will get
//higher values than from the standard enviromental data, a deeper reserach here is needed
int CorrectionFormulaForEnviromentalData(int pm) {
  float k = 0.4;
  int RH = currentRelativeHumidity;
  return pm / (1 + ((k / 1.65) / (-1 + (1 / RH))));
}

//determines how frequently the data will be read and logged as well as how frequently screen will be updated
void ActionRate() {

  samplingTime = millis();

  if ((samplingTime - lastSamplingTime) / 1000 >= samplingRate) {

    lastSamplingTime = samplingTime;

    PM25SensorReading();
    CO2TempRHSensorReading();

    if (!LoggingDataFileSystem()) {
      Serial.println("Data writing has failed");

      file.close();
      root.close();
      LittleFS.end();

      exit(1);
    }
  }


  drawingTime = millis();

  if ((drawingTime - lastDrawingTime) / 1000 >= drawingRate) {
    if (!updatePlotValueMatrix()) {
      Serial.println("reading error");

      file.close();
      root.close();

      exit(0);
    }
    Serial.println(drawAxis);
    if (drawAxis == true) {
      DrawPlot(plotValueMatrix[currentMesurement]);


    } else {
      tft.fillScreen(ST77XX_BLACK);
      printValue();
    }
    lastDrawingTime = drawingTime;
  }
}

//CO2TemperatureRelativeHumidity
//error functions for the SCD sensor(from their example code)
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

void InitializeCO2TemperatureRelativeHumidity() {
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

void CO2TempRHSensorReading() {
  uint16_t error;
  char errorMessage[256];

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
    Serial.println("data is not ready");
    return;
  }

  float localTemperature;
  float localHumidity;
  uint16_t localCO2;

  error = CO2TempRHSensor.readMeasurement(localCO2, localTemperature, localHumidity);

  if (error) {
    Serial.print("Error trying to execute readMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);

  } else if (localCO2 == 0) {
    Serial.println("Invalid sample detected, skipping.");
  } else {
    Serial.println("cotemprh reading success");

    currentRelativeHumidity = (int)localHumidity;

    currentTemperature = (int)localTemperature;

    currentCO2 = (int)localCO2;
  }
}

//Button
//long pressing will change between displaying current data vs plot data
void OnPressedForDurationLongPress() {
  drawAxis = !drawAxis;
}

//short pressing will change the current mesurement 
void OnPressedForDurationShortPress() {
  currentMesurement++;
  currentMesurement %= NR_COLUMNS - 1;

  if (currentMesurement == 0) {
    currentMesurement++;
  }
}
