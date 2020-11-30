/*
   Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleScan.cpp
   Ported to Arduino ESP32 by Evandro Copercini
   Adapted by Wilfredo Abudeye
*/

//Includes for beacon + app
#include <BLEDevice.h>
#include <BLEUtils.h>

//Includes only for BEACONSCAN
#include <BLEScan.h>
#include <BLEUUID.h>
#include <BLEAdvertisedDevice.h>
#include "BLEBeacon.h"

//Includes for only APP
#include <BLEServer.h>
#include <BLE2902.h>

//Includes for only RTC
#include <DS3231.h>
#include <Wire.h>

//Includes for External Wakeup
#define BUTTON_PIN_BITMASK 0x300000000

//define for BEACONSCAN
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  1        /* Time ESP32 will go to sleep (in seconds) */
//define for APP
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

//Variables for BEACONSCAN
    //Blink data below
const int CUTOFF = 69;          //dB of signal intensity
    //Blink Data above
BLEScan* pBLEScan;
int scanTime = 2;               //In seconds
int scanCounter = 0;            //Count the amount of comparisons and scans done
uint16_t SCANS =10 ;
int loop1 = 0;    //For delayed turnoff
//static BLEUUID aa;
std::string strServiceUUID = "0000fefd-0000-1000-8000-00805f9b34fb";
//Variables for APP
BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
bool searchPhone = false;
float txValue = 0;
const int readPin = 0;          // Use GPIO number. See ESP32 board pinouts
const int LED = 25;             // Could be different depending on the dev board. I used the DOIT ESP32 dev board.
uint16_t button;                //Temporal variable to be replaced by the button readings
uint16_t button_sleep;          //Temporal variable to be replaced by the button readings
RTC_DATA_ATTR uint16_t flag = 0;//To count the amount of times that the APP setup is performed
std::string rxValue;            // Could also make this a global var to access it in loop()
int APP_connected_once = 0;
int minutes_waiting_app_response = 2;

//Variables for RTC
DS3231 Clock;
byte Year;
byte Month;
byte Date;
byte DoW;
byte Hour;
byte Minute;
byte Second;
bool h12;
bool PM;

//Variables for External Wakeup
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR byte Hour_Wake;
RTC_DATA_ATTR byte Minute_Wake;
RTC_DATA_ATTR byte Second_Wake;
RTC_DATA_ATTR byte Hour_Sleep;
RTC_DATA_ATTR byte Minute_Sleep;
RTC_DATA_ATTR byte Second_Sleep;


//Class MyServerCallbacks for APP
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};
//Class MyCallbacks for APP
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      rxValue = pCharacteristic->getValue();

      if (rxValue.length() > 0) {
        Serial.println("*********");
        Serial.print("Received Value: ");

        for (int i = 0; i < rxValue.length(); i++) {
          Serial.print(rxValue[i]);
        }

        Serial.println();

        // Do stuff based on the command received from the app
        if (rxValue.find("A") != -1) { 
          Serial.print("Turning ON!");
          digitalWrite(LED, HIGH);
        }
        else if (rxValue.find("B") != -1) {
          Serial.print("Turning OFF!");
          digitalWrite(LED, LOW);
        }
        else if (rxValue.find("x") != -1) 
        {//System time has been found
          Serial.print("Found the x -- Found the date");
          GetDateStuff(Year, Month, Date, DoW, Hour, Minute, Second,rxValue); //Discover System time to information
          set_Date();//Will set date and time to RTC
        }
        else if (rxValue.find("TON") != -1) 
        {//System time has been found
          Serial.print("Found the TON -- Time to wake up\n");
          GetTimeStuffAlarms(Hour_Wake, Minute_Wake, Second_Wake, rxValue);
        }
        else if (rxValue.find("TOFF") != -1) 
        {//System time has been found
          Serial.print("Found the TOFF -- Time to SLEEP\n");
          GetTimeStuffAlarms(Hour_Sleep, Minute_Sleep, Second_Sleep, rxValue) ;
          set_Alarm1(Hour_Sleep, Minute_Sleep, Second_Sleep );//Sleep 2 minutes
        }
        Serial.println();
        Serial.println("*********");
      }
    }
};


//Class MyAdvertisedDeviceCallbacks for BEACONSCAN
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks 
{
    void onResult(BLEAdvertisedDevice advertisedDevice) 
    {
         scanCounter++;
         int int_device_RSSI = advertisedDevice.getRSSI();

         if(abs(int_device_RSSI)<CUTOFF)
         {
           Serial.printf("\nA BLE device signal strength is close enough");
           if(advertisedDevice.haveServiceUUID()&&(advertisedDevice.getServiceUUID().toString()==strServiceUUID))
           {
             Serial.printf("\nYou have found the Doggomatic Collar Tag");
             //ADD THIRD LAYER for manufacturer number if not changing with time
             Serial.printf("\nThe RSSI of this device is:%d****\n", int_device_RSSI);
             Serial.printf(" Advertised Device: %s \n\n", advertisedDevice.toString().c_str());
             digitalWrite(LED, HIGH);
             loop1=0;
           }
         }
         else if(scanCounter==SCANS)
         {
           Serial.printf("Device not found anywhere, waiting...");
           loop1++;
           if((loop1>1) && digitalRead(LED)==HIGH)
           {
             digitalWrite(LED, LOW);
             Serial.printf("Device not found, Turn OFF");
             loop1=0;
           }
         }
         Serial.printf("*");
//            else
//            {
//              Serial.printf("Device not close enough, scan #: %d\n", scanCounter);
//            }

    }
};

void SCAN_TAG()
{
  BLEScanResults foundDevices = pBLEScan->start(scanTime);
  SCANS=foundDevices.getCount();
  //digitalWrite(PIN, LOW);
  scanCounter = 0;
  Serial.printf("\nScan done! Devices found: %d\n",foundDevices.getCount());
  
  //Code for deep sleep
  if(digitalRead(LED)==LOW)
  {
    //delay(50);
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    //esp_deep_sleep_start();//Activate after presentation
    Serial.println("Here is where deep sleep will be");
    
  }
  else
  {
    delay(700);
  }
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

}

void SCAN_APP()
{
    delay (1000);
    if (deviceConnected) 
    {
    APP_connected_once = 1;                // Rise flag to show one connection has been at least established.
    // Fabricate some arbitrary junk for now...
    txValue = analogRead(readPin) / 3.456; // This could be an actual sensor reading!

                                           // Let's convert the value to a char array:
    char txString[8];                      // make sure this is big enuffz
    dtostrf(txValue, 1, 2, txString);      // float_val, min_width, digits_after_decimal, char_buffer
    
//    pCharacteristic->setValue(&txValue, 1); // To send the integer value
//    pCharacteristic->setValue("Hello!"); // Sending a test message
    pCharacteristic->setValue(txString);
    
    pCharacteristic->notify();             // Send the value to the app!
    Serial.print("*** Sent Value: ");
    Serial.print(txString);
    Serial.println(" ***");
                                           // You can add the rxValue checks down here instead
                                           // if you set "rxValue" as a global var at the top!
                                           // Note you will have to delete "std::string" declaration
                                           // of "rxValue" in the callback function.
    if (rxValue.find("A") != -1) { 
      Serial.println("Turning ON!");
      digitalWrite(LED, HIGH);
    }
    else if (rxValue.find("B") != -1) 
    {
      Serial.println("Turning OFF!");
      digitalWrite(LED, LOW);
    }

    Clock.checkIfAlarm(2);//To clear the byte
    }
    
    else if ((!deviceConnected)&&(APP_connected_once==1))
    {
      APP_connected_once=0; //lower flag for future connections
      //flag =0; //Reset flag to go back 
      searchPhone=false;//Change a value to stop looking for the phone on main loop.
      button=0;//Temporal code while button is being placed
      Serial.println("PHONE HAS DISCONNECTED, searching for Dog Collar");
      Clock.checkIfAlarm(2);//To clear the byte
      //we can reuse alarm1 to new value of when the dog tag should wake up or go back to sleep
    }
    else if ((!deviceConnected)&&(Clock.checkIfAlarm(2)))//The two represents the minutes to wait
    {
      //flag =0;
      searchPhone=false;//Change a value to stop looking for the phone on main loop. 
      button=0;//Temporal code while button is being placed
      Serial.println("No Phone connection established, searching for Dog Collar");
    }
    delay(1000);
}

void SCAN_APP_SETUP()
{
  //Serial.begin(115200);

  pinMode(LED, OUTPUT);
    // Create the BLE Device
  BLEDevice::init("ESP32 UART Test"); // Give it a name
    // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_TX,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
                      
  pCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID_RX,
                                         BLECharacteristic::PROPERTY_WRITE
                                       );

  pCharacteristic->setCallbacks(new MyCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  pServer->getAdvertising()->start();
  Serial.println("Waiting a client connection to notify (Connecting to Mobile Application)...");
  Clock.checkIfAlarm(2);//To clear the byte
  set_Alarm2(1);//Wait one minutes, if no connection found then search for dog tag ++ CODE TO COUNT
}

void GetTimeStuffAlarms(byte& Hour, byte& Minute, byte& Second, std::string rxValue) 
{
  // Call this if you notice something coming in on 
  // the serial port. The stuff coming in should be in 
  // the order YYMMDDwHHMMSS, with an 'x' at the end.
  boolean GotString = false;
  char InChar;
  byte Temp1, Temp2;
  char InString[20];

  byte j=0;
  // now Hour
  Temp1 = (byte)rxValue[0] -48;
  Temp2 = (byte)rxValue[1] -48;
  Hour = Temp1*10 + Temp2;
  Serial.println(Hour);
  // now Minute
  Temp1 = (byte)rxValue[3] -48;
  Temp2 = (byte)rxValue[4] -48;
  Minute = Temp1*10 + Temp2;
  Serial.println(Minute);
  // now Second
  Temp1 = (byte)rxValue[6] -48;
  Temp2 = (byte)rxValue[7] -48;
  Second = Temp1*10 + Temp2;
  Serial.println(Second);
}

//Void for RTC GET DATE
void GetDateStuff(byte& Year, byte& Month, byte& Day, byte& DoW, 
    byte& Hour, byte& Minute, byte& Second, std::string rxValue) {
  // Call this if you notice something coming in on 
  // the serial port. The stuff coming in should be in 
  // the order YYMMDDwHHMMSS, with an 'x' at the end.
  
  //*******MAY NEED TO ERAESE SOME VARIABLES ON CLEANUP
  boolean GotString = false;
  char InChar;
  byte Temp1, Temp2;
  char InString[20];

  byte j=0;
  // Read Year first
  Temp1 = (byte)rxValue[0] -48;
  Temp2 = (byte)rxValue[1] -48;
  Year = Temp1*10 + Temp2;
  Serial.println(Year);
  // now month
  Temp1 = (byte)rxValue[2] -48;
  Temp2 = (byte)rxValue[3] -48;
  Month = Temp1*10 + Temp2;
  Serial.println(Month);
  // now date
  Temp1 = (byte)rxValue[4] -48;
  Temp2 = (byte)rxValue[5] -48;
  Day = Temp1*10 + Temp2;
  Serial.println(Day);
  // now Day of Week
  DoW = (byte)rxValue[6] - 48;   
  // now Hour
  Temp1 = (byte)rxValue[7] -48;
  Temp2 = (byte)rxValue[8] -48;
  Hour = Temp1*10 + Temp2;
  Serial.println(Hour);
  // now Minute
  Temp1 = (byte)rxValue[9] -48;
  Temp2 = (byte)rxValue[10] -48;
  Minute = Temp1*10 + Temp2;
  Serial.println(Minute);
  // now Second
  Temp1 = (byte)rxValue[11] -48;
  Temp2 = (byte)rxValue[12] -48;
  Second = Temp1*10 + Temp2;
  Serial.println(Second);
}

void set_Date()
{
    //GetDateStuff(Year, Month, Date, DoW, Hour, Minute, Second, rxValue);//Already done in line checkup

    Clock.setClockMode(false);  // set to 24h
    //setClockMode(true); // set to 12h

    Clock.setYear(Year);
    Clock.setMonth(Month);
    Clock.setDate(Date);
    Clock.setDoW(DoW);
    Clock.setHour(Hour);
    Clock.setMinute(Minute);
    Clock.setSecond(Second);
}

void set_Alarm1(byte Hour_Temp, byte Minute_Temp, byte Second_Temp )
//"a" represents the amount of minutes to wait before ending connection and look for collar tag
{//This will give the user 2 minutes in which the phone will be scanned
    // set A2 to two minutes past, on current day of month.
    Clock.setA1Time(Clock.getDoW(), Hour_Temp, Minute_Temp, Second_Temp, 0x0, true, 
      false, false);
      Serial.println("Alarm 1 has been set! It will also turn on");
    // Turn on alarms, with external interrupt
    Clock.turnOnAlarm(1); //SQW on DS3231 will provide the queue 
}
void set_Alarm2(int minutes_to_wait)//"a" represents the amount of minutes to wait before ending connection and look for collar tag
{//This will give the user 2 minutes in which the phone will be scanned
    // set A2 to two minutes past, on current day of month.
    Clock.setA2Time(Clock.getDate(), Clock.getHour(h12,PM), Clock.getMinute()+minutes_to_wait, 0x0, false, 
      false, false);
      Serial.println("Alarm 2 has been set! It will not turn on");
    // Turn on alarms, with external interrupt
    //Clock.turnOnAlarm(2); //SQW on DS3231 will provide the queue NOT NECESSARY
}




void Check_Alarms()
{
  if (Clock.checkIfAlarm(1)) //This alarm will give the indication to go back to sleep
    {
      Serial.println(" A1! WE are setting alarm to wake up---Inside Check Alarms");
      Serial.println(digitalRead(4));
      set_wake_and_sleep();
    }
}

void set_wake_and_sleep(){
      set_Alarm1(Hour_Wake, Minute_Wake, Second_Wake);//Sleep 2 minutes //This is a wakeup alarm
      //Set new value for the alarm to know when to wake up
        if(digitalRead(32))
          {
            esp_sleep_enable_ext0_wakeup(GPIO_NUM_4,0); //1 = High, 0 = Low
            Serial.println(" Found 1, wake on 0---Inside Check Alarms");
          }
        else if(digitalRead(32)==0)
          {
            esp_sleep_enable_ext0_wakeup(GPIO_NUM_4,1); //1 = High, 0 = Low
            Serial.println(" Found 0, wake on 1---Inside Check Alarms");
          }
      delay(3000);
      esp_deep_sleep_start();//Puasing momentarily
     
}

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}
void setup() 
{
  Serial.begin(115200);
  Serial.println("Scanning...");
  delay(500);
  pinMode(LED, OUTPUT);
  // Create the BLE Device for APP
  BLEDevice::init("ESP32 UART Test"); // Give it a name
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  
  //Now the code is set to allow deep sleep and time set to the Alarms
  if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT0)//CHECK WHICH ONE IS CAUSED BY ALARM
  {
      Serial.println(" A1! We are setting time to sleep--Inside Setup");
      Clock.checkIfAlarm(1);//Clear Flag
      if((Clock.getHour(h12,PM)<= Hour_Wake)&&(Clock.getMinute()< Minute_Wake))
      {//Check what to do if the deep sleep is interrupted by button in the middle of the sleep to connect to phone and then the connection ends, 
        Serial.println(" I was not supposed to wake up yet, I am going back to sleep in 5 minutes until it is time to wake up");
        set_Alarm1(Clock.getHour(h12,PM), Clock.getMinute()+5, Clock.getSecond());
      }
      else
      {//It was time to wake up, the time to go back to sleep has been set.
      Serial.println(" /It was time to wake up, the time to go back to sleep has been set.---Inside setup");
      set_Alarm1(Hour_Sleep, Minute_Sleep, Second_Sleep);//Sleep 2 minutes //This is a wakeup alarm  
      }
       
  }
  //Set Deep sleep (INTERRUPT) wakeup from pushbuttons
  esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK,ESP_EXT1_WAKEUP_ANY_HIGH);
  
  //Declarations for the buttons (with double functionality as external wakeups
  pinMode(34, INPUT);
  pinMode(32, INPUT);
  pinMode(4, INPUT);//To synchronize PHone Application
  pinMode(33, INPUT);//To synchronize PHone Application
  //Set the RTC 
  Wire.begin();
}
void loop() 
{
  Check_Alarms();
  Serial.println(digitalRead(4));
  button = digitalRead(32);
  button_sleep = digitalRead(34);
  if(button_sleep)              //Button is pressed
  {
    set_wake_and_sleep();
  }
  if(button==1||searchPhone)              //Button is pressed
  {
    searchPhone=true;
    if(flag == 0)//Make sure to setup only once // Make flag=0 both after timer no connection and connection done. 
    {
      SCAN_APP_SETUP();
      flag++;
      //Make sure to flag=0 after the clock sets alarm after 2 minutes of pressing the button or after the connection with BLE is lost.
    }
    SCAN_APP();
  }
  else if((button==0)&&(!searchPhone))                   //Button Not pressed
  {
    SCAN_TAG();
  }
}