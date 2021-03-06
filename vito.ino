#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <VitoWiFi.h>
#include <AsyncMqttClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

//HTTP-Server fuer Info-Webseite
ESP8266WebServer httpServer(80);
//HTTP-Server fuer Sketch/Firmware Update
ESP8266HTTPUpdateServer httpUpdater;

//###### Variablen
volatile bool updateVitoWiFi = false;
bool bStopVito = false;
unsigned long lastMillis;
bool bLastMqttCheck = true;
Ticker timer;
Ticker timerPublish;
int systemUpTimeSc = 0;
int systemUpTimeMn = 0;
int systemUpTimeHr = 0;
int systemUpTimeDy = 0;

char lambdaO2[9] = {0};
char kesselLeistung[9] = {0};
char brennerVerbrauch[9] = {0};
char brennerStarts[9] = {0};
char brennerStunden[9] = {0};
char tempFlamme[9] = {0};
char tempKessel[9] = {0};
char tempPufferUnten[9] = {0};
char tempPufferOben[9] = {0};
char tempWarmwasser[9] = {0};
char tempAussen[9] = {0};
char dateTime[20] = {0};
char error01[23] = {0};
char error02[23] = {0};
char error03[23] = {0};
char error04[23] = {0};
char error05[23] = {0};
char error06[23] = {0};
char error07[23] = {0};
char error08[23] = {0};
char error09[23] = {0};
char error10[23] = {0};

//###### Konfiguration
static const char SSID[] = "holzhaus";
static const char PASS[] = "pyuifmyshnxbhwdzimct2691485152DKZKYMHTNWGEGSFTGNQZ8127323751!?*";
static const IPAddress BROKER(192, 168, 6, 7);
static const uint16_t PORT =  1883;
static const char CLIENTID[] = "Vito";
static const char MQTTUSER[] = "";
static const char MQTTPASS[] = "";
static const int READINTERVAL = 30; //Abfrageintervall OptoLink, in Sekunden
uint32_t period = 1 * 60000L;       // 1 Minuten
VitoWiFi_setProtocol(P300);

//###### Data Types

// Error messages / DPError
class conv9_Error : public DPType {
 public:
  void encode(uint8_t* out, DPValue in) {}
  DPValue decode(const uint8_t* in) {
    uint8_t tmp[9] = { 0 };
    memcpy(tmp, in, 9);
    DPValue out(tmp, 9);
    return out;
  }
  const size_t getLength() const {
    return 9;
  }
};
typedef Datapoint<conv9_Error> DPError;

// dateTime / DPDateTime
class conv8_DateTime : public DPType {
 public:
  void encode(uint8_t* out, DPValue in) {}
  DPValue decode(const uint8_t* in) {
    uint8_t tmp[8] = { 0 };
    memcpy(tmp, in, 8);
    DPValue out(tmp, 8);
    return out;
  }
  const size_t getLength() const {
    return 8;
  }
};
typedef Datapoint<conv8_DateTime> DPDateTime;

// Percent2 / DPPercent2
class conv1_Percent2 : public DPType {
 public:
  void encode(uint8_t* out, DPValue in) {}
  DPValue decode(const uint8_t* in) {
    DPValue out(in[0] / 2.0f);
    return out;
  }
  const size_t getLength() const {
    return 1;
  }
};
typedef Datapoint<conv1_Percent2> DPPercent2;

//###### Allgemein
DPTemp getTempAussen("tempAussen", "allgemein", 0x0800);               //Aussentemperatur
DPTemp getTempAussenTiefpass("tempAussenTiefpass", "allgemein", 0x5525);               //Aussentemperatur Tiefpass
// DPStat getAlarmStatus("alarmStatus", "allgemein", 0x0A82);   //Sammelstoerung Ja/Nein
DPDateTime getDateTime("dateTime", "allgemein", 0x088E);
DPError getError01("error01", "allgemein", 0x7507);
DPError getError02("error02", "allgemein", 0x7510);
DPError getError03("error03", "allgemein", 0x7519);
DPError getError04("error04", "allgemein", 0x7522);
DPError getError05("error05", "allgemein", 0x752B);
DPError getError06("error06", "allgemein", 0x7534);
DPError getError07("error07", "allgemein", 0x753D);
DPError getError08("error08", "allgemein", 0x7546);
DPError getError09("error09", "allgemein", 0x754F);
DPError getError10("error10", "allgemein", 0x7558);

//###### Warmwasser
DPTemp getTempWarmwasser("tempWarmwasser", "wasser", 0x0814);          //Warmwasser

//###### Puffer
DPTemp getTempPufferOben("tempPufferOben", "puffer", 0x0810);
DPTemp getTempPufferUnten("tempPufferUnten", "puffer", 0x0812);

//###### Kessel
DPTemp getTempKessel("tempKessel", "kessel", 0x0B12);
DPTemp getTempFlamme("tempFlamme", "kessel", 0x0B14);
DPHours getBrennerStunden("brennerStunden", "kessel", 0x08A7);  //Brennerstunden Stufe
DPCount getBrennerStarts("brennerStarts", "kessel", 0x088A);
DPCount getBrennerVerbrauch("brennerVerbrauch", "kessel", 0x08B0);
DPPercent2 getKesselLeistung("kesselLeistung", "kessel", 0xA305);
DPCoP getLambdaO2("lambdaO2", "kessel", 0x0B18);

//###### Heizkreise
// DPTemp getTempVListM1("tempvlistm1", "heizkreise", 0x2900);                  //HK1 Vorlauftemp
// DPTemp getTempVListM2("tempvlistm2", "heizkreise", 0x3900);                  //HK2 Vorlauftemp
// DPTempS getTempRaumNorSollM1("tempraumnorsollm1", "heizkreise", 0x2306);     //HK1 Raumtemp-Soll
// DPTempS setTempRaumNorSollM1("settempraumnorsollm1", "heizkreise", 0x2306);   //HK1 Raumtemp-Soll schreiben
// DPTempS getTempRaumNorSollM2("tempraumnorsollm2", "heizkreise", 0x3306);     //HK2 Raumtemp-Soll
// DPTempS setTempRaumNorSollM2("settempraumnorsollm2", "heizkreise", 0x3306);   //HK2 Raumtemp-Soll schreiben

//###### Betriebsarten
// TODO DPMode getBetriebArtM1("betriebartm1","betriebsarten", 0x2301);     //HK1 0=Abschaltb,1=nur WW,2=heiz+WW, 3=DauernRed,3=Dauer Norma.
// DPMode getBetriebArtM2("betriebartm2","betriebsarten", 0x3301);     //HK2 0=Abschaltb,1=nur WW,2=heiz+WW, 3=DauernRed,3=Dauer Norma.
// DPStat getBetriebPartyM1("betriebpartym1","betriebsarten", 0x2303); //HK1 Party
// DPStat getBetriebPartyM2("betriebpartym2","betriebsarten", 0x3303); //HK2 Party
// DPStat setBetriebPartyM1("setbetriebpartym1","betriebsarten", 0x2330); //HK1 Party schreiben
// DPStat setBetriebPartyM2("setbetriebpartym2","betriebsarten", 0x3330); //HK2 Party schreiben


//###### Objekte und Event-Handler
AsyncMqttClient mqttClient;
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

void connectToWiFi() {
  //Mit WLAN verbinden
  WiFi.begin(SSID, PASS);
}

void connectToMqtt() {
  //Mit MQTT-Server verbinden
  mqttClient.connect();
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  //Wenn WLAN-Verbindung steht und IP gesetzt, zu MQTT verbinden
  timer.once(2, connectToMqtt);
}

void updateMqttData(const IDatapoint& dp, const char *outVal) {
  const char *name = dp.getName();

  if(!strcmp(name, "lambdaO2")) {
    strcpy(lambdaO2, outVal);
  } else if(!strcmp(name, "kesselLeistung")) {
    strcpy(kesselLeistung, outVal);
  } else if(!strcmp(name, "brennerVerbrauch")) {
    strcpy(brennerVerbrauch, outVal);
  } else if(!strcmp(name, "brennerStarts")) {
    strcpy(brennerStarts, outVal);
  } else if(!strcmp(name, "brennerStunden")) {
    strcpy(brennerStunden, outVal);
  } else if(!strcmp(name, "tempFlamme")) {
    strcpy(tempFlamme, outVal);
  } else if(!strcmp(name, "tempKessel")) {
    strcpy(tempKessel, outVal);
  } else if(!strcmp(name, "tempPufferUnten")) {
    strcpy(tempPufferUnten, outVal);
  } else if(!strcmp(name, "tempPufferOben")) {
    strcpy(tempPufferOben, outVal);
  } else if(!strcmp(name, "tempWarmwasser")) {
    strcpy(tempWarmwasser, outVal);
  } else if(!strcmp(name, "tempAussen")) {
    strcpy(tempAussen, outVal);
  } else if(!strcmp(name, "dateTime")) {
    strcpy(dateTime, outVal);
  } else if(!strcmp(name, "error01")) {
    strcpy(error01, outVal);
  } else if(!strcmp(name, "error02")) {
    strcpy(error02, outVal);
  } else if(!strcmp(name, "error03")) {
    strcpy(error03, outVal);
  } else if(!strcmp(name, "error04")) {
    strcpy(error04, outVal);
  } else if(!strcmp(name, "error05")) {
    strcpy(error05, outVal);
  } else if(!strcmp(name, "error06")) {
    strcpy(error06, outVal);
  } else if(!strcmp(name, "error07")) {
    strcpy(error07, outVal);
  } else if(!strcmp(name, "error08")) {
    strcpy(error08, outVal);
  } else if(!strcmp(name, "error09")) {
    strcpy(error09, outVal);
  } else if(!strcmp(name, "error10")) {
    strcpy(error10, outVal);
  } else {
    char outName[50] = { 0 };
    snprintf(outName, sizeof(outName), "vito/unhandled/%s", name);
    mqttClient.publish(outName, 1, false, outVal);
  }
}

void publishMqttData() {
  if(!strlen(dateTime)) {
    return;
  }

  String mqttJson = String() +
    "{\n" +
    "  \"lambdaO2\": \"" + lambdaO2 + "\",\n" +
    "  \"kesselLeistung\": \"" + kesselLeistung + "\",\n" +
    "  \"brennerVerbrauch\": \"" + brennerVerbrauch + "\",\n" +
    "  \"brennerStarts\": \"" + brennerStarts + "\",\n" +
    "  \"brennerStunden\": \"" + brennerStunden + "\",\n" +
    "  \"tempFlamme\": \"" + tempFlamme + "\",\n" +
    "  \"tempKessel\": \"" + tempKessel + "\",\n" +
    "  \"tempPufferUnten\": \"" + tempPufferUnten + "\",\n" +
    "  \"tempPufferOben\": \"" + tempPufferOben + "\",\n" +
    "  \"tempWarmwasser\": \"" + tempWarmwasser + "\",\n" +
    "  \"tempAussen\": \"" + tempAussen + "\",\n" +
    "  \"dateTime\": \"" + dateTime + "\",\n" +
    "  \"error01\": \"" + error01 + "\",\n" +
    "  \"error02\": \"" + error02 + "\",\n" +
    "  \"error03\": \"" + error03 + "\",\n" +
    "  \"error04\": \"" + error04 + "\",\n" +
    "  \"error05\": \"" + error05 + "\",\n" +
    "  \"error06\": \"" + error06 + "\",\n" +
    "  \"error07\": \"" + error07 + "\",\n" +
    "  \"error08\": \"" + error08 + "\",\n" +
    "  \"error09\": \"" + error09 + "\",\n" +
    "  \"error10\": \"" + error10 + "\"\n" +
    "}";
  mqttClient.publish("vito/tele/SENSOR", 1, true, mqttJson.c_str());
}

void onMqttConnect(bool sessionPresent) {
  //Wenn MQTT verbunden, Topics abonnieren
  //mqttClient.subscribe("vito/cmnd/setBetriebPartyM1", 0);
  //mqttClient.subscribe("vito/cmnd/setBetriebPartyM2", 0);
  //mqttClient.subscribe("vito/cmnd/setTempWWsoll", 0);
  //mqttClient.subscribe("vito/cmnd/setTempRaumNorSollM1", 0);
  //mqttClient.subscribe("vito/cmnd/setTempRaumNorSollM2", 0);
  mqttClient.publish("vito/tele/LWT", 1, true, "Online");
  //Timer aktivieren, alle X Sekunden die Optolink-Schnittstelle abfragen
  timer.attach(READINTERVAL, [](){
    updateVitoWiFi = true;
  });
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  //Wenn Mqtt Verbindung verloren und wifi noch verbunden
  if (WiFi.isConnected()) {
    //Mqtt erneut verbinden
    timer.once(2, connectToMqtt);
  }
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  //Wenn WLAN Verbindung verloren, neu verbinden
  timer.once(2, connectToWiFi);
}

// void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
//  //Wenn abonnierte MQTT Nachricht erhalten
//  if(strcmp(topic,"vito/cmnd/setBetriebPartyM1") == 0) {
//     bool setParty = 0;
//     //Wert(Payload) auswerten und Variable setzen
//     if(strstr(payload,"1")) setParty = 1;
//     //In DPValue Konvertieren (siehe github vitowifi fuer Datentypen)
//     DPValue value(setParty);
//     //Wert an Optolink schicken
//     //VitoWiFi.writeDatapoint(setBetriebPartyM1, value);
//     //VitoWiFi.writeDatapoint(setBetriebPartyM1, value);
//     //VitoWiFi.writeDatapoint(setBetriebPartyM1, value);
//     //Wert auslesen um aktuellen Status an MQTT-Broker zu senden
//     //VitoWiFi.readDatapoint(getBetriebPartyM1);
//  }
//  if(strcmp(topic,"vito/cmnd/setBetriebPartyM2") == 0) {
//     bool setParty = 0;
//     //if(strcmp(payload,"1") == 0) setParty = 1;
//     if(strstr(payload,"1")) setParty = 1;
//     DPValue value(setParty);
//     //VitoWiFi.writeDatapoint(setBetriebPartyM2, value);
//     //VitoWiFi.writeDatapoint(setBetriebPartyM2, value);
//     //VitoWiFi.writeDatapoint(setBetriebPartyM2, value);
//     //VitoWiFi.readDatapoint(getBetriebPartyM2);
//  }
//  /*if(strcmp(topic,"vito/cmnd/setTempWWsoll") == 0) {
//     uint8_t setTemp = atoi(payload);
//     if(setTemp>=45 && setTemp<=60){
//       DPValue value(setTemp);
//       VitoWiFi.writeDatapoint(setTempWWsoll, value);
//       VitoWiFi.readDatapoint(getTempWWsoll);
//     }
//  }
//  if(strcmp(topic,"vito/cmnd/setTempRaumNorSollM1") == 0) {
//     uint8_t setTemp = atoi(payload);
//     if(setTemp>=3 && setTemp<=37){
//       DPValue value(setTemp);
//       VitoWiFi.writeDatapoint(setTempRaumNorSollM1, value);
//       VitoWiFi.readDatapoint(getTempRaumNorSollM1);
//     }
//  }
//  if(strcmp(topic,"vito/cmnd/setTempRaumNorSollM2") == 0) {
//     uint8_t setTemp = atoi(payload);
//     if(setTemp>=3 && setTemp<=37){
//       DPValue value(setTemp);
//       VitoWiFi.writeDatapoint(setTempRaumNorSollM2, value);
//       VitoWiFi.readDatapoint(getTempRaumNorSollM2);
//     }
//  }*/
// }

void boolCallbackHandler(const IDatapoint& dp, DPValue value) {
  //Umwandeln, und zum schluss per mqtt publish an mqtt-broker senden
  const char * outVal = (value.getBool()) ? "1" : "0";
  updateMqttData(dp, outVal);
}

// void uint8CallbackHandler(const IDatapoint& dp, DPValue value) {
//  //Umwandeln, und zum schluss per mqtt publish an mqtt-broker senden
//  int nValue = value.getU8();
//  updateMqttData(dp, outVal);
// }

void uint32CallbackHandler(const IDatapoint& dp, DPValue value) {
  //Umwandeln, und zum schluss per mqtt publish an mqtt-broker senden
  char outVal[11];
  ultoa(value.getU32(), outVal, 10);
  updateMqttData(dp, outVal);
}

void floatCallbackHandler(const IDatapoint& dp, DPValue value) {
  //Umwandeln, und zum schluss per mqtt publish an mqtt-broker senden
  char outVal[9];
//  dtostrf(value.getFloat(), 6, 2, outVal);
  snprintf(outVal, sizeof(outVal), "%.2f", value.getFloat());
  updateMqttData(dp, outVal);
}

void errorCallbackHandler(const IDatapoint& dp, DPValue value) {
  //Umwandeln, und zum schluss per mqtt publish an mqtt-broker senden
  uint8_t outRaw[9] = { 0 };
  value.getRaw(outRaw);

  // F5 20 20 10 05 01 07 49 20
  char outVal[23] = { 0 }; // F5 2020-10-05 07:49:20
  snprintf(outVal, sizeof(outVal), "%02X %02X%02X-%02X-%02X %02X:%02X:%02X",
    outRaw[0] & 255,
    outRaw[1] & 255,
    outRaw[2] & 255,
    outRaw[3] & 255,
    outRaw[4] & 255,
    // outRaw[5] is the day of week (1=Mon, 7=Sun)
    outRaw[6] & 255,
    outRaw[7] & 255,
    outRaw[8] & 255);  

  updateMqttData(dp, outVal);
}

void dateTimeCallbackHandler(const IDatapoint& dp, DPValue value) {
  //Umwandeln, und zum schluss per mqtt publish an mqtt-broker senden
  char outVal[20] = { 0 }; // 2021-02-28 12:06:17

  uint8_t outRaw[8] = { 0 };
  value.getRaw(outRaw);

  // 20 21 02 28 07 12 06 17
  snprintf(outVal, sizeof(outVal), "%02X%02X-%02X-%02X %02X:%02X:%02X",
    outRaw[0] & 255,
    outRaw[1] & 255,
    outRaw[2] & 255,
    outRaw[3] & 255,
    // outRaw[4] is the day of week (1=Mon, 7=Sun)
    outRaw[5] & 255,
    outRaw[6] & 255,
    outRaw[7] & 255);

  updateMqttData(dp, outVal);
}

// int char2hex(char *outString, const char *charPtr, int len) {
//  int n;
//  char string[4] = { 0 };
//
//  for (n = 0; n < len; n++) {
//    unsigned char byte = *charPtr++ & 255;
//    snprintf(string, sizeof(string), "%02X ", byte);
//    strcat(outString, string);
//  }
//
//  // Remove last space
//  outString[strlen(outString) - 1] = '\0';
//
//  return len;
// }

// Hex - troubleshooting
// void hexCallbackHandler(const IDatapoint& dp, DPValue value) {
//  //Umwandeln, und zum schluss per mqtt publish an mqtt-broker senden
//  char outVal[30] = { 0 };
//
//  size_t len = 8;
//  uint8_t outRaw[len];
//  value.getRaw(outRaw);
//  char2hex(outVal, (char*) outRaw, len); 
//
//  updateMqttData(dp, outVal);
// }

void setup() {
  //DEBUG WiFi.mode(WIFI_AP_STA);
  //Setze WLAN-Optionen
  WiFi.mode(WIFI_STA);
  WiFi.hostname(CLIENTID);

  //Setze Datenpunkte als beschreibbar
  //setBetriebPartyM1.setWriteable(true);
  //setBetriebPartyM2.setWriteable(true);
  //setTempWWsoll.setWriteable(true);
  //setTempRaumNorSollM1.setWriteable(true);
  //setTempRaumNorSollM2.setWriteable(true);

  //Zuweisung der Datenpunkte anhand des Rueckgabewerts an entsprechende Handler
  //(siehe github vitowifi fuer Datentypen)
  getTempAussen.setCallback(floatCallbackHandler);
  getTempWarmwasser.setCallback(floatCallbackHandler);
  getTempKessel.setCallback(floatCallbackHandler);
  getTempPufferOben.setCallback(floatCallbackHandler);
  getTempPufferUnten.setCallback(floatCallbackHandler);
  getBrennerStunden.setCallback(floatCallbackHandler);
  getTempFlamme.setCallback(floatCallbackHandler);
  getBrennerStarts.setCallback(uint32CallbackHandler);
  getBrennerVerbrauch.setCallback(uint32CallbackHandler);
  getKesselLeistung.setCallback(floatCallbackHandler);
  getLambdaO2.setCallback(floatCallbackHandler);
  getError01.setCallback(errorCallbackHandler);
  getError02.setCallback(errorCallbackHandler);
  getError03.setCallback(errorCallbackHandler);
  getError04.setCallback(errorCallbackHandler);
  getError05.setCallback(errorCallbackHandler);
  getError06.setCallback(errorCallbackHandler);
  getError07.setCallback(errorCallbackHandler);
  getError08.setCallback(errorCallbackHandler);
  getError09.setCallback(errorCallbackHandler);
  getError10.setCallback(errorCallbackHandler);
  getDateTime.setCallback(dateTimeCallbackHandler);

  //Wichtig, da sonst ueber die Serielle-Konsole (Optolink) Text geschrieben wird
  VitoWiFi.disableLogger();
  //Setze Serielle PINS an VitoWifi
  VitoWiFi.setup(&Serial);

  //Verbindungsaufbau und setzen der Optionen
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
//  mqttClient.onMessage(onMqttMessage);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
  mqttClient.setServer(BROKER, PORT);
  mqttClient.setClientId(CLIENTID);
  mqttClient.setCredentials(MQTTUSER, MQTTPASS);
  mqttClient.setKeepAlive(5);
  mqttClient.setCleanSession(true);
  mqttClient.setWill("vito/tele/LWT", 1, true, "Offline"); // Last-Will & Testament
  connectToWiFi();

  //Info-Webseite anzeigen auf HTTP-Port 80
  httpServer.on("/", [](){
    httpServer.send(200, "text/html", String() +
      "<head>" +
      "<link href=\"data:image/png;base64,AAABAAEAEBAAAAEAIABoBAAAFgAAACgAAAAQAAAAIAAAAAEAIAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAQPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//Dzv//xA8//8QPP//EDz//xA8//8PPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//Djr//zpe//+Tp///mKv//5mr//+Emv//IUr//w87//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//Dzz//xZB//+is///////////////////+fr//22I//8OOv//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//w06//9Nbv//7fD////////////////////////M1f//KFD//w47//8QPP//EDz//xA8//8QPP//EDz//w87//8ZRP//rr3/////////////5+v///Hz////////+/z//3mS//8PO///EDz//xA8//8QPP//EDz//xA8//8NOv//WHf///L0////////+/z//3uT//+puP/////////////U3P//L1X//w46//8QPP//EDz//xA8//8PO///Hkf//7nG/////////////9HZ//8oT///UXH///Dz/////////v7//4Wc//8QPP//EDz//xA8//8QPP//DTr//2OA///19/////////7+//9/lv//Djr//xpE//+0wv/////////////c4v//N1z//w46//8QPP//Dzv//yRN///F0P/////////////Z4P//MVf//w46//8NOv//XHr///X3/////////////5On//8TP///EDz//w06//9Uc///0Nn//9fe///Y3///f5b//xE8//8QPP//Dzv//x9I//+puf//2N///9jf//+7yP//LlX//w46//8QPP//G0X//yVN//8lTf//JU3//xhC//8QPP//EDz//xA8//8QPP//HUb//yVN//8lTf//JU3//xZB//8QPP//EDz//w87//8PO///Dzv//w87//8PPP//EDz//xA8//8QPP//EDz//w87//8PO///Dzv//w87//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//EDz//xA8//8QPP//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==\" rel=\"icon\" type=\"image/x-icon\" />" +
      "<meta http-equiv=\"refresh\" content=\"30\">" +
      "<title>Vito</title>" +
      "</head>" +
      "<body>" +
      "tempAussen: " + tempAussen + "<br>" +
      "lambdaO2: " + lambdaO2 + "<br>" +
      "kesselLeistung: " + kesselLeistung + "<br>" +
      "brennerVerbrauch: " + brennerVerbrauch + "<br>" +
      "brennerStarts: " + brennerStarts + "<br>" +
      "brennerStunden: " + brennerStunden + "<br>" +
      "tempFlamme: " + tempFlamme + "<br>" +
      "tempKessel: " + tempKessel + "<br>" +
      "tempPufferUnten: " + tempPufferUnten + "<br>" +
      "tempPufferOben: " + tempPufferOben + "<br>" +
      "tempWarmwasser: " + tempWarmwasser + "<br>" +
      "dateTime: " + dateTime + "<br>" +
      "error01: " + error01 + "<br>" +
      "error02: " + error02 + "<br>" +
      "error03: " + error03 + "<br>" +
      "error04: " + error04 + "<br>" +
      "error05: " + error05 + "<br>" +
      "error06: " + error06 + "<br>" +
      "error07: " + error07 + "<br>" +
      "error08: " + error08 + "<br>" +
      "error09: " + error09 + "<br>" +
      "error10: " + error10 + "<br>" +
      "<br><br>" +
      "<b>Vito-Status: " + String((bStopVito ? "Stopped" : "Running")) + "</b>" +
      "<br>" +
      "<b>MQTT-Connected: " + String(mqttClient.connected() ? "Yes" : "No") + "</b>" +
      "<br>" +
      "<a href='/start'>Start</a> <a href='/stop'>Stop</a>" +
      "<br><br>" +
      "<b>Compiled: " __DATE__ " " __TIME__ +
      "<br>" +
      "Uptime: " + String(systemUpTimeDy) + " days " + String(systemUpTimeHr) + ":" + String(systemUpTimeMn) + ":" + String(systemUpTimeSc) + "</b>" +
      "<br><br>"
      "<a href='/update'>update</a> <a href='/reboot'>reboot</a>" +
      "</body>");
  });

  //Stop-Funktion sollte etwas schieflaufen :)
  httpServer.on("/stop", [](){
    bStopVito = true;
    httpServer.send(200, "text/plain", "OK - Stopped");
  });
  //Startfunktion
  httpServer.on("/start", [](){
    bStopVito = false;
    httpServer.send(200, "text/plain", "OK - Started");
  });
  //Reboot ueber Webinterface
  httpServer.on("/reboot", [](){
    httpServer.send(200, "text/plain", "OK - rebooting...");
    ESP.restart();
  });

  //Starte Webserver fuer Sketch/Firmware-Update
  httpUpdater.setup(&httpServer);
  httpServer.begin();
}

void loop() {
  if (!bStopVito){
    VitoWiFi.loop();
    if (updateVitoWiFi && mqttClient.connected()) {
      updateVitoWiFi = false;
      VitoWiFi.readAll();

      timerPublish.attach(5, [](){
        publishMqttData();
        timerPublish.detach();
      });
//      Ticker timerPublish([](){
//        publishMqttData();
//      }, 5000, 1);
//    Ticker timerPublish(doIt, 5000, 1);
//
//      timer.attach(READINTERVAL, [](){
//    updateVitoWiFi = true;
//  });
    }
  }
  httpServer.handleClient();

  long millisecs = millis();
  systemUpTimeSc = int((millisecs / 1000) % 60);

  //Jede Minute
  if (millisecs - lastMillis >= period) {
    lastMillis = millisecs;  //get ready for the next iteration
    if (!mqttClient.connected() && !bLastMqttCheck){
      ESP.restart();
    }
    if (!mqttClient.connected()){
      bLastMqttCheck = false;
    }else{
      bLastMqttCheck = true;
    }
    systemUpTimeMn = int((millisecs / (1000 * 60)) % 60);
    systemUpTimeHr = int((millisecs / (1000 * 60 * 60)) % 24);
    systemUpTimeDy = int((millisecs / (1000 * 60 * 60 * 24)) % 365);
  }
}
