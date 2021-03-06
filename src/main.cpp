/***********************************************************************************
  Includes
***********************************************************************************/
#include <version.h>   //https://gitlab.com/pvojnisek/buildnumber-for-platformio/tree/master
#include <Arduino.h>
#include <WiFi.h>     
#include <Wire.h>     
#include <WebServer.h>     
#include <AutoConnect.h>  //https://github.com/Hieromon/AutoConnect
#include <AutoConnectCredential.h>
#include <PageBuilder.h>
#include <WiFiClient.h>
#include <Preferences.h>
#include "SSD1306.h"
#include <NTPClient.h>
#include <Time.h>
#include <TimeLib.h>
#include <Timezone.h>

/*
#include "OV2640.h"
#include "SimStreamer.h"
#include "OV2640Streamer.h"
#include "CRtspSession.h"

#define USEBOARD_TTGO_T
#define ENABLE_OLED
*/

/***********************************************************************************
  Global defines
***********************************************************************************/
#define FACTORYRESET_BUTTON 32
#define OLED_ADDRESS 0x3c
#define I2C_SDA 14
#define I2C_SCL 13

#define CONFIG_PSK "forkcam2019"


/***********************************************************************************
  GPIO pin variables
***********************************************************************************/
int pinButton = 32;

/***********************************************************************************
  GLOBAL VARIABLES
***********************************************************************************/
bool acEnable;

String szSoftSSID;
/***********************************************************************************
  GLOBAL OBJECTS
***********************************************************************************/
SSD1306Wire display(OLED_ADDRESS, I2C_SDA, I2C_SCL, GEOMETRY_128_32);
WebServer Server;          
AutoConnect Portal(Server);
AutoConnectConfig acConfig;
Preferences prefs;
AutoConnectAux videoSettings("/videosettings", "Video Target");
AutoConnectText vtsHeader("Video Target Settings");
AutoConnectText vtsCaption("Configure the server side target for video files.");
AutoConnectInput txtTargetServer("txtTargetServer", "", "Target Path", "Target folder path");
AutoConnectSubmit vtsSubmit("cmdSubmit", "Save", "/video_save");

AutoConnectAux videoSettingsSaved("/video_save", "Video Target", false);
AutoConnectText vssHeader("Video Target Saved");
AutoConnectText vssCaption("Hit Back to configure the AP connection settings.");

/***********************************************************************************
  TESTING STUFF
***********************************************************************************/




/***********************************************************************************
  SUPPORTING WORKERS
***********************************************************************************/
// Hello world HTTP response
void rootPage() {
  char content[] = "Hello, world";
  Server.send(200, "text/plain", content);
}
// Postback handler for saving the video target settings
String vtsSavedHandler(AutoConnectAux& aux, PageArgument& args){
  AutoConnectAux* targetServerUri = Portal.aux(Portal.where());
  AutoConnectInput& targetServer = targetServerUri->getElement<AutoConnectInput>("txtTargetServer");

  Serial.println(targetServer.value);

  return String("");
}
// Delete all SSID credentials to force the captive portal to start - called if the user button is pressed at start up.
void deleteAllCredentials() {
  // Delete all SSID and PSK credentials from NVS and force the captive portal to start.
  prefs.begin(AC_IDENTIFIER, false);
  prefs.clear();
  prefs.end();
  // Adjust the AutoConnect config to force the captive portal to begin.
  acConfig.apid = szSoftSSID;
  acConfig.psk = CONFIG_PSK;
  acConfig.immediateStart = true;
  Portal.config(acConfig); 
}
// Send text to the OLED screen
void lcdMessage(String msg1, String msg2 = "", String msg3 = ""){
  // There can be up to 3 lines of text on the screen.
  // Depending upon the number of lines we modify the vertical position of each line.
  display.clear();
  if (msg2.length() > 0){
    // We have at least 2 lines
    if (msg3.length() > 0){
      // We have 3 lines of text
      display.drawString(64, 0, msg1);
      display.drawString(64, 11, msg2);
      display.drawString(64, 22, msg3);
    }else{
      // We only have 2 lines of text
      display.drawString(64, 5, msg1);
      display.drawString(64, 16, msg2);
    }
  }else{
    // We assume we only have 1 line of text.
    display.drawString(64, 11, msg1);
  }
  // Update the OLED
  display.display();
}
// Callback for the Portal object to allow us to notify the user that WiFi config is required as we have no credentials to connect to anything.
bool startCaptivePortal(IPAddress ip){
  lcdMessage(F("Config Required"), F("Connect to:"), szSoftSSID);
  return true;
}
/***********************************************************************************
  **********************************************************************************
  SETUP CODE
  **********************************************************************************
***********************************************************************************/
void setup() {
  // Start the serial object so we can output information along the way
  Serial.begin(115200);
  while (!Serial)
  {
    delay(10);
  }
  Serial.println();
  // Set up the button that resets the WiFi configuration - Button must be pressed during power up or reset
  pinMode(pinButton, INPUT);
  // Set up the soft access point (captive portal) SSID and PSK. The SSID is the last 6 characters of the MAC Address followed by "-CAM"
  szSoftSSID = WiFi.macAddress();
  szSoftSSID.replace(":", "");
  szSoftSSID.remove(0, 6);
  szSoftSSID.concat(F("-CAM"));
  acConfig.apid = szSoftSSID;
  acConfig.psk = CONFIG_PSK;
  Portal.config(acConfig);
  // Output some info to the monitor terminal
  Serial.print(F("CapPortal SSID = "));
  Serial.println(szSoftSSID);
  Serial.print(F("CapPortal PSK = "));
  Serial.println(CONFIG_PSK);
  // Initialise the OLED display and display a simple booting message
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  Serial.println(F("Display Initalised."));
  lcdMessage(F("BOOTING"), F("Fork Cam"), VERSION_SHORT);
  delay(1000);
  // Tell the user we are attempting to connect to an access point
  lcdMessage(F("Connecting to"), F("WiFi..."), VERSION_SHORT);
  //  One of two things will happen:
  //    *If there are previously stored credentials then we will connect, assuming the credentials match the SSID where the device has started up.
  //    *If the button is pressed or the device has never had any credentials, the device will start the captive portal.
  if (digitalRead(pinButton) == LOW){
    Serial.println(F("The user button is pressed - delete all credentials so that the Soft AP and captive portal are started."));
    deleteAllCredentials();
  }
  // Set up the additional config web pages we need
  videoSettings.add({vtsHeader, vtsCaption, txtTargetServer, vtsSubmit});
  Portal.join(videoSettings);
  videoSettingsSaved.add({vssHeader, vssCaption});
  Portal.join(videoSettingsSaved);
  Portal.on("/video_save", vtsSavedHandler, AC_EXIT_AHEAD);
  // Set a hook for the portal to be able to call back to a routine above that displays a message on the OLED telling the user that config is required.
  Portal.onDetect(startCaptivePortal);
  // Start the portal. It will either connect to the WiFi with known credentials or it will launch the captive portal soft AP.
  acEnable = Portal.begin(); 
  if (acEnable) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
    lcdMessage(F("WiFi connected to"), "SSID: " + WiFi.SSID(), "as " + WiFi.localIP().toString());
  }else{
    Portal.end();
  }
}
/***********************************************************************************
  **********************************************************************************
  RUNTIME LOOP
  **********************************************************************************
***********************************************************************************/
void loop() {

  if (WiFi.status() == WL_CONNECTED) {
    // code for the connected state is here.
  }
  else {
    // code for not connected state is here.
  }
  // Handle portal clients
  if (acEnable) {
    Portal.handleClient();
  }
}

