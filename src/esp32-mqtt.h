/******************************************************************************
 * Copyright 2018 Google
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/
// This file contains static methods for API requests using Wifi / MQTT
#include <Client.h>
#include <Ethernet.h>
#include <EthernetUdp.h> // For NTP timesync
#include <SSLClient.h>

#include <MQTT.h>

#include <NTPClient.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>

#include <CloudIoTCore.h>
#include <CloudIoTCoreMqtt.h>
#include "ciotc_config.h" // Update this file with your configuration

// !!REPLACEME!!
// The MQTT callback function for commands and configuration updates
// Place your message handler code here.
void messageReceived(String &topic, String &payload){
  Serial.println("incoming: " + topic + " - " + payload);
}
///////////////////////////////

// Initialize WiFi and MQTT for this board
SSLClient *netClient;
CloudIoTCoreDevice *device;
CloudIoTCoreMqtt *mqtt;
MQTTClient *mqttClient;
unsigned long iat = 0;
String jwt;

#define NTP_TIME_OFFSET_SECONDS 3600 //1h
#define NTP_TIME_REFRESH_INTERVAL 60000 // 60s
EthernetUDP ethernetUdpClient;
NTPClient timeClient(ethernetUdpClient, ntp_primary, NTP_TIME_OFFSET_SECONDS, NTP_TIME_REFRESH_INTERVAL);

///////////////////////////////
// Helpers specific to this board
///////////////////////////////
String getDefaultSensor(){
  // return "Wifi: " + String(WiFi.RSSI()) + "db";
  return "LAN: 99 db";
}

String getJwt(){
  iat = time(nullptr);
  Serial.println("Refreshing JWT");
  jwt = device->createJWT(iat, jwt_exp_secs);
  return jwt;
}

void setupLan(){
  Serial.println("Starting LAN");

  /*WiFi.mode(WIFI_STA);
  // WiFi.setSleep(false); // May help with disconnect? Seems to have been removed from WiFi
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED){
    delay(100);
  }*/

  // todo figure this out with an alternative library
  // configTime(0, 0, ntp_primary, ntp_secondary);
  Serial.println("Waiting on time sync...");

  timeClient.begin();
  bool ok = timeClient.forceUpdate();
  if(ok) {
    Serial.println("NTP update okay!");
    Serial.println("It is: " + timeClient.getFormattedTime());
    unsigned long timestamp = timeClient.getEpochTime();
    Serial.println("Unix time: " + String(timestamp));
    // Set as system time
    struct timeval now;
    int rc;
    now.tv_sec = (time_t) timestamp;
    now.tv_usec = 0;
    rc = settimeofday(&now, NULL);
    if(rc==0) {
        Serial.println("settimeofday() successful.");
    }
    else {
        Serial.println("settimeofday() failed, errno = " + String(errno));
    }
  } else {
    Serial.println("NTP failed!!");
  }
  timeClient.end();

  time_t rawtime;
  struct tm *info;
  char buffer[80];  time( &rawtime );
  info = localtime( &rawtime );
  strftime(buffer, 80, "%x - %I:%M%p", info);
  Serial.println("Current date + time: "  + String(buffer));

  // before Tue Nov 14 2017 07:36:07 GMT+0000? we did not sync correctly..
  if (time(nullptr) < 1510644967){
    Serial.println("NTP sync failed, time is behind..");
  } else {
    Serial.println("NTP sync time sanity check passed.");

  }
}

void connectLan(){
  Serial.print("Waiting for LAN cable connected..");
  while(Ethernet.linkStatus() != LinkON) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println(" good.");
}

///////////////////////////////
// Orchestrates various methods from preceeding code.
///////////////////////////////
bool publishTelemetry(String data){
  return mqtt->publishTelemetry(data);
}

bool publishTelemetry(const char *data, int length){
  return mqtt->publishTelemetry(data, length);
}

bool publishTelemetry(String subfolder, String data){
  return mqtt->publishTelemetry(subfolder, data);
}

bool publishTelemetry(String subfolder, const char *data, int length){
  return mqtt->publishTelemetry(subfolder, data, length);
}

void connect(){
  connectLan();
  mqtt->mqttConnect();
}

void setupCloudIoT(SSLClient* sslClient){
  device = new CloudIoTCoreDevice(
      project_id, location, registry_id, device_id,
      private_key_str);

  setupLan();
  netClient = sslClient; //= new WiFiClientSecure();
  // already handled by initializing the sslClient with the proper certificate
  //netClient->setCACert(root_cert);
  mqttClient = new MQTTClient(512);
  mqttClient->setOptions(180, true, 1000); // keepAlive, cleanSession, timeout
  mqtt = new CloudIoTCoreMqtt(mqttClient, netClient, device);
  mqtt->setUseLts(true);
  mqtt->startMQTT();
}