
#include <SPI.h>
#include <ESP8266mDNS.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include "IthoCC1101.h"
#include "IthoPacket.h"
#include <Ticker.h>
#include <TimeLib.h>

/*
   Original Author: Klusjesman

   Tested with STK500 + ATMega328P
   GCC-AVR compiler

   Modified by supersjimmie:
   Code and libraries made compatible with Arduino and ESP8266
   Tested with Arduino IDE v1.6.5 and 1.6.9
   For ESP8266 tested with ESP8266 core for Arduino v 2.1.0 and 2.2.0 Stable
   (See https://github.com/esp8266/Arduino/ )

   Modified by arjenhiemstra to intergrate some separate projects
   Added webserver code/api, mostly a copied from incmve https://github.com/incmve/Itho-WIFI-remote
   Added MQTT, loosly copied from incmve
   Added Leave, Timer2 and Timer3

*/

/*
  CC11xx pins    ESP pins Arduino pins  Description
  1 - VCC        VCC      VCC           3v3
  2 - GND        GND      GND           Ground
  3 - MOSI       13=D7    Pin 11        Data input to CC11xx
  4 - SCK        14=D5    Pin 13        Clock pin
  5 - MISO/GDO1  12=D6    Pin 12        Data output from CC11xx / serial clock from CC11xx
  6 - GDO2       04=D2    Pin 2?        Data receive interrupt pin CC11xx
  7 - GDO0       ?        Pin  ?        output as a symbol of receiving or sending data
  8 - CSN        15=D8    Pin 10        Chip select / (SPI_SS)
*/

/*
 * Begin user settings.
 */
#define SOFTWARE_VERSION "0.1"

#define ITHO_IRQ_PIN 10 //10 = GPIO10 - NodeMCU pin SD3 || D2 = GPIO4 - NodeMCU pin D2

const uint8_t RFTid[] = {106, 170, 106, 101, 154, 107, 154, 86}; // remote ID, change to your own remote ID to receive commands from that remote

// WiFi
#define CONFIG_WIFI_SSID "ssid"
#define CONFIG_WIFI_PASS "password"
#define CONFIG_HOST_NAME "ESP-ITHO"

// MQTT
IPAddress mqttIP(192, 168, 0, 2);
#define CONFIG_MQTT_HOST "" //if host is left empty mqttIP is used
#define CONFIG_MQTT_USER ""
#define CONFIG_MQTT_PASS ""
#define CONFIG_MQTT_CLIENT_ID "MQTTITHO01" // Must be unique on the MQTT network

// MQTT Topics
#define CONFIG_MQTT_TOPIC_STATE "itho/fan/State"
#define CONFIG_MQTT_TOPIC_COMMAND "itho/fan/Cmd"
#define CONFIG_MQTT_TOPIC_IDINDEX "itho/fan/LastIDindex" //"1" if external remote triggered state topic update, "0" if state topic updated from API or MQTT cmd topic
/*
 * End user settings
 */



IthoCC1101 rf;
IthoPacket packet;
Ticker ITHOticker;

// WIFI
String ssid    = CONFIG_WIFI_SSID;
String password = CONFIG_WIFI_PASS;
String espName    = CONFIG_HOST_NAME;

String Version = SOFTWARE_VERSION;

String ClientIP;

// webserver
ESP8266WebServer  server(80);
MDNSResponder   mdns;
WiFiClient client;

// MQTT
const char* mqttHost = CONFIG_MQTT_HOST;
const char* mqttUsername = CONFIG_MQTT_USER;
const char* mqttPassword = CONFIG_MQTT_PASS;
const char* mqttClientId = CONFIG_MQTT_CLIENT_ID;
PubSubClient mqttclient(client);
long lastMsg = 0;
char msg[50];


// MQTT Topics
const char* commandtopic = CONFIG_MQTT_TOPIC_COMMAND;
const char* statetopic = CONFIG_MQTT_TOPIC_STATE;
const char* idindextopic = CONFIG_MQTT_TOPIC_IDINDEX;

// Div
bool ITHOhasPacket = false;
bool knownRFid = false;
IthoCommand RFTcommand[3] = {IthoUnknown, IthoUnknown, IthoUnknown};
byte RFTRSSI[3] = {0, 0, 0};
byte RFTcommandpos = 0;
IthoCommand RFTlastCommand = IthoLow;
IthoCommand RFTstate = IthoUnknown;
IthoCommand savedRFTstate = IthoUnknown;
bool RFTidChk[3] = {false, false, false};
String Laststate;
String CurrentState;
char lastidindex[2];

//HTML
String header       =  "<html lang='en'><head><title>Itho control panel</title><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'><link rel='stylesheet' href='http://maxcdn.bootstrapcdn.com/bootstrap/3.3.4/css/bootstrap.min.css'><script src='https://ajax.googleapis.com/ajax/libs/jquery/1.11.1/jquery.min.js'></script><script src='http://maxcdn.bootstrapcdn.com/bootstrap/3.3.4/js/bootstrap.min.js'></script></head><body>";
String navbar       =  "<nav class='navbar navbar-default'><div class='container-fluid'><div class='navbar-header'><a class='navbar-brand' href='/'>Itho control panel</a></div><div><ul class='nav navbar-nav'><li><a href='/'><span class='glyphicon glyphicon-question-sign'></span> Status</a></li><li class='dropdown'><a class='dropdown-toggle' data-toggle='dropdown' href='#'><span class='glyphicon glyphicon-cog'></span> Tools<span class='caret'></span></a><ul class='dropdown-menu'><li><a href='/api?action=reset&value=true'>Restart</a></ul></li><li><a href='https://github.com/incmve/Itho-WIFI-remote' target='_blank'><span class='glyphicon glyphicon-question-sign'></span> Help</a></li></ul></div></div></nav>  ";

String containerStart   =  "<div class='container'><div class='row'>";
String containerEnd     =  "<div class='clearfix visible-lg'></div></div></div>";
String siteEnd        =  "</body></html>";

String panelHeaderName    =  "<div class='col-md-4'><div class='page-header'><h1>";
String panelHeaderEnd   =  "</h1></div>";
String panelEnd       =  "</div>";

String panelBodySymbol    =  "<div class='panel panel-default'><div class='panel-body'><span class='glyphicon glyphicon-";
String panelBodyDuoSymbol    =  "</span></div><div class='panel-body'><span class='glyphicon glyphicon-";
String panelBodyName    =  "'></span> ";
String panelBodyValue   =  "<span class='pull-right'>";
String panelcenter   =  "<div class='row'><div class='span6' style='text-align:center'>";
String panelBodyEnd     =  "</span></div></div>";

String inputBodyStart   =  "<form action='' method='POST'><div class='panel panel-default'><div class='panel-body'>";
String inputBodyName    =  "<div class='form-group'><div class='input-group'><span class='input-group-addon' id='basic-addon1'>";
String inputBodyPOST    =  "</span><input type='text' name='";
String inputBodyClose   =  "' class='form-control' aria-describedby='basic-addon1'></div></div>";
String ithocontrol     =  "<a href='/button?action=Low'<button type='button' class='btn btn-default'> Low</button></a><a href='/button?action=Medium'<button type='button' class='btn btn-default'> Medium</button></a><a href='/button?action=High'<button type='button' class='btn btn-default'> High</button><a href='/button?action=Timer'<button type='button' class='btn btn-default'> Timer</button></a></a><br><a href='/button?action=Join'<button type='button' class='btn btn-default'> Join</button></a><a href='/button?action=Leave'<button type='button' class='btn btn-default'> Leave</button></a></div>";


void ITHOinterrupt() {
  ITHOticker.once_ms(10, ITHOcheck);
}

void ITHOcheck() {
  if (rf.checkForNewPacket()) {
    IthoCommand cmd = rf.getLastCommand();
    if (++RFTcommandpos > 2) RFTcommandpos = 0;  // store information in next entry of ringbuffers
    RFTcommand[RFTcommandpos] = cmd;
    RFTRSSI[RFTcommandpos]    = rf.ReadRSSI();
    bool chk = rf.checkID(RFTid);
    RFTidChk[RFTcommandpos]   = chk;
    if (cmd != IthoUnknown) {  // only act on good cmd
      if (chk) {
        ITHOhasPacket = true;
        knownRFid = true;
        strncpy(lastidindex, "1", 2);
      }
      else {
        Serial.print(F("Unknown remote with ID: "));
        Serial.println(rf.getLastIDstr());  
      }
    }
    else {
      //Serial.println(F("--- packet reveiced but of unknown type ---"));
    }
  }
}

void showPacket() {
  ITHOhasPacket = false;
  uint8_t goodpos = findRFTlastCommand();
  if (goodpos != -1)  RFTlastCommand = RFTcommand[goodpos];
  else                RFTlastCommand = IthoUnknown;
  //show data
  Serial.print(F("RFid known: "));
  Serial.println(knownRFid);
  Serial.print(F("RFT Current Pos: "));
  Serial.print(RFTcommandpos);
  Serial.print(F(", Good Pos: "));
  Serial.println(goodpos);
  Serial.print(F("Stored 3 commands: "));
  Serial.print(RFTcommand[0]);
  Serial.print(F(" "));
  Serial.print(RFTcommand[1]);
  Serial.print(F(" "));
  Serial.print(RFTcommand[2]);
  Serial.print(F(" / Stored 3 RSSI's:     "));
  Serial.print(RFTRSSI[0]);
  Serial.print(F(" "));
  Serial.print(RFTRSSI[1]);
  Serial.print(F(" "));
  Serial.print(RFTRSSI[2]);
  Serial.print(F(" / Stored 3 ID checks: "));
  Serial.print(RFTidChk[0]);
  Serial.print(F(" "));
  Serial.print(RFTidChk[1]);
  Serial.print(F(" "));
  Serial.print(RFTidChk[2]);
  Serial.print(F(" / Last ID: "));
  Serial.print(rf.getLastIDstr());

  Serial.print(F(" / Command = "));
  //show command
  switch (RFTlastCommand) {
    case IthoUnknown:
      Serial.print("unknown\n");
      break;
    case IthoLow:
      Serial.print("low\n");
      mqttclient.publish(statetopic, "Low");
      mqttclient.publish(idindextopic, lastidindex);
      break;
    case IthoMedium:
      Serial.print("medium\n");
      mqttclient.publish(statetopic, "Medium");
      mqttclient.publish(idindextopic, lastidindex);
      break;
    case IthoHigh:
      Serial.print("high\n");
      mqttclient.publish(statetopic, "High");
      mqttclient.publish(idindextopic, lastidindex);
      break;
    case IthoFull:
      Serial.print("full\n");
      mqttclient.publish(statetopic, "Full");
      mqttclient.publish(idindextopic, lastidindex);
      break;
    case IthoTimer1:
      Serial.print("timer1\n");
      mqttclient.publish(statetopic, "Timer1");
      mqttclient.publish(idindextopic, lastidindex);
      break;
    case IthoTimer2:
      Serial.print("timer2\n");
      mqttclient.publish(statetopic, "Timer2");
      mqttclient.publish(idindextopic, lastidindex);
      break;
    case IthoTimer3:
      Serial.print("timer3\n");
      mqttclient.publish(statetopic, "Timer3");
      mqttclient.publish(idindextopic, lastidindex);
      break;
    case IthoJoin:
      Serial.print("join\n");
      break;
    case IthoLeave:
      Serial.print("leave\n");
      break;
  }
}

uint8_t findRFTlastCommand() {
  if (RFTcommand[RFTcommandpos] != IthoUnknown)               return RFTcommandpos;
  if ((RFTcommandpos == 0) && (RFTcommand[2] != IthoUnknown)) return 2;
  if ((RFTcommandpos == 0) && (RFTcommand[1] != IthoUnknown)) return 1;
  if ((RFTcommandpos == 1) && (RFTcommand[0] != IthoUnknown)) return 0;
  if ((RFTcommandpos == 1) && (RFTcommand[2] != IthoUnknown)) return 2;
  if ((RFTcommandpos == 2) && (RFTcommand[1] != IthoUnknown)) return 1;
  if ((RFTcommandpos == 2) && (RFTcommand[0] != IthoUnknown)) return 0;
  return -1;
}

void sendJoin() {
  rf.sendCommand(IthoJoin);
  Serial.println("sending join done.");
}
void sendLeave() {
  rf.sendCommand(IthoLeave);
  Serial.println("sending leave done.");
}
void sendStandbySpeed() {
  rf.sendCommand(IthoStandby);
  CurrentState = "Standby";
  mqttclient.publish(statetopic, CurrentState.c_str());
  mqttclient.publish(idindextopic, "0");
  Serial.println("sending standby done.");
}

void sendLowSpeed() {
  rf.sendCommand(IthoLow);
  CurrentState = "Low";
  mqttclient.publish(statetopic, CurrentState.c_str());
  mqttclient.publish(idindextopic, "0");
  Serial.println("sending low done.");
}

void sendMediumSpeed() {
  rf.sendCommand(IthoMedium);
  CurrentState = "Medium";
  mqttclient.publish(statetopic, CurrentState.c_str());
  mqttclient.publish(idindextopic, "0");
  Serial.println("sending medium done.");
}

void sendHighSpeed() {
  rf.sendCommand(IthoHigh);
  CurrentState = "High";
  mqttclient.publish(statetopic, CurrentState.c_str());
  mqttclient.publish(idindextopic, "0");
  Serial.println("sending high done.");
}

void sendFullSpeed() {
  rf.sendCommand(IthoFull);
  CurrentState = "Full";
  mqttclient.publish(statetopic, CurrentState.c_str());
  mqttclient.publish(idindextopic, "0");
  Serial.println("sending FullSpeed done.");
}

void sendTimer1() {
  rf.sendCommand(IthoTimer1);
  CurrentState = "Timer1";
  mqttclient.publish(statetopic, CurrentState.c_str());
  mqttclient.publish(idindextopic, "0");
  Serial.println("sending timer1 done.");
}
void sendTimer2() {
  rf.sendCommand(IthoTimer2);
  CurrentState = "Timer2";
  mqttclient.publish(statetopic, CurrentState.c_str());
  mqttclient.publish(idindextopic, "0");
  Serial.println("sending timer2 done.");
}
void sendTimer3() {
  rf.sendCommand(IthoTimer3);
  CurrentState = "Timer3";
  mqttclient.publish(statetopic, CurrentState.c_str());
  mqttclient.publish(idindextopic, "0");
  Serial.println("sending timer3 done.");
}

// ROOT page
void handle_root()
{
  // get IP
  IPAddress ip = WiFi.localIP();
  ClientIP = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
  delay(500);

  String title1     = panelHeaderName + String("Itho WIFI remote") + panelHeaderEnd;
  String IPAddClient    = panelBodySymbol + String("globe") + panelBodyName + String("IP Address") + panelBodyValue + ClientIP + panelBodyEnd;
  String ClientName   = panelBodySymbol + String("tag") + panelBodyName + String("Client Name") + panelBodyValue + espName + panelBodyEnd;
  String ithoVersion   = panelBodySymbol + String("ok") + panelBodyName + String("Version") + panelBodyValue + Version + panelBodyEnd;
  String State   = panelBodySymbol + String("info-sign") + panelBodyName + String("Current state") + panelBodyValue + CurrentState + panelBodyEnd;
  String Uptime     = panelBodySymbol + String("time") + panelBodyName + String("Uptime") + panelBodyValue + hour() + String(" h ") + minute() + String(" min ") + second() + String(" sec") + panelBodyEnd + panelEnd;


  String title3 = panelHeaderName + String("Commands") + panelHeaderEnd;

  String commands = panelBodySymbol + panelBodyName + panelcenter + ithocontrol + panelBodyEnd;


  server.send ( 200, "text/html", header + navbar + containerStart + title1 + IPAddClient + ClientName + ithoVersion + State + Uptime + title3 + commands + containerEnd + siteEnd);
}
void handle_api()
{
  // Get var for all commands
  String action = server.arg("action");
  String value = server.arg("value");
  String api = server.arg("api");

  if (action == "Receive")
  {
    rf.initReceive();
    server.send ( 200, "text/html", "Receive mode");
  }

  if (action == "High")
  {
    sendFullSpeed();
    server.send ( 200, "text/html", "Full Powerrr!!!");
  }

  if (action == "Medium")
  {
    sendMediumSpeed();
    server.send ( 200, "text/html", "Medium speed selected");
  }

  if (action == "Low")
  {
    sendLowSpeed();
    server.send ( 200, "text/html", "Slow speed selected");
  }

  if (action == "Timer")
  {
    sendTimer1();
    server.send ( 200, "text/html", "Timer1 on selected");
  }

  if (action == "Join")
  {
    sendJoin();
    server.send ( 200, "text/html", "Send join command OK");
  }

  if (action == "Leave")
  {
    sendLeave();
    server.send ( 200, "text/html", "Send leave command OK");
  }
  
  if (action == "reset" && value == "true")
  {
    server.send ( 200, "text/html", "Reset ESP OK");
    delay(500);
    Serial.println("RESET");
    ESP.restart();
  }

}

void handle_buttons()
{
  // Get vars for all commands
  String action = server.arg("action");
  String api = server.arg("api");

  if (action == "High")
  {
    sendFullSpeed();
    handle_root();
  }

  if (action == "Medium")
  {
    sendMediumSpeed();
    handle_root();    
  }

  if (action == "Low")
  {
    sendLowSpeed();
    handle_root();
  }

  if (action == "Timer")
  {
    sendTimer1();
    handle_root();
  }

  if (action == "Join")
  {
    sendJoin();
    handle_root();
  }
  
  if (action == "Leave")
  {
    sendLeave();
    handle_root();
  }  

}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
 
  payload[length] = '\0';
  String strPayload = String((char*)payload);
  
    if (strPayload == "Join") {
      sendJoin();
    }
    else if (strPayload == "Leave"){
      sendLeave();
    }    
    else if (strPayload == "Standby"){
      sendStandbySpeed();
    }
    else if (strPayload == "Low"){
      sendLowSpeed();
    }    
    else if (strPayload == "Medium"){
      sendMediumSpeed();
    }    
    else if (strPayload == "High"){
      sendHighSpeed();
    }
    else if (strPayload == "Full"){
      sendFullSpeed();
    }    
    else if (strPayload == "Timer1"){
      sendTimer1();
    }
    else if (strPayload == "Timer2"){
      sendTimer2();
    }
    else if (strPayload == "Timer3"){
      sendTimer3();
    }
    else if (strPayload == "Reset"){
      ESP.restart();
    }
    else {
      Serial.println("Payload unknown");
    }    
  }



void reconnect() {
  // Loop until we're reconnected
  while (!mqttclient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqttclient.connect(mqttClientId, mqttUsername, mqttPassword)) {
      Serial.println("connected");
      // subscribe
      mqttclient.subscribe(commandtopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttclient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup(void) {
  Serial.println("\n*** setup begin ***\n");
  Serial.begin(115200);
  Serial.println("\n*** setup wifi ***\n");
  WiFi.mode(WIFI_STA);
  delay(500);
  WiFi.hostname(espName);

  WiFi.begin(ssid.c_str(), password.c_str());
  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i < 31)
  {
    delay(1000);
    Serial.print(".");
    ++i;
  }
  if (WiFi.status() != WL_CONNECTED && i >= 30)
  {
    WiFi.disconnect();
    delay(1000);
    Serial.println("");
    Serial.println("Couldn't connect to network :( ");
    Serial.println("Review your WIFI settings");

  }
  else
  {
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Hostname: ");
    Serial.println(espName);

  }
    
  Serial.println("\n*** setup RF ***\n");
  rf.init();


  Serial.println("\n*** setup webserver ***\n");
  server.on("/", handle_root);
  server.on("/api", handle_api);
  server.on("/button", handle_buttons);


  if (!mdns.begin(espName.c_str(), WiFi.localIP())) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }

  server.begin();
  Serial.println("HTTP server started");
  MDNS.addService("http", "tcp", 80);  

  Serial.println("\n*** setup MQTT ***\n");
  if (mqttHost == "") {
    mqttclient.setServer(mqttIP, 1883);
    Serial.println("setup MQTT using IP");
  }
  else {
    mqttclient.setServer(mqttHost, 1883);
    Serial.println("setup MQTT using hostname");
  }
    
  mqttclient.setCallback(callback);
  
  Serial.println("\n*** setup done ***\n");
  sendJoin();
  Serial.println("join command sent");
  pinMode(ITHO_IRQ_PIN, INPUT);
  attachInterrupt(ITHO_IRQ_PIN, ITHOinterrupt, RISING);
}

void loop(void) {
  server.handleClient();
  
  // do whatever you want, check (and reset) the ITHOhasPacket flag whenever you like
  if (ITHOhasPacket) {
    showPacket();
  }
  
  if (!mqttclient.connected()) {
    reconnect();
  }
  mqttclient.loop();  
}
