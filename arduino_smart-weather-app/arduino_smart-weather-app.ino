//We have 1 module which works with SPI bus by ICSP plug (Ethernet shield)
//We have 2 modules which work with I2C bus by cables (BME and RTC)

#include <SPI.h>              //***** library for SPI bus configuration - enable working with Ethernet shield (Ethernet module with SD module) from ICSP
#include <Wire.h>             //***** library for I2C bus configuration - enable working with BME module and RTC module
#include <Ethernet.h>         //***** library for ehternet configuration
#include "WiFiEsp.h"          //***** library for wifi configuration
#include "SdFat.h"            //***** library for SD configuration
#include <BME280I2C.h>        //***** library for BME module configuration (temperature, humidity, pressure)
#include "LowPower.h"         //***** library for low power comsumption configuration
#include <DS3232RTC.h>        //***** library for RTC configuration (time keeping and sleep interrupt)
#include <avr/wdt.h>          //***** library for watchdog timer (responsible for reset the Arduino in case it stuck)
#include <stdlib.h>           //***** general functions library
#include <string.h>           //***** general functions library

#define EspSerial Serial1     //***** emulate Hardware serial

byte mac[] = { 0x74, 0x69, 0x69, 0x2D, 0x30, 0x31 };             //***** Arduino assigned mac address  (customizable)
char server[] = "example.com";                                   //***** web application server        (customizable)
char ssid[] = "wifi_name";                                       //***** your network SSID (name)      (customizable)
char pass[] = "wifi_name";                                       //***** your network password         (customizable)
int status = WL_IDLE_STATUS;                                     //***** the Wifi radio's status
char txtopen[] = "measures.txt";                                 //***** file created in SD card, when no intenret connection and measures should be saved    (customizable)
byte eth = 10;
byte sd = 4;
byte state = 0;                                                  //***** general state of the app cycle
bool ethernetFailed = false;                                     //***** boolean that checks if Ethernet initialization failed
bool SDFailed = false;                                           //***** boolean that checks if SD initialization failed
bool wifiFailed = false;                                         //***** boolean that checks if Wifi initialization failed
bool dataInSD = false;                                           //***** general purpose boolean that checks if data exist in SD
bool stillSDHasData = false;                                     //***** general purpose boolean that checks if data still exist in SD
bool SDSendAttempt = false;                                      //***** general purpose boolean that determines if selected data to be sent are data from SD
bool RTCSyncFailed = false;                                      //***** boolean that checks if problem occured while RTC syncing time with Arduino timer
byte changeSleepTime = 0;                                        //***** variable used for changing wake up time
byte order = 0;                                                  //***** variable responsible for choosing module in response
unsigned int counter = 0;                                        //***** variable filled in server response, used in SD for measuring chars
unsigned int backUpCounter = 0;                                  //***** variable filled in server response, used after SD fail for backup measuring chars
byte checkIfTxtEmpty = true;                                     //***** variable for checking if txt file is empty
char measures[110];                                              //***** array that holds the whole http request (filled)
char response[20];                                               //***** array that holds server response

const int redLEDPin = 46;                                        // LED connected to digital pin 46 (PWM)
const int greenLEDPin = 45;                                      // LED connected to digital pin 45 (PWM)
const int blueLEDPin = 44;                                       // LED connected to digital pin 44 (PWM)

IPAddress ip(192, 168, 0, 177);                                  //***** set the Arduino static IP address to use if the DHCP fails to assign     (customizable)
SdFat SD;                                                        //***** SD object instantiation
File myFile;                                                     //***** SD File object instantiation
EthernetClient ethClient;                                        //***** Ethernet object instantiation
WiFiEspClient wifiClient;                                        //***** Wifi object instantiation


void setup() {
  pinMode(2, INPUT);                                            // pin needed for interrupt        
  digitalWrite(2, HIGH);                                        // pin needed for interrupt
  pinMode(7, INPUT);                                            // dust pin
  pinMode(53, OUTPUT);                                          // SS hardware only for Arduino mega (without this declaration SPI can't work)
  pinMode(greenLEDPin, OUTPUT);                                 // pin for RGB LED
  pinMode(redLEDPin, OUTPUT);                                   // pin for RGB LED
  pinMode(blueLEDPin, OUTPUT);                                  // pin for RGB LED
  Serial.begin(115200);                                         
  Serial1.begin(115200);  
  pinMode(5, OUTPUT);                                           // pin selected for current consumption                                      
  pinMode(6, OUTPUT);                                           // pin selected for current consumption
  pinMode(39, OUTPUT);                                          // pin selected for current consumption
  pinMode(40, OUTPUT);                                          // pin selected for current consumption
  pinMode(41, OUTPUT);                                          // pin selected for current consumption
  giveCurrent(true);                                            // apply digital pins to give current (HIGH) in order to activate the sensors/modules
  Wire.begin();
}


void loop() {

  switch (state) {
    
    case 0:                                                         // starting point after uploading sketch for first time or after sleep interrupt
    
      Serial.println(F("starting proccess"));
      Serial.println();
      giveLight(0);                                                 // changing the color of LED depending on current state
      
      for (int i = 1; i <= 60; i++) {                                //  i <= 60   //1 minute given to Arduino in order to initalise (need 1min for dust module to warm up)
        delay(1000);
        Serial.print(i);
        Serial.println(F(" s (wait 1 minute for Arduino to initialize)"));
      }
      Serial.println();

      wdt_enable(WDTO_8S);                                          //enable watchdog for 8s in case SD configuration proccess stack   

      arrangeToSleep();                                             // arrange duration for next sleeping 

      wdt_reset();                                                  //reset watchdog from beginning

      SDFailed = checkSDFailure();                                  // check if SD is in place and working properly

      wdt_disable();                                                //disable watchdog

      delay(500);
      ethernetFailed = checkEthFailure(true);                       // check if Ethernet is working
      delay(500);
      wifiFailed = checkWifiFailure(true);                          // check if Wifi is working 

      state = 1;
      break;
      
    //***********
    
    case 1:                                                         // check point for modules functionality 

      Serial.println();
      Serial.println("-----------------------------------");
      Serial.println();

      if (ethernetFailed && wifiFailed && SDFailed) {               // all modules failed configuration
        state = 10;
      } else if (ethernetFailed && wifiFailed) {                    // only SD module passed configuration
        state = 2;
      } else if (!ethernetFailed) {                                 // Ethernet module passed configuration (don't care for now about Wifi, SD)
        state = 3;
      } else {                                                      // Wifi module passed configuration (Ethernet failed, don't care for now about SD)
        state = 4;                                                  
      }

      break;

    //***********
    
    case 2:                                                         // Ethernet and Wifi failed, going to take measures (if no measures previously taken) with unix timestamp and try to save them on SD
      
      Serial.println(F("Ethernet and Wifi failed\nTaking measures and try performing second SD initialization check"));

      if (checkIfMeasuresEmpty()) {                                 // check if measures array is empty
          takeMeasures(true);                                       // if empty take measures with unix timestamp
      } else {
          readTimeFromRTC();                                        // if not just concatenate unix timestamp
      }

      if (!RTCSyncFailed) {                                         // check if RTC synchronized the Time library, if false delete measures, terminate proccess and sleep Arduino

        wdt_enable(WDTO_8S); 
        
        if (!checkSDFailure()) {                                    // performing second SD module configuration check
  
          myFile = SD.open(txtopen, FILE_WRITE);                    // try open file 
  
          if (myFile) {                                             // if the file opened okay, write to it

            myFile.println(measures);
            myFile.close();

            giveLight(2);
            Serial.println(F("Measures saved in SD.\nTerminating proccess and sleep Arduino"));
            Serial.println();
            delay(2000);

            dataInSD = true;                                        // variable indicating that there are measures on SD from now on
            
          } else {
            SDErrorCase();
          }
  
        } else {
          SDErrorCase();
        }

        wdt_disable();

      } else {
        RTCErrorCase();
      }
        
      emptyMeasures();
      state = 12;

      break;

    //***********
    
    case 3:                                                         // Ethernet passed, going to take and send measures   -   if fail switch to Wifi

      Serial.println(F("From first check: Ethernet is working. Not care for Wifi and SD for now"));
      Serial.println(F("Taking measures and afterwards making a second Ethernet check: if passed send measures to server"));
      Serial.println();

      if (checkIfMeasuresEmpty()) {                     //taking measures without unix timestamp (if empty)
        takeMeasures(false);                             
      }

      if (!checkEthFailure(false)) {                    //check if passed second Ethernet check

        Serial.println(F("Second Ethernet check passed, sending measures"));
        Serial.println();
        sendHttpRequest(measures, 1);                   //make http request
        order = 1;                                      //specify the module
        state = 5;

      } else {                                          //error, switching to Wifi

        Serial.println(F("Ethernet connection error. Swiching to Wifi"));
        Serial.println();
        ethClient.stop();

        state = 4;
      }

      break;

    //***********

    case 4:                                                         // Wifi functionality, came here: if ethernet was not working or failed, going to take measures, make second Wifi check and send them to server   -   if fail switch to SD for saving measures

      Serial.println(F("Wifi functionality. Reasons to came here: if ethernet was not configured in beginning or failed in proccess"));
      Serial.println(F("Taking measures and afterwards making a second Wifi check: if passed send measures to server"));
      Serial.println();

      if (checkIfMeasuresEmpty()) {                      //taking measures without unix timestamp (if empty)
        takeMeasures(false);                             
      }

      if (!checkWifiFailure(false)) {                    //check if passed second Wifi check

        Serial.println(F("Second Wifi check passed, sending measures"));
        Serial.println();
        sendHttpRequest(measures, 2);                   //make http request
        order = 2;                                      //specify the module
        state = 5;

      } else {                                          //error, switching to Wifi

        Serial.println(F("Wifi connection error"));
        Serial.println();
        wifiClient.stop();

        if (SDSendAttempt) {                            // check if this try was about sending measures from SD, if true clear all values and go sleep

          SDMeasuresSendErrorCase();
          giveLight(0);
          SDSendAttempt = false;
          emptyMeasures();
          counter = 0;
          state = 12;

        } else {                                        // else go for saving in SD

          Serial.println(F("Going to check for SD and save the measures"));
          Serial.println();
          state = 2;
        }

      }

      break;

    //***********
    
    case 5:                                                         // server response 
      
      takeResponse(order);
      Serial.println(response);
      Serial.println();
      
      if (strstr(response, "200 OK")) {                       // successful http response

        giveLight(5);
        Serial.println(F("Server response status 200 - measures saved to web app successfully"));
        Serial.println();
        delay(2000);
        giveLight(0);

        if (backUpCounter > 0) {                              // check if backup measure detected

          Serial.println(F("Backup measure detected. Needs to be deleted before procceed"));
          delay(500);
          myFile = SD.open(txtopen, FILE_WRITE);

          if (myFile) {                                       // check if file opened, if true, delete backup measure 
            myFile.seek(0);
            for (int l = 0; l <= backUpCounter ; l++) {
              myFile.write(' ');
            }
          
            myFile.close();
            Serial.println(F("Backup measure deleted successfully"));
            backUpCounter = 0;

          } else {                                            // file failed to open, clear all values and go sleep
            
            giveLight(10);
            Serial.println(F("error opening measures.txt from SD\nFailed deleting backup measure, terminating proccess and sleep Arduino"));
            Serial.println();
            delay(2000);

            SDSendAttempt = false;
            counter = 0;
            emptyMeasures();
            emptyResponse();
            order = 0;
            state = 12;
            break;
          }

        }

        if (SD.exists(txtopen)) {                             // check if SD has measures (useful when reset is clicked and default settings are set - in this situation Arduino thinks that SD holds no measures)
          myFile = SD.open(txtopen);
          if (myFile) {                                       // attempt open file
            while (myFile.available()) {                      // check if SD has measures, if true continues operation
              if (myFile.read() != ' ')  {
                dataInSD = true;
                checkIfTxtEmpty = false;
              }
            }
            myFile.close();

            if(checkIfTxtEmpty){                              // if no measures found clear values and go sleep         
              dataInSD = false;
              counter = 0;
              SDSendAttempt = false;
              emptyMeasures();
              emptyResponse();
              order = 0;
              state = 12;
              Serial.println(F("No measures found to SD\nTerminating proccess and sleep Arduino"));
              SD.remove(txtopen);
              break;
            }

          } else {                                            // cant open file
            
            if(SDSendAttempt){                                // check if this try was about sending measures from SD, if true clear all values, keep counter off current measure in order to delete it in the next try and go sleep
              
              SDDeleteMeasuresSentErrorCase();
              giveLight(0);
              backUpCounter = counter;
              SDSendAttempt = false;
              counter = 0;
            } else {

              SDErrorCase();
              giveLight(0);
            }
            emptyMeasures();
            emptyResponse();
            order = 0;
            state = 12;
            break;
          }
        } else {                                              // going to sleep Arduino

          Serial.println(F("Terminating proccess and sleep Arduino"));
          Serial.println();
        }

        if (SDSendAttempt) {                                  // check if this try was about sending measures from SD, if true clear current measure from SD and check if exist other measures in SD

          Serial.println(F("Measures from SD card sent!"));
          Serial.println();

          SDSendAttempt = false;
          delay(500);

          myFile = SD.open(txtopen, FILE_WRITE);

          if (myFile) {                                       // if file opened, delete measure from SD
            myFile.seek(0);
            for (int l = 0; l <= counter ; l++) {
              myFile.write(' ');
            }
            
            myFile.close();
            Serial.println(F("Deleted measures record, that has been sent"));
            Serial.println();
            stillSDHasData = false;
          } else {                                            // failed open SD, keep counter off current measure in order to delete it in the next try and go sleep

            SDDeleteMeasuresSentErrorCase();
            giveLight(0);
            backUpCounter = counter;
            counter = 0;
            emptyMeasures();
            emptyResponse();
            order = 0;
            state = 12;
            break;
          }

          myFile = SD.open(txtopen);

          if (myFile) {                                       // check if there are still measures, existing in SD

            while (myFile.available()) {

              if (myFile.read() != ' ') {
                stillSDHasData = true;
              }

            }
           
            myFile.close();

          } else {                                            // failed open SD, clear values and go sleep
            
            SDErrorCase();
            giveLight(0);

            counter = 0;
            emptyMeasures();
            emptyResponse();
            order = 0;
            state = 12;
            break;
          }

          if (!stillSDHasData) {                              // if found out that there are no measures that exist in SD, clear all values and go sleep
            dataInSD = false;
            counter = 0;
            Serial.println(F("All measures sent from SD, deleting file\nTerminating proccess and sleep Arduino"));
            Serial.println();
            SD.remove(txtopen);
          }

        }

        if (dataInSD) {                                       // check if still data in SD, if true grab next measure 

          wdt_enable(WDTO_8S); 

          if (!checkSDFailure()) {

            wdt_disable();

            order = 0;
            emptyMeasures();
            emptyResponse();
            counter = 0;

            myFile = SD.open(txtopen);
            if (myFile) {

              String buffer;

              while (myFile.available()) {
                buffer = myFile.readStringUntil('\n');
                break;
              }
            
              counter = buffer.length();
              
              buffer.trim();

              Serial.println(buffer);
              Serial.println();
              
              buffer.toCharArray(measures, 110);
          
              // close the file:
              myFile.close();

              
              SDSendAttempt = true;
              state = 3;
              break;


            } else {

              SDErrorCase();
              giveLight(0);
            }
          } else {
            
            wdt_disable(); 

            SDErrorCase();
            giveLight(0);

            stillSDHasData = false;
            counter = 0;
          }
        }

      } else {                                                // not successful http response. Possibly server error, no reason to try again with different module

        giveLight(6);
        Serial.println(F("Problem. Not receiving successful http response"));
        Serial.println();
        delay(2000);
        giveLight(0);

        if (SDSendAttempt) {                                        // check if this try was about sending measures from SD, if true clear all values and sleep

          SDMeasuresSendErrorCase();
          giveLight(0);
          SDSendAttempt = false;
          counter = 0;

        } else {                                                    // else go for saving in SD     
          
          Serial.println(F("Switching to SD for saving measures"));
          Serial.println();
          state = 2;
          order = 0;
          emptyResponse();
          break;
        }
      }

      emptyMeasures();
      emptyResponse();
      order = 0;
      state = 12;

      break;

    //***********
    
    case 10:                                                        // all modules failed on configuration, going to sleep Arduino

      giveLight(10);
      Serial.println(F("\nEthernet, Wifi and SD error.\nTerminating proccess and sleep Arduino"));
      Serial.println();
      delay(2000);
      state = 12;

      break;

    //***********
    
    case 12:                                                        // arrange interrupt on Arduino, going to sleep, waking up and initialize default values on variables                                          
      
      giveLight(12);          
      delay(3000);
      giveLight(13);

      attachInterrupt(0, wakeUp, LOW);                        // Allow wake up pin to trigger interrupt on low

      giveCurrent(false);                                     // apply pins to stop giving current (LOW) in order to reduce consumption
       
      LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);    // Enter power down state with ADC and BOD module disabled, wake up when wake up pin is low
      
      detachInterrupt(0);                                     // Disable external pin interrupt on wake up pin

      Serial.println(F("wake up and start from beginning"));
      Serial.println();
      Serial.print(F("unix time is: "));
      Serial.println(RTC.get());
      Serial.println();
      Serial.println("-----------------------------------");
      Serial.println();
    
      state = 0;                                               //initalise default values
      ethernetFailed = false;   
      SDFailed = false;
      RTCSyncFailed = false;
      wifiFailed = false;
      status = WL_IDLE_STATUS;
      checkIfTxtEmpty = true;
     
      break;

  }

}


void giveLight(byte currentState) {                                             // function that triggers RGB LED light and its color from given state

  switch(currentState){

    case 0:                                                                   // almost white: state after wake up and stand by between actions
      analogWrite(redLEDPin, 20);
      analogWrite(greenLEDPin, 255);
      analogWrite(blueLEDPin, 40);
    break;

    case 2:                                                                   // pink: state where measures saved in SD
      analogWrite(redLEDPin, 190);
      analogWrite(greenLEDPin, 111);
      analogWrite(blueLEDPin, 40);
    break;

    case 5:                                                                   // green: state where measures successfully sent and saved to web app
      analogWrite(redLEDPin, 0);
      analogWrite(greenLEDPin, 255);
      analogWrite(blueLEDPin, 0);
    break;

    case 6:                                                                   // purple: state where measures sent and server response was unsuccessful
      analogWrite(redLEDPin, 80);
      analogWrite(greenLEDPin, 0);
      analogWrite(blueLEDPin, 80);
    break;

    case 10:                                                                  //red: state where all modules failed
      analogWrite(redLEDPin, 255);
      analogWrite(greenLEDPin, 0);
      analogWrite(blueLEDPin, 0);
    break;

    case 12:                                                                  // blue: last active state before sleeping
      analogWrite(redLEDPin, 0);
      analogWrite(greenLEDPin, 0);
      analogWrite(blueLEDPin, 255);  
    break;

    case 13:                                                                  // inactive: switching off before sleeping (close LED)
      analogWrite(redLEDPin, 0);
      analogWrite(greenLEDPin, 0);
      analogWrite(blueLEDPin, 0);
    break;
  }
}

void giveCurrent(bool answer) {                                                 // function that controls current flow to modules/sensors

  if(answer){
    digitalWrite(5, HIGH);
    digitalWrite(6, HIGH);
    digitalWrite(39, HIGH);
    digitalWrite(40, HIGH);
    digitalWrite(41, HIGH);
  } else {
    digitalWrite(5, LOW);
    digitalWrite(6, LOW);
    digitalWrite(39, LOW);
    digitalWrite(40, LOW);
    digitalWrite(41, LOW);
  }
  
}

void arrangeToSleep(){                                                          // function that enables sleep arrangement
  RTC.setAlarm(ALM1_MATCH_DATE, 0, 0, 0, 1);                        
  RTC.alarm(ALARM_1);
  RTC.alarmInterrupt(ALARM_1, false);                                          // disables alarm 1
  RTC.squareWave(SQWAVE_NONE);                                                 // disables the square wave output
  //set Alarm 1 to occur at 5 seconds after every minute
  // RTC.setAlarm(ALM1_MATCH_SECONDS, 5, 0, 0, 0);
  RTC.setAlarm(ALM1_MATCH_MINUTES, 5, changeSleepTime, 0, 0);                  // set alarm 1 to occur at 5 seconds after every half an hour, starting from next o'clock hour
  changeSleepTime = (changeSleepTime == 0) ? 30 : 0 ;                          // variable that holds sleep arrangement for next time
  RTC.alarmInterrupt(ALARM_1, true);                                           // enable interrupt output for alarm 1
}
void wakeUp(){                                                                  // just a handler for the pin interrupt (needed for sleep interrupt)
  giveCurrent(true);                                           // apply digital pins to give current (HIGH) in order to activate the sensors/modules
}                                                                 



bool checkSDFailure() {                                                         // check SD Initialization

  delay(500);

  Serial.print(F("Initializing SD card..."));

  if (!SD.begin(sd)) {
    Serial.println(F("SD Initialization error"));
    Serial.println();
    return true;

  } else {
    Serial.println(F("SD Initialization done"));
    Serial.println();
    return false;
  }

}

bool checkEthFailure(bool closeConnection) {                                    // check Ethernet module initialization, connection and server response

  delay(500);

  if (Ethernet.begin(mac) == 0) {                                       // start configuring Ethernet using DHCP                      
    Serial.println(F("Failed to configure Ethernet using DHCP"));
    Ethernet.begin(mac, ip);                                            // try to congifure using IP address instead of DHCP
  }

  delay(1000);                                                          // give the Ethernet shield a second to initialize

  if (ethClient.connect(server, 80) == 1) {                             // check for connection status
    Serial.println(F("Ethernet connection established"));
    Serial.println();
     if (closeConnection) ethClient.stop();
    return false;

  } else {                                                              // connection status error
    Serial.println(F("Ethernet connection error"));
    Serial.println();
     if (closeConnection) ethClient.stop();
    return true;
  }

}

bool checkWifiFailure(bool closeConnection) {                                   // check Wifi module initialization, connection to network and server response
  
  bool passed = false;                                              //variable needed for keeping state of proccess
  unsigned long startTime;
  unsigned long deadtime_ms = 15000;
  WiFi.init(&Serial1);

  delay(500);

  if (WiFi.status() == WL_NO_SHIELD) {                              // check for the presence of the shield
    Serial.println(F("WiFi shield not present"));
    Serial.println();
    return true;
  }

  Serial.print(F("Attempting to connect to Wifi network with given credentials"));
  Serial.println();
  startTime = millis();

  while ((millis()-startTime) <= deadtime_ms) {                   // attempt to connect to WiFi network within given time (15 seconds)
    status = WiFi.begin(ssid, pass);                            
    if(status == WL_CONNECTED){
      passed = true;                                              // successfully connect to WPA/WPA2 network
      break;
    }
  }

  if(!passed) return true;                                        // if not connected to wifi network return error

  Serial.println(F("You're connected to the network"));
  Serial.println();
  if (wifiClient.connect(server, 80) == 1) {                      // check connection status
    Serial.println(F("Wifi connection established"));
    Serial.println();
    if (closeConnection) wifiClient.stop();
    return false;
  } else {                                                        // connection status error
    Serial.println(F("Wifi connection error"));
    Serial.println();
    if (closeConnection) wifiClient.stop();
    return true;
  }
}



void sendHttpRequest(char measures[110], byte moduleOrder) {                    // make a HTTP request

  wdt_enable(WDTO_8S);

  if(moduleOrder == 1){                                 //check which module makes the request (Ethernet or Wifi)
    ethClient.print("POST /example/measures");
    ethClient.println(" HTTP/1.1");
    ethClient.println("Host: example.com");
    ethClient.println("Connection: close");
    ethClient.println("Content-Type: application/x-www-form-urlencoded;");
    ethClient.print("Content-Length: ");
    ethClient.println(strlen(measures));
    ethClient.println();
    ethClient.println(measures);

  } else {
    wifiClient.print("POST /example/measures");
    wifiClient.println(" HTTP/1.1");
    wifiClient.println("Host: example.com");
    wifiClient.println("Connection: close");
    wifiClient.println("Content-Type: application/x-www-form-urlencoded;");
    wifiClient.print("Content-Length: ");
    wifiClient.println(strlen(measures));
    wifiClient.println();
    wifiClient.println(measures);
  }

  wdt_disable(); 
}

void takeResponse(byte moduleOrder) {                                           //take server response and save it to array

  byte j = 0;
  delay(1000);                                         //**** important

  if(moduleOrder == 1){

    while (ethClient.available()) {                   // read server's response and save it to array
      char c = ethClient.read();
      
      if (j < 20) {
        strncat(response, &c, 1);         
        j++;
      }
    }

    if (!ethClient.connected()) {                      // if the server's disconnected, stop the connection
      ethClient.stop();
    }

  } else {

    while (wifiClient.available()) {                   // read server's response and save it to array
      char c = wifiClient.read();
      
      if (j < 20) {
        strncat(response, &c, 1);         
        j++;
      }
    }

    if (!wifiClient.connected()) {                      // if the server's disconnected, stop the connection
      wifiClient.stop();
    }
  }
  
  Serial.println(F("Server response sent back - going to compare http response status"));
  Serial.println();
}



void takeMeasures(bool takeTime) {                                              //take measures

  byte uv;                      //UVindex
  byte rain;                    //Rain
  float dust;                   //Dust
  float temp, hum, pres;        //BME
  char exm [20];         

  //UV
  uv = readUVSensor();          //   -   0-12 UVIndex
  Serial.print(F("uv "));
  Serial.println(uv);

  //Rain
  rain = readRainSensor();      //   -    0 => It's raining     1 => It's drizzling        3 => Not raining
  Serial.print(F("rain "));
  Serial.println(rain);

  //Dust
  dust = readDustSensor();      //   -    float number (particales / 0.01 cubic feet)
  Serial.print(F("dust concentration = "));
  Serial.print(dust);
  Serial.print(F(" pcs/0.01cf  -  "));
  if (dust < 1.0) Serial.println(F("It's a smokeless and dustless environment")); 
  if (dust > 1.0 && dust < 20000) Serial.println(F("It's probably only you blowing air to the sensor :)"));
  if (dust > 20000 && dust < 315000) Serial.println(F("Smokes from matches detected!"));
  if (dust > 315000) Serial.println(F("Smokes from cigarettes detected! Or It might be a huge fire! Beware!"));
  

  //Temperature Hummidity Pressure (BME sensor)
  BME280I2C bme;
  bme.begin();
  delay(1500);
  bme.read(pres, temp, hum);      //   -   float temperature(°C), humidity(%), pressure(Pa)
  Serial.print(F("temp "));
  Serial.println(temp);  
  Serial.print(F("hum "));
  Serial.println(hum);  
  Serial.print(F("press "));
  Serial.println(pres);  


  //build measures string
  strcpy(measures, "unique=big");
  
  strcat(measures, "&uv=");
  (uv < 10) ? dtostrf(uv, 1, 0, exm) : dtostrf(uv, 2, 0, exm);
  strcat(measures, exm);
  
  strcat(measures, "&rain=");
  dtostrf(rain, 1, 0, exm);
  strcat(measures, exm);
  
  strcat(measures, "&dust=");
  if (dust < 10) {
    dtostrf(dust, 1, 1, exm);
  }else if (dust < 100) {
    dtostrf(dust, 2, 1, exm);
  }else if (dust < 1000) {
    dtostrf(dust, 3, 1, exm);
  }else if (dust < 10000) {
    dtostrf(dust, 4, 1, exm);
  }else if (dust < 100000) {
    dtostrf(dust, 5, 1, exm);
  }
  strcat(measures, exm);
  
  strcat(measures, "&temperature=");
  (temp < 10) ? dtostrf(temp, 1, 1, exm) : dtostrf(temp, 2, 1, exm);
  strcat(measures, exm);
  
  strcat(measures, "&humidity=");
  (hum < 100) ? dtostrf(hum, 2, 1, exm) : dtostrf(hum, 3, 1, exm);
  strcat(measures, exm);
 
    strcat(measures, "&pressure=");
    (pres < 1000) ? dtostrf(pres, 3, 1, exm) : dtostrf(pres, 4, 1, exm);
    strcat(measures, exm);

  if (takeTime) readTimeFromRTC();


}


byte readUVSensor() {                                                           //uv implementation
  
  byte UVIndex;
  int sensorValue = 0;

  sensorValue = analogRead(0);                                        //connect UV sensor to Analog 0
  int voltage = (sensorValue * (5.0 / 1023.0)) * 1000;                //Voltage in miliVolts

  if (voltage < 50)
  {
    UVIndex = 0;
  } else if (voltage > 50 && voltage <= 227)
  {
    UVIndex = 0;
  } else if (voltage > 227 && voltage <= 318)
  {
    UVIndex = 1;
  }
  else if (voltage > 318 && voltage <= 408)
  {
    UVIndex = 2;
  } else if (voltage > 408 && voltage <= 503)
  {
    UVIndex = 3;
  }
  else if (voltage > 503 && voltage <= 606)
  {
    UVIndex = 4;
  } else if (voltage > 606 && voltage <= 696)
  {
    UVIndex = 5;
  } else if (voltage > 696 && voltage <= 795)
  {
    UVIndex = 6;
  } else if (voltage > 795 && voltage <= 881)
  {
    UVIndex = 7;
  }
  else if (voltage > 881 && voltage <= 976)
  {
    UVIndex = 8;
  }
  else if (voltage > 976 && voltage <= 1079)
  {
    UVIndex = 9;
  }
  else if (voltage > 1079 && voltage <= 1170)
  {
    UVIndex = 10;
  } else if (voltage > 1170)
  {
    UVIndex = 11;
  }
  return UVIndex;
}

byte readRainSensor() {                                                         //rain implementation
  
  int sensorReading;            //Rain
  byte range;
  byte sensorMin = 0;           // Rain sensor minimum
  int sensorMax = 1024;         // Rain sensor maximum
  
  sensorReading = analogRead(A1);
  range = map(sensorReading, sensorMin, sensorMax, 0, 3);
  return range;
}

float readDustSensor() {                                                        //dust implementation
  
  unsigned long duration;
  unsigned long startTime;
  unsigned long sampletime_ms = 30000;                                   //time to take measure  (recommended 30000 -> 30sec)
  unsigned long lowpulseoccupancy = 0;
  float ratio = 0;
  float concentration = 0;


  startTime = millis();                                                 //get the current time;

  do {
    duration = pulseIn(7, LOW);
    lowpulseoccupancy = lowpulseoccupancy+duration;
    
  } while( (millis()-startTime) <= sampletime_ms );

  ratio = lowpulseoccupancy/(sampletime_ms*10.0);                       // Integer percentage 0=>100
  concentration = 1.1*pow(ratio,3)-3.8*pow(ratio,2)+520*ratio+0.62;     // using spec sheet curve
  return concentration;  
}

void readTimeFromRTC() {                                                        //unix timestamp implementation

  char exm [20];
  time_t unix;    

  wdt_enable(WDTO_8S);                         

  setSyncProvider(RTC.get);             // get the time from RTC and try to synchronize with the Time library
  if(timeStatus() != timeSet) {         // check if previous synchonization passed
      Serial.println(F("Unable to sync with the RTC\nDeleting measures, terminating proccess and sleep Arduino"));
      RTCSyncFailed = true;
  } else {                              // synchonization passed
      Serial.println(F("RTC has set the system time"));

      unix = RTC.get();

      Serial.print(F("Unix time is: "));
      Serial.println(unix);
      Serial.println();

      strcat(measures, "&time=");   
      ultoa (unix, exm, 10);
      strcat(measures, exm);

      Serial.println(F("The whole string that is going to be saved is: "));
      Serial.println(measures);
      Serial.println();
  }
  wdt_disable();

}




bool checkIfMeasuresEmpty() {                                                   // general function for checking if measures array is empty

  return (strlen(measures) == 0) ? true : false;
}

void emptyMeasures() {                                                          // general function for clearing measures array
                                                          
  memset(measures, 0, sizeof(measures));
}

void emptyResponse() {                                                          // general function for clearing response array

  memset(response, 0, sizeof(response));
}

void SDErrorCase() {                                                            // general error function for SD

  giveLight(10);
  Serial.println(F("\nAn error occured while initialising SD card.\nTerminating proccess and sleep Arduino"));
  Serial.println();
  delay(2000);
}

void SDMeasuresSendErrorCase() {                                                // general error function for sending SD measures 
  giveLight(10);
  Serial.println(F("Problem occurred while sending measures, retreived from SD card.\nTerminating proccess and sleep Arduino"));
  Serial.println();
  delay(2000);
}

void SDDeleteMeasuresSentErrorCase() {                                          // general error function for deleting SD measures that was sent
  giveLight(10);
  Serial.println(F("error opening measures.txt from SD\nBackup measure that was sent, in order to delete it after SD confugiration fix, terminating proccess and sleep Arduino"));
  Serial.println();
  delay(2000);
}

void RTCErrorCase() {                                                           // general error function for RTC

  giveLight(10);
  Serial.println(F("\nAn error occured while initialising RTC clock module.\nTerminating proccess and sleep Arduino"));
  Serial.println();
  delay(2000);
}