

#define VERSION "1.0"
// Access Point name over-ride
// if this is defined, use that instead of the stored "ident" string
#define AP_NAME "CornholeSwitch"

#include <WiFi.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <PubSubClient.h>         // https://github.com/plapointe6/EspMQTTClient and dependencies
#include <Ticker.h>
#include <Preferences.h>

#include <Adafruit_NeoPixel.h> 
// it seems GRB is the default
#define ORDER NEO_GRB
  
WiFiClient espClient;
WiFiManager wifiManager;
PubSubClient client(espClient);

Ticker ticker;

int LED_BUILTIN = 2;  // LED on the board connected to GPIO2
int BTN_BUILTIN = 3;  // BTN on GPIO3
int SWITCHS[6] = {4, 5, 6, 7, 8, 10};  // GPIO SWITCH CHNs

Adafruit_NeoPixel pixel = Adafruit_NeoPixel(1, LED_BUILTIN, ORDER); // one pixel, on pin referenced by LED_BUILTIN 

// flashes this colour when connecting to wifi:
static uint32_t wifi_colour = pixel.Color(128, 0, 128); // magenta

// flashes this colour when in AP config mode
static uint32_t config_colour = pixel.Color(0, 0, 128); // blue

// flashes this colour when connecting to MQTT:
static uint32_t mqtt_colour = pixel.Color(0, 128, 128); // cyan

static uint32_t current_colour = 0x000000; // black
static uint32_t current_LED = current_colour;

// broker connection information
struct MQTTConf{
  char ip_address[20];
  int port;
  unsigned int valid; // this should be set to 0xCAFEF00D when the confi is valid
};
char buffer[sizeof(MQTTConf)]; // prepare a buffer for the data
MQTTConf *mqtt_conf = (MQTTConf *) buffer;
WiFiManagerParameter wifi_manager_mqqt_server("mqtt_server", "MQTT Server IP Address", "192.168.1.120", 20);
WiFiManagerParameter wifi_manager_port("mqtt_port", "MQTT Port", "1885", 6);
char clientID[128];
Preferences prefs;

char mac_string[20]; // mac address

enum e_hole_state {HOLE_OFF = 0, HOLE_RED=1, HOLE_BLUE=2, HOLE_GREEN=3, HOLE_CYAN=4, HOLE_WHITE=5, 
                   HOLE_OLDLACE=6,HOLE_PURPLE=7, HOLE_MAGENTA=8, HOLE_YELLOW=9, HOLE_ORANGE=10,
                   HOLE_PINK=11 }; 
e_hole_state hole_state[6] = { HOLE_OFF, HOLE_OFF, HOLE_OFF, HOLE_OFF, HOLE_OFF, HOLE_OFF };

int current_button_state = 0;

unsigned long switch_polling_interval = 100;
unsigned long switch_hold_off = 3000;


void setup() {
  // put your setup code here, to run once:

  pinMode (LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  pinMode (BTN_BUILTIN, INPUT_PULLUP);

  for(int hole_id=1; hole_id<=6; hole_id++)
  {
    pinMode (SWITCHS[hole_id-1], INPUT_PULLUP);
  }
    
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  // it is a good practice to make sure your code sets wifi mode how you want it.
  // this fixes an issue where the C3 wasn't picking up previously saved settings so went into configuration mode 
  // on startup even though the correct SSID and password for an available network had been saved.

  pixel.begin();
  delay(10);
  pixel.show(); // Initialize all pixels to 'off'

  delay(500);

  Serial.println();
  Serial.println("Cornhole Switch");
  Serial.print("Firmware version ");
  Serial.println(VERSION);

  Serial.print("Acess Point name set to: ");
  Serial.println(AP_NAME);
  
  // make it wait longer when trying to connect
  wifiManager.setConnectTimeout(30);

  // display mac address
  mac_address();
      
  Serial.print("Looking for ");
  Serial.print(WiFi.SSID().c_str());
  Serial.print(" / ");
  Serial.println(WiFi.psk().c_str());
    
  // connecting to wifi colour
  set_colour(wifi_colour); 
  ticker.attach(1, tick);

  // configure the EEPROM
  prefs.begin("MQTT_Settings", false);

  // Uncomment and run it once, if you want to erase all the stored information
  current_button_state = digitalRead(BTN_BUILTIN);
  if (current_button_state == 0) {
    Serial.println("BTN pressed during initialisation, clearing the wifi details");
    wifiManager.resetSettings();
  }
  else
  {
      size_t record_len = prefs.getBytesLength("MQTT_Settings");
      Serial.print("prefs length:");
      Serial.println(record_len);
      prefs.getBytes("MQTT_Settings", buffer, sizeof(MQTTConf));
      Serial.println("mqtt conf retrieved:");
      Serial.print("ip address:");
      Serial.println(mqtt_conf->ip_address);
      Serial.print("port:");
      Serial.println(mqtt_conf->port);
      Serial.print("valid:");
      Serial.println(mqtt_conf->valid);
      // mqtt_conf is mapped onto buffer   
      if (mqtt_conf->valid != 0xCAFEF00D) {
        Serial.println("Invalid EEPROM content, clearin the WiFI details");
        // for reconfiguration of the system
        wifiManager.resetSettings();
      }
  }

  // call us back when it's connected so we can reset the pixel
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.addParameter(&wifi_manager_mqqt_server);
  wifiManager.addParameter(&wifi_manager_port);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  Serial.print("set the AP name it comes up as");
  // set the AP name it comes up as
  if (!wifiManager.autoConnect(AP_NAME)) {
    Serial.println("failed to connect to an access point");
    delay(2000);
  }
    
  Serial.println("connected to wifi!");
  ticker.detach();
    
  // reset the pixel to show we've connected successfully, before going for MQTT connection colour
  set_colour(0);

  Serial.print("connecting to broker: ");
  Serial.print(mqtt_conf->ip_address);
  Serial.print(":");
  Serial.println(mqtt_conf->port);

  client.setServer(mqtt_conf->ip_address, mqtt_conf->port);
  client.setCallback(mqtt_callback);


  // create unique client ID
  // last 2 octets of MAC address plus some time bits
  sprintf(clientID, "%s_%s_%05d","corn_hole_switch", (char *)(mac_string+9), millis()%100000);
 
  Serial.print("clientID: ");
  Serial.println(clientID);

}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());

  Serial.println(myWiFiManager->getConfigPortalSSID());

  ticker.detach();
  set_colour(config_colour);
  ticker.attach(1, tick);
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  char port_number_as_char_array[10];
  size_t ret_code = 0;
  Serial.print("mqtt server from wifimanager-----");
  strcpy(mqtt_conf->ip_address, wifi_manager_mqqt_server.getValue());
  strcpy(port_number_as_char_array, wifi_manager_port.getValue());
  mqtt_conf->port = String(port_number_as_char_array).toInt();
  mqtt_conf->valid = 0xCAFEF00D;
  Serial.println("mqtt conf retrieved:");
  Serial.print("ip address:");
  Serial.println(mqtt_conf->ip_address);
  Serial.print("port:");
  Serial.println(mqtt_conf->port);
  Serial.print("valid:");
  Serial.println(mqtt_conf->valid);
  ret_code = prefs.putBytes("MQTT_Settings", mqtt_conf, sizeof(MQTTConf)); 
  Serial.print("putBytes ret code:");
  Serial.println(ret_code);
  prefs.end();

  // check the readback
  prefs.begin("MQTT_Settings", true);
  char new_buffer[sizeof(MQTTConf)]; // prepare a buffer for the data
  MQTTConf *new_mqtt_conf = (MQTTConf *) new_buffer;
  prefs.getBytes("MQTT_Settings", new_buffer, sizeof(MQTTConf));
  Serial.println("read_back mqtt conf retrieved:");
  Serial.print("ip address:");
  Serial.println(new_mqtt_conf->ip_address);
  Serial.print("port:");
  Serial.println(new_mqtt_conf->port);
  Serial.print("valid:");
  Serial.println(new_mqtt_conf->valid);
  prefs.end();
}

void tick()
{
  toggle_pixel();
}

void mqtt_callback(char* topic, byte* payload, unsigned int length)
/*
Call back for handling inbound MQTT messages
 */
{
  char expected_topic_str[20];
  unsigned short hole_id;
  char payload_string[length];
  for (int i = 0; i<length; i++) { payload_string[i] = (char) payload[i]; }
  String message_payload = String(payload_string);
  
  Serial.print("Message arrived: ");

  sprintf(expected_topic_str, "switch/interval");
  Serial.print("checking message matched:");
  Serial.println(expected_topic_str);
  if (strcmp(topic, expected_topic_str) == 0) {
    switch_polling_interval = message_payload.toInt();
    Serial.print("updating switch_polling_interval to:");
    Serial.println(switch_polling_interval);
  }
  else
  {
    Serial.println("no match");  
  }

  sprintf(expected_topic_str, "switch/hold_off");
  Serial.print("checking message matched:");
  Serial.println(expected_topic_str);
  if (strcmp(topic, expected_topic_str) == 0) {
    switch_hold_off = message_payload.toInt();
    Serial.print("updating switch_hold_off to:");
    Serial.println(switch_hold_off);
  }
  else
  {
    Serial.println("no match");  
  }

  for(hole_id=1; hole_id<=6; hole_id++)
  {
    sprintf(expected_topic_str, "holes/%u", hole_id);
    Serial.print("checking message matched:");
    Serial.println(expected_topic_str);
    if (strcmp(topic, expected_topic_str) == 0) {
      Serial.print("match");  
      //Serial.println(message_payload);

      // decode the payload start by looking for the open/close curly brackets, this gets to the JSON record
      int start_bracket = message_payload.indexOf('{');
      int end_bracket = message_payload.indexOf('}');
      String json_content = message_payload.substring(start_bracket+1,end_bracket);
      Serial.println(json_content);

      if (json_content.indexOf("\"status\": true") != -1) {
        Serial.println("Hole On");
        if (json_content.indexOf("\"colour\": \"red\"") != -1) {
          set_colour(0xFF0000);  // red
          Serial.println("Hole Red");
          hole_state[hole_id-1] = HOLE_RED;
        }

        else if (json_content.indexOf("\"colour\": \"blue\"") != -1) {
            set_colour(0x0000FF); // blue
            Serial.println("Hole Blue");
            hole_state[hole_id-1] = HOLE_BLUE;
          }

        else if (json_content.indexOf("\"colour\": \"green\"") != -1) {
            set_colour(0x008000); // green
            Serial.println("Hole Green");
            hole_state[hole_id-1] = HOLE_GREEN;
          }

        else if (json_content.indexOf("\"colour\": \"cyan\"") != -1) {
            set_colour(0x00FFFF); // cyan
            Serial.println("Hole Cyan");
            hole_state[hole_id-1] = HOLE_CYAN;
          }

         else if (json_content.indexOf("\"colour\": \"white\"") != -1) {
            set_colour(0xFFFFFF); // white
            Serial.println("Hole White");
            hole_state[hole_id-1] = HOLE_WHITE;
          }

         else if (json_content.indexOf("\"colour\": \"oldlace\"") != -1) {
            set_colour(0xFDF5E6); // oldlace
            Serial.println("Hole Old Lace");
            hole_state[hole_id-1] = HOLE_OLDLACE;
          }

         else if (json_content.indexOf("\"colour\": \"purple\"") != -1) {
            set_colour(0x800080); // purple
            Serial.println("Hole Purple");
            hole_state[hole_id-1] = HOLE_PURPLE;
          }

        else if (json_content.indexOf("\"colour\": \"magenta\"") != -1) {
            set_colour(0xFF00FF); // magenta
            Serial.println("Hole Magenta");
            hole_state[hole_id-1] = HOLE_MAGENTA;
          }

        else if (json_content.indexOf("\"colour\": \"yellow\"") != -1) {
            set_colour(0xFFFF00); // yellow
            Serial.println("Hole Yellow");
            hole_state[hole_id-1] = HOLE_YELLOW;
          }

         else if (json_content.indexOf("\"colour\": \"orange\"") != -1) {
            set_colour(0xFFA500); // orange
            Serial.println("Hole Orange");
            hole_state[hole_id-1] = HOLE_ORANGE;
          }

       else  if (json_content.indexOf("\"colour\": \"pink\"") != -1) {
            set_colour(0xFFC0CB); // pink
            Serial.println("Hole Pink");
            hole_state[hole_id-1] = HOLE_PINK;
          }
       else {
           Serial.print("Unhandled colour message:");  
           Serial.println(json_content);
       }
        
      } // end of if hole state true

      else if (json_content.indexOf("\"status\": false") != -1) {
        Serial.println("Hole Off");
        set_colour(0); // clear pixel when connected (black)
        hole_state[hole_id-1] = HOLE_OFF;
      }
      else {
           Serial.print("Unhandled status message:");  
           Serial.println(json_content);
       }
      
      
    }
    else
    {
      Serial.println("no match");
    }
    
  }
}


void wait_for_wifi()
{
  
  Serial.println("waiting for Wifi");
  
  // connecting to wifi colour
  set_colour(wifi_colour);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    toggle_pixel();
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
   
}


void reconnect() {
  
  boolean first = true;

  // Loop until we're reconnected to the broker
  while (!client.connected()) {

    if (WiFi.status() != WL_CONNECTED) {
      ticker.detach();
      wait_for_wifi();
      first = true;
    }

    Serial.print("Attempting MQTT connection...");
    if (first) {
      // now we're on wifi, show connecting to MQTT colour
      set_colour(mqtt_colour);
      first = false;

      // flash every 2 sec while we're connecting to broker
      ticker.attach(2,tick);
    }

    if (client.connect(clientID)) {
      Serial.println("connected");
    } 
    else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      
      // wait 5 seconds to prevent connect storms
      delay(5000);
    }
    
  }

  ticker.detach();

  delay(1000); // wait a second to make sure they saw the mqtt_colour
  set_colour(0); // clear pixel when connected (black)
  
  // subscribe to the command topic
  Serial.println("subscribing to:'");
  char expected_topic_str[20];
  unsigned short hole_id;

  sprintf(expected_topic_str, "switch/interval");
  client.subscribe(expected_topic_str);
  sprintf(expected_topic_str, "switch/hold_off");
  client.subscribe(expected_topic_str);
  
  for(hole_id=1; hole_id<=6; hole_id++)
  {
    sprintf(expected_topic_str, "holes/%u", hole_id);
    Serial.print("'");
    Serial.print(expected_topic_str);
    client.subscribe(expected_topic_str);
    Serial.println("'");
  }
  Serial.println("Setup Over");
}


void set_colour(uint32_t colour) {
  
  set_pixels(colour);
  // Updates current_LED with what the user last requested,
  // so we can toggle it to black and back again.

  // DJM TODO convert colour to RGB656, update screen on M5STICK-C -> M5.Lcd.fillScreen() refers
  
  current_colour = colour;
}



void set_pixels(uint32_t colour) {
  
  for (int i = 0; i < pixel.numPixels(); i++) {
    pixel.setPixelColor(i, colour);
  }
  pixel.show();

  // Store current actual LED colour
  // (which may be black if toggling code turned it off.)
  current_LED = colour;
}


void toggle_pixel() {

  if (current_LED == 0) 
  {
    // if it's black, set it to the stored colour
    set_pixels(current_colour);
  } 
  else
  {
    // otherwise set it to black
    set_pixels(0);
  }
}

unsigned long time_last_heartbeat = millis();  
unsigned long time_now; 
char heatbeat_topic_name[20]="switch/heartbeat";
char heatbeat_payload[60];
unsigned long next_switch_time[6] =  {millis(), millis(), millis(), millis(), millis(), millis()}; 
int current_switch_state = 0;
int previous_switch_state[6] = {0, 0, 0, 0, 0, 0};
int hold_off_button = 0;
char btn_press_topic_name[20]="switch/1";
char btn_press_payload[60];

void loop() {
    if (!client.connected()) {
      reconnect();
    }
    
    // service the MQTT  client
    client.loop();

    time_now = millis();
    

    // send the periodic heartbeat
    if ((time_now - time_last_heartbeat ) >= 30000) {
      sprintf(heatbeat_payload,"%u%" , time_now);
      client.publish(heatbeat_topic_name, heatbeat_payload);
      Serial.println("Heatbeat sent");
      time_last_heartbeat = millis();
    }

    // check for changes in the button state
    for(int hole_id=1; hole_id<=6; hole_id++)
    {
      if (time_now >  next_switch_time[hole_id-1]) {
        current_switch_state = digitalRead(SWITCHS[hole_id-1]);
        if ((previous_switch_state[hole_id-1] == 0) & (current_switch_state == 1)) // button is pulled up and grounded when pressed
        {
          // button press detected
          Serial.print("button press");
          Serial.println(hole_id);
          
          if (hole_state[hole_id-1] == HOLE_RED) {
            sprintf(btn_press_payload,"{\"time\":%u%, \"colour\": \"red\"}" , time_now);
          }
          else if (hole_state[hole_id-1] == HOLE_BLUE) {
            sprintf(btn_press_payload,"{\"time\":%u%, \"colour\": \"blue\"}" , time_now);
          }
          else if (hole_state[hole_id-1] == HOLE_GREEN) {
            sprintf(btn_press_payload,"{\"time\":%u%, \"colour\": \"green\"}" , time_now);
          }
          else if (hole_state[hole_id-1] == HOLE_CYAN) {
            sprintf(btn_press_payload,"{\"time\":%u%, \"colour\": \"cyan\"}" , time_now);
          }
          else if (hole_state[hole_id-1] == HOLE_WHITE) {
            sprintf(btn_press_payload,"{\"time\":%u%, \"colour\": \"white\"}" , time_now);
          }
          else if (hole_state[hole_id-1] == HOLE_OLDLACE) {
            sprintf(btn_press_payload,"{\"time\":%u%, \"colour\": \"oldlace\"}" , time_now);
          }
          else if (hole_state[hole_id-1] == HOLE_PURPLE) {
            sprintf(btn_press_payload,"{\"time\":%u%, \"colour\": \"purple\"}" , time_now);
          }
          else if (hole_state[hole_id-1] == HOLE_MAGENTA) {
            sprintf(btn_press_payload,"{\"time\":%u%, \"colour\": \"magenta\"}" , time_now);
          }
          else if (hole_state[hole_id-1] == HOLE_YELLOW) {
            sprintf(btn_press_payload,"{\"time\":%u%, \"colour\": \"yellow\"}" , time_now);
          }
          else if (hole_state[hole_id-1] == HOLE_ORANGE) {
            sprintf(btn_press_payload,"{\"time\":%u%, \"colour\": \"orange\"}" , time_now);
          }
          else if (hole_state[hole_id-1] == HOLE_PINK) {
            sprintf(btn_press_payload,"{\"time\":%u%, \"colour\": \"pink\"}" , time_now);
          }
          else if (hole_state[hole_id-1] == HOLE_OFF) {
            sprintf(btn_press_payload,"{\"time\":%u%, \"colour\": \"off\"}" , time_now);
          }
          else {
            Serial.print("Unhandled hole_state:");
            Serial.println(hole_state[hole_id-1]);
          }
          sprintf(btn_press_topic_name, "switch/%d", hole_id);
          client.publish(btn_press_topic_name, btn_press_payload); 
          next_switch_time[hole_id-1] = time_now + switch_hold_off; // hold off before next time
          previous_switch_state[hole_id-1] = current_switch_state;
        }
        else {
          next_switch_time[hole_id-1] = time_now + switch_polling_interval; // next time round the loop
          previous_switch_state[hole_id-1] = current_switch_state;
        }
      }
    }
}

void mac_address() {
  byte mac[6];                   

  WiFi.macAddress(mac);   // the MAC address of your Wifi shield

  sprintf(mac_string, "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

  Serial.println(mac_string);
}
