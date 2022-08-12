#include <WiFi.h>
#include "ThingSpeak.h"
#include "DHT.h"
#include <DYIRDaikin.h>
#include <SimpleKalmanFilter.h>
#include "time.h"
#include <Preferences.h>

#define CONN_TIMEOUT 10000
const char* ssid     = "<SSID>";
const char* password = "<PASSWORD>"; // credential
const char* ntpServer = "pool.ntp.org";

#define CH_ID <ID>
#define CH_API_KEY "<KEY>" // credential

#define DHTPIN 4
#define DHTTYPE DHT22

#define DAIKIN_PIN 23
#define AC_MAX_RUNNING_TIME 10800
#define AC_MIN_STARTING_TIME 7200
#define AC_MIN_ADJUST_TIME 1200

// pins
#define INFO_LED 27
#define AC_LED 21

// obj
WiFiClient client;
DHT dht(DHTPIN, DHTTYPE);
DYIRDaikin daikinController;
SimpleKalmanFilter heatIndexFilter(0.7, 0.7, 0.1);
Preferences preferences;

// var
bool curr_AC_state = false;
bool prev_AC_state = false;
int AC_on_time;
int AC_off_time;
int AC_adjust_time;
int time_now;

void connectToWiFi(){
  // Connect to Wi-Fi
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long connection_start = millis(); // for timeout purpose
  
  while (WiFi.status() != WL_CONNECTED && millis() - connection_start < CONN_TIMEOUT) {
    Serial.print(".");
    digitalWrite(INFO_LED, HIGH);
    delay(250);
    digitalWrite(INFO_LED, LOW);
    delay(250);
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) { // if not connected after timeout
    Serial.println("Connection timed out.");
    while(WiFi.status() != WL_CONNECTED) {
      digitalWrite(INFO_LED, HIGH);
      delay(1500);
      digitalWrite(INFO_LED, LOW);
      delay(1500); 
    }
  }
  Serial.println("Connected!");
  digitalWrite(INFO_LED, HIGH);
}

void turnOnAC(int ac_swing, int ac_mode, int ac_fan, int ac_temp){
  daikinController.on();
  if (ac_swing != 0) {
    daikinController.setSwing_on(); 
  }
  else {
    daikinController.setSwing_off();
  }
  daikinController.setMode(ac_mode); // 0 = fan, 1 = cool, 2 = dry
  daikinController.setFan(ac_fan);
  daikinController.setTemp(ac_temp);
  daikinController.sendCommand();
  Serial.println("Command sent with parameter: SWING=" + (String)ac_swing + ",MODE=" + (String)ac_mode + ",TEMP=" + (String)ac_temp);
  digitalWrite(AC_LED, HIGH);
  delay(100);
  digitalWrite(AC_LED, LOW);
  delay(100);
  digitalWrite(AC_LED, HIGH);
  delay(100);
  digitalWrite(AC_LED, LOW);
  delay(100);
}

void turnOffAC() {
  daikinController.off();
  daikinController.sendCommand();
  Serial.println("Command sent: AC OFF");
  digitalWrite(AC_LED, HIGH);
  delay(500);
  digitalWrite(AC_LED, LOW);
}

void showErrorLED(){
  digitalWrite(INFO_LED, LOW);
  delay(100);
  digitalWrite(INFO_LED, HIGH);
  delay(100);
}

void setup(){
  pinMode(INFO_LED, OUTPUT);
  pinMode(AC_LED, OUTPUT);
  Serial.begin(9600);
  
  dht.begin();
  daikinController.begin(DAIKIN_PIN);
  connectToWiFi();
  configTime(7*3600, 0, ntpServer); // get time, +7 UTC, no DST
  ThingSpeak.begin(client);

  // setup this to start the AC regardless if not found
  preferences.begin("states", false);
  curr_AC_state = preferences.getBool("curr_state", false);
  prev_AC_state = preferences.getBool("prev_state", false);
  AC_on_time = preferences.getInt("on_time", 0);
  AC_off_time = preferences.getInt("off_time", 0);
  AC_adjust_time = preferences.getInt("adjust_time", 0);

  // test the IR emitter
  for (int i = 0; i < 2; i++) {
    if (curr_AC_state) { // if AC is ON, test for ON so that it wont disturb the AC
      turnOnAC(1, 1, 4, 25);
      delay(1000);
    }
    else { // if AC is OFF, test for OFF so that it wont disturb the AC
      turnOffAC();
      delay(1000);
    } 
  }
}

void loop(){
  struct tm timeinfo;
  float humidity = dht.readHumidity();
  float temp = dht.readTemperature();

  if (isnan(humidity) || isnan(temp)) {
    Serial.println("Failed to read from sensor!");
    showErrorLED();
    return;
  }

  float heat_index = dht.computeHeatIndex(temp, humidity, false);
  float est_heat_index = heatIndexFilter.updateEstimate(heat_index);

  if (millis() < 15000) { // waiting estimated heat index to be in steady-state
    return;
  }

  if (!getLocalTime(&timeinfo)) {
    showErrorLED();
    return;
  }

  /* AC timing and heat index control */
  
  if (timeinfo.tm_hour == 0) { 
    timeinfo.tm_hour = 24;
  }
  
  time_now = (timeinfo.tm_hour * 3600) + (timeinfo.tm_min * 60) + (timeinfo.tm_sec);

  // if the AC may be turned on again after some time
  if (prev_AC_state && time_now - AC_off_time > AC_MIN_STARTING_TIME) {
    prev_AC_state = false;
  }

  // if AC is OFF, has been allowed to be turned on, time is afternoon/evening, and the heat index is currently not comfortable
  if (!curr_AC_state && !prev_AC_state && timeinfo.tm_hour > 11 && timeinfo.tm_hour <= 20 && est_heat_index > 35) {
    turnOnAC(1, 1, 4, 25);
    curr_AC_state = true;
    AC_on_time = (timeinfo.tm_hour * 3600) + (timeinfo.tm_min * 60) + (timeinfo.tm_sec);
  }

  // always turn on AC in the evening to prepare for going to bed
  // if it is already evening/night and the AC is currently OFF
  else if ((timeinfo.tm_hour > 20 || timeinfo.tm_hour == 0) && !curr_AC_state) {
    curr_AC_state = true; 
    turnOnAC(1, 1, 2, 25);
  }

  // if it is already evening/night and the AC is already ON, just check whether the heat index has dropped too cold
  else if ((timeinfo.tm_hour > 20 || timeinfo.tm_hour == 0) && curr_AC_state && est_heat_index < 26 && time_now - AC_adjust_time > AC_MIN_ADJUST_TIME) {
    turnOnAC(1, 2, 3, 25); // to raise the temp a little bit, set to DRY MODE to maintain lower humidity
    AC_adjust_time = (timeinfo.tm_hour * 3600) + (timeinfo.tm_min * 60) + (timeinfo.tm_sec);
  }

  // if it is time to turn off the AC for saving
  else if (time_now - AC_on_time > AC_MAX_RUNNING_TIME && timeinfo.tm_hour > 11 && timeinfo.tm_hour <= 20 && curr_AC_state) {
    turnOffAC();
    curr_AC_state = false;
    AC_off_time = (timeinfo.tm_hour * 3600) + (timeinfo.tm_min * 60) + (timeinfo.tm_sec);
    prev_AC_state = true;
  }

  // if it is already night/morning and the AC is still ON, turn it OFF, reset all timer for this next day
  else if (curr_AC_state && (timeinfo.tm_hour >= 1 && timeinfo.tm_hour <= 11)) {
    turnOffAC();
    curr_AC_state = false;
    prev_AC_state = true;
    AC_on_time = 0;
    AC_off_time = 0;
    AC_adjust_time = 0;
  }

  // save the states, in case something like ESP restart happens
  preferences.putBool("curr_state", curr_AC_state);
  preferences.putBool("prev_state", prev_AC_state);
  preferences.putInt("on_time", AC_on_time);
  preferences.putInt("off_time", AC_off_time);
  preferences.putInt("adjust_time", AC_adjust_time);
  
  Serial.println("Humidity: " + (String)humidity + "%");
  Serial.println("Temperature: " + (String)temp + " C");
  Serial.println("Feels like: " + (String)heat_index + " C");
  Serial.println("Adjusted heat index: " + (String)est_heat_index + " C");
  Serial.println("Time now: " + (String)time_now);
  Serial.println("Off time: " + (String)AC_off_time);
  Serial.println("On time: " + (String)AC_on_time);
  Serial.println("Adjust time: " + (String)AC_adjust_time);
  Serial.println("---");
  Serial.println("---");

  delay(5000);
}
