#include <Arduino.h>
#include <ESP8266WiFi.h>              // Connect to Wifi

#include "wifi_manager.h"
#include "dns.h"
#include "debug.h"

#define WIFI_CONNECTION_LED       0
#define WIFI_CONNECTION_LED_STATE LOW
#define WIFI_CLIENT_MAX_DISCONNECTS 3

#define wifi_is_client_configured()   (WiFi.SSID() != "")

// Wifi mode
#define wifi_mode_is_sta()            (WIFI_STA == (WiFi.getMode() & WIFI_STA))
#define wifi_mode_is_sta_only()       (WIFI_STA == WiFi.getMode())
#define wifi_mode_is_ap()             (WIFI_AP == (WiFi.getMode() & WIFI_AP))

// Performing a scan enables STA so we end up in AP+STA mode so treat AP+STA with no
// ssid set as AP only
#define wifi_mode_is_ap_only()        ((WIFI_AP == WiFi.getMode()) || \
                                       (WIFI_AP_STA == WiFi.getMode() && !wifi_is_client_configured()))

WiFiManagerTask::WiFiManagerTask(String hostname, String ssid, String password) :
  scanCompleteEvent(),
  softAP_ssid(hostname+"_"+String(ESP.getChipId())),
  softAP_password(""),
  apIP(192, 168, 4, 1),
  netMask(255, 255, 255, 0),
  apClients(0),
  client_ssid(ssid),
  client_password(password),
  hostname(hostname),
  client_disconnects(0),
  client(false),
  timeout(millis()),
  scan(false),
  wifiState(-1),
  wifiLedState(!WIFI_CONNECTION_LED_STATE),
  dns(),
  MicroTasks::Task()
{
}

WiFiManagerTask::WiFiManagerTask() :
  scanCompleteEvent(),
  softAP_ssid("_"+String(ESP.getChipId())),
  softAP_password(""),
  apIP(192, 168, 4, 1),
  netMask(255, 255, 255, 0),
  apClients(0),
  client_ssid(""),
  client_password(""),
  hostname(""),
  client_disconnects(0),
  client(false),
  timeout(millis()),
  scan(false),
  wifiState(-1),
  wifiLedState(!WIFI_CONNECTION_LED_STATE),
  dns(),
  MicroTasks::Task()
{
}

void WiFiManagerTask::setHostName(String hostname) {
  this->hostname = hostname;
  this->softAP_ssid = hostname+"_"+String(ESP.getChipId());
}

void WiFiManagerTask::setClientDetails(String ssid, String password) {
  client_ssid = ssid;
  client_password = password;
}

void WiFiManagerTask::setup()
{
  DBUGF("hostname = '%s'", hostname.c_str());
  DBUGF("client_ssid = '%s'", client_ssid.c_str());
  DBUGF("client_password = '%s'", client_password.c_str());

  pinMode(WIFI_CONNECTION_LED, OUTPUT);
  digitalWrite(WIFI_CONNECTION_LED, wifiLedState);

  // Seed the number generator, channel selection for AP mode 
  randomSeed(analogRead(0));

  // If we have an SSID configured at this point we have likely
  // been running another firmware, clear the results
  if(wifi_is_client_configured()) {
    WiFi.persistent(true);
    WiFi.disconnect();
    ESP.eraseConfig();
  }

  // Stop the WiFi module
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);

  // Connect to the WiFi events
  static auto _onStationModeConnected = WiFi.onStationModeConnected([this](const WiFiEventStationModeConnected &event) { DBUGF("Connected to %s", event.ssid.c_str()); });
  static auto _onStationModeGotIP = WiFi.onStationModeGotIP([this](const WiFiEventStationModeGotIP &event) { onStationModeGotIP(event); });
  static auto _onStationModeDisconnected = WiFi.onStationModeDisconnected([this](const WiFiEventStationModeDisconnected &event) { onStationModeDisconnected(event); });
  static auto _onSoftAPModeStationConnected = WiFi.onSoftAPModeStationConnected([this](const WiFiEventSoftAPModeStationConnected &event) {
    apClients++;
  });
  static auto _onSoftAPModeStationDisconnected = WiFi.onSoftAPModeStationDisconnected([this](const WiFiEventSoftAPModeStationDisconnected &event) {
    apClients--;
  });

  if(client_ssid != "") {
    startClient();
  } else {
    startAP();
  }
}

unsigned long WiFiManagerTask::loop(MicroTasks::WakeReason reason)
{
  if(scan)
  {
    int n = WiFi.scanComplete();
    if(WIFI_SCAN_RUNNING != n)
    {
      DBUGF("Complete, found %d", n);
      scanCompleteEvent.ScanComplete();
      scan = false;
    } else {
      DBUGF("Scanning... (%d)", n);
    }
  }

  return client ? loopClient() : loopAP();
}

void WiFiManagerTask::startAP()
{
  DBUGF("Starting AP %s, pass %s", softAP_ssid.c_str(), softAP_password.c_str());
  if (wifi_mode_is_sta()) {
    WiFi.disconnect(true);
  }

  apClients = 0;

  WiFi.enableAP(true);

  // Start up the AP
  WiFi.softAPConfig(apIP, apIP, netMask);

  int channel = (random(3) * 5) + 1;
  WiFi.softAP(softAP_ssid.c_str(), softAP_password.c_str(), channel);

  // Start the DNS task
  dns.setIp(apIP);
  MicroTask.startTask(dns);

  client = false;
}

unsigned long WiFiManagerTask::loopAP()
{
  wifiLedState = !wifiLedState;
  digitalWrite(WIFI_CONNECTION_LED, wifiLedState);

  if(client_ssid == "") {
    // No client SSID set, nothing to do..
    return 1000;
  }

  // Scan for the client SSID
  int n = WiFi.scanComplete();
  if(WIFI_SCAN_RUNNING != n)
  {
    DBUGF("%d networks found", n);
    for (int i = 0; i < n; ++i) {
      if(client_ssid == WiFi.SSID(i)) {
        startClient();
        return 0;
      }
    }

    StartScan();
  }

  return 1000;
}

void WiFiManagerTask::stopAP()
{
}

void WiFiManagerTask::startClient()
{
  DBUGF("Connecting to %s, pass %s", client_ssid.c_str(), client_password.c_str());
  WiFi.disconnect();

  client_disconnects = 0;

  WiFi.begin(client_ssid.c_str(), client_password.c_str());
  WiFi.hostname(hostname);
  WiFi.enableSTA(true);

  client = true;
}

unsigned long WiFiManagerTask::loopClient()
{
  //DBUGF("Loop Client");

  if(WiFi.isConnected())
  {
    // We are connected nothing to do...
    return MicroTask.Infinate;
  }

  wifiState = WiFi.status();
  wifiLedState = !wifiLedState;
  digitalWrite(WIFI_CONNECTION_LED, wifiLedState);

  if(client_disconnects >= WIFI_CLIENT_MAX_DISCONNECTS) {
    // timeout connecting to AP
    startAP();
  }

  return 500;
}

void WiFiManagerTask::stopClient()
{
}

void WiFiManagerTask::StartScan()
{
  WiFi.scanNetworks(true);
  scan = true;
  DBUGF("Start scan");
}

void WiFiManagerTask::onScanComplete(MicroTasks::EventListener& eventListener)
{
  scanCompleteEvent.Register(&eventListener);
}


void WiFiManagerTask::onStationModeGotIP(const WiFiEventStationModeGotIP &event)
{
  DBUGLN("Connected");
  wifiLedState = WIFI_CONNECTION_LED_STATE;
  digitalWrite(WIFI_CONNECTION_LED, wifiLedState);
  wifiState = WL_CONNECTED;
}

void WiFiManagerTask::onStationModeDisconnected(const WiFiEventStationModeDisconnected &event)
{
  DBUGF("WiFi dissconnected: %s",
  WIFI_DISCONNECT_REASON_UNSPECIFIED == event.reason ? "WIFI_DISCONNECT_REASON_UNSPECIFIED" :
  WIFI_DISCONNECT_REASON_AUTH_EXPIRE == event.reason ? "WIFI_DISCONNECT_REASON_AUTH_EXPIRE" :
  WIFI_DISCONNECT_REASON_AUTH_LEAVE == event.reason ? "WIFI_DISCONNECT_REASON_AUTH_LEAVE" :
  WIFI_DISCONNECT_REASON_ASSOC_EXPIRE == event.reason ? "WIFI_DISCONNECT_REASON_ASSOC_EXPIRE" :
  WIFI_DISCONNECT_REASON_ASSOC_TOOMANY == event.reason ? "WIFI_DISCONNECT_REASON_ASSOC_TOOMANY" :
  WIFI_DISCONNECT_REASON_NOT_AUTHED == event.reason ? "WIFI_DISCONNECT_REASON_NOT_AUTHED" :
  WIFI_DISCONNECT_REASON_NOT_ASSOCED == event.reason ? "WIFI_DISCONNECT_REASON_NOT_ASSOCED" :
  WIFI_DISCONNECT_REASON_ASSOC_LEAVE == event.reason ? "WIFI_DISCONNECT_REASON_ASSOC_LEAVE" :
  WIFI_DISCONNECT_REASON_ASSOC_NOT_AUTHED == event.reason ? "WIFI_DISCONNECT_REASON_ASSOC_NOT_AUTHED" :
  WIFI_DISCONNECT_REASON_DISASSOC_PWRCAP_BAD == event.reason ? "WIFI_DISCONNECT_REASON_DISASSOC_PWRCAP_BAD" :
  WIFI_DISCONNECT_REASON_DISASSOC_SUPCHAN_BAD == event.reason ? "WIFI_DISCONNECT_REASON_DISASSOC_SUPCHAN_BAD" :
  WIFI_DISCONNECT_REASON_IE_INVALID == event.reason ? "WIFI_DISCONNECT_REASON_IE_INVALID" :
  WIFI_DISCONNECT_REASON_MIC_FAILURE == event.reason ? "WIFI_DISCONNECT_REASON_MIC_FAILURE" :
  WIFI_DISCONNECT_REASON_4WAY_HANDSHAKE_TIMEOUT == event.reason ? "WIFI_DISCONNECT_REASON_4WAY_HANDSHAKE_TIMEOUT" :
  WIFI_DISCONNECT_REASON_GROUP_KEY_UPDATE_TIMEOUT == event.reason ? "WIFI_DISCONNECT_REASON_GROUP_KEY_UPDATE_TIMEOUT" :
  WIFI_DISCONNECT_REASON_IE_IN_4WAY_DIFFERS == event.reason ? "WIFI_DISCONNECT_REASON_IE_IN_4WAY_DIFFERS" :
  WIFI_DISCONNECT_REASON_GROUP_CIPHER_INVALID == event.reason ? "WIFI_DISCONNECT_REASON_GROUP_CIPHER_INVALID" :
  WIFI_DISCONNECT_REASON_PAIRWISE_CIPHER_INVALID == event.reason ? "WIFI_DISCONNECT_REASON_PAIRWISE_CIPHER_INVALID" :
  WIFI_DISCONNECT_REASON_AKMP_INVALID == event.reason ? "WIFI_DISCONNECT_REASON_AKMP_INVALID" :
  WIFI_DISCONNECT_REASON_UNSUPP_RSN_IE_VERSION == event.reason ? "WIFI_DISCONNECT_REASON_UNSUPP_RSN_IE_VERSION" :
  WIFI_DISCONNECT_REASON_INVALID_RSN_IE_CAP == event.reason ? "WIFI_DISCONNECT_REASON_INVALID_RSN_IE_CAP" :
  WIFI_DISCONNECT_REASON_802_1X_AUTH_FAILED == event.reason ? "WIFI_DISCONNECT_REASON_802_1X_AUTH_FAILED" :
  WIFI_DISCONNECT_REASON_CIPHER_SUITE_REJECTED == event.reason ? "WIFI_DISCONNECT_REASON_CIPHER_SUITE_REJECTED" :
  WIFI_DISCONNECT_REASON_BEACON_TIMEOUT == event.reason ? "WIFI_DISCONNECT_REASON_BEACON_TIMEOUT" :
  WIFI_DISCONNECT_REASON_NO_AP_FOUND == event.reason ? "WIFI_DISCONNECT_REASON_NO_AP_FOUND" :
  WIFI_DISCONNECT_REASON_AUTH_FAIL == event.reason ? "WIFI_DISCONNECT_REASON_AUTH_FAIL" :
  WIFI_DISCONNECT_REASON_ASSOC_FAIL == event.reason ? "WIFI_DISCONNECT_REASON_ASSOC_FAIL" :
  WIFI_DISCONNECT_REASON_HANDSHAKE_TIMEOUT == event.reason ? "WIFI_DISCONNECT_REASON_HANDSHAKE_TIMEOUT" :
  "UNKNOWN");

  client_disconnects++;
}
