/**
 * Simple RGB Stripe Controller
 * 
 * Anleitung:
 * http://smarthomeyourself.de/anleitung-zum-selber-bauen/rgb-controller/
 * 
 * Die erste einfache Version eines RGB-Stripe Controllers.
 * Das Sketch steuert 3 PWM-Pins an. Je einen für Rot, Grün und Blau. 
 * Je nach Board müssen die Pins angepasst werden (nicht alle sind PWM-fähig)
 * 
 * Angesprochen wird dieser Controller mit den parametern der Standard-Sender.
 * 
 * Beispiel:
 * http://IP_DIESES_CONTROLLERS/rawCmd?red=RRR&green=GGG&blue=BBB
 * 
 * Parameter:
 *     red   = 0-255
 *     green = 0-255
 *     blue  = 0-255
 *     
 * Der Parameter-Wert gibt an, wie hell die jeweilige Farbe sein soll. 
 * Auf diese Weise lassen sich beliebige Farben mischen. 
 * Werden alle Parameter auf 0 gesetzt entspricht das schwarz und schaltet die LEDs aus
 */
#include <EEPROM.h>
#include <SPI.h>
#include <Ethernet.h>  // Bei Ethernetshield hier Ethernet.h anstatt UIPEthernet.h einbinden
#include <SoftwareSerial.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiAvrI2c.h>


// ---------------------------------------------------------------
// --                      START CONFIG                         --
// ---------------------------------------------------------------

// Display
boolean useDisplay = false;
#define OLED_I2C_ADDRESS 0x3C
// Display-Timeout in ms
long displayTimeout = 60000; 

// Die 3 RGB-Pins müssen PWM-fähig sein
#define RED_PIN 3
#define GREEN_PIN 5
#define BLUE_PIN 6

// Netzwerk
unsigned char _mac[]  = {0xB2, 0xAB, 0x32, 0x56, 0xFE, 0x0D  };
unsigned char _ip[]   = { 192, 168, 1, 130 };
unsigned char _dns[]  = { 192, 168, 1, 15  };
unsigned char _gate[] = { 192, 168, 1, 15  };
unsigned char _mask[] = { 255, 255, 255, 0  };

// EEPROM
// Ist das EEPROM aktiviert, werden bei Neustart die zuletzt verwendeten Farbwerte geladen. 
// Bei Deaktiviertem EEPROM wird mit Default R=0, G=0, B=0 gestartet.
// Deaktivieren um EEPROM zu schonen (da maximal 100.000 Schreibzyklen)
boolean useEepromToStoreSettings = false;

// Serielle Ausgabe zu Debugzwecken aktivierbar
boolean serialOut = true;

// ---------------------------------------------------------------
// --                       END CONFIG                          --
// ---------------------------------------------------------------



// Netzwerkdienste
EthernetServer HttpServer(80); 
EthernetClient interfaceClient;

// Display 
SSD1306AsciiAvrI2c oled;

// Webseiten/Parameter
char*      rawCmdParam      = (char*)malloc(sizeof(char)*10);

const int  MAX_BUFFER_LEN           = 80; // max characters in page name/parameter 
char       buffer[MAX_BUFFER_LEN+1]; // additional character for terminating null

#if defined(__SAM3X8E__)
    #undef __FlashStringHelper::F(string_literal)
    #define F(string_literal) string_literal
#endif

const __FlashStringHelper * htmlHeader;
const __FlashStringHelper * htmlHead;
const __FlashStringHelper * htmlFooter;

// ------------------ Reset stuff --------------------------
void(* resetFunc) (void) = 0;
unsigned long resetMillis;
boolean resetSytem = false;
// --------------- END - Reset stuff -----------------------

long lastAction = -1;
long chkSum  = -1;
long chkDisplaySum = -1;

boolean idle = false;


byte red=-1;
byte green=-1;
byte blue=-1;

// EEPROM 
struct RgbData {
  byte r;
  byte g;
  byte b;
};

RgbData rgbDataStore  = {0,0,0};
int eepromChkVal = 34361;



void setup() {
  // Serial initialisieren
  Serial.begin(9600);
  Serial.println(F("SmartHome yourself - RGB Controller"));
  Serial.println();
  delay(500);
  
  // Display
  if(useDisplay){
    oled.begin(&Adafruit128x64, OLED_I2C_ADDRESS);
    oled.setFont(System5x7);
    oled.clear();
    oled.home();
  }

  // Netzwerk initialisieren
  Ethernet.begin(_mac, _ip, _dns, _gate, _mask);
  HttpServer.begin();
  Serial.print( F("IP: ") );
  Serial.println(Ethernet.localIP());
  
  initStrings();
  
  displayIntro();
  
  readSettings();
  
  switchRGB(rgbDataStore.r, rgbDataStore.g, rgbDataStore.b);
}



void loop() {
  EthernetClient client = HttpServer.available();
  if (client) {
    while (client.connected()) {
      if(client.available()){        
        if(serialOut){
          Serial.println(F("Website anzeigen"));
        }
        showWebsite(client);
        
        delay(100);
        client.stop();
      }
    }
  }
  delay(100);
  
  checkDisplayOutput();

    
  // Gecachte URL-Parameter leeren
  memset(rawCmdParam,0, sizeof(rawCmdParam));
}




// ---------------------------------------
//     RGB Hilfsmethoden
// ---------------------------------------
void switchRGB(int _red, int _green, int _blue){
  if(serialOut){
    Serial.print(F("Schalte RGB: "));
    Serial.print(red);
    Serial.print(F(" / "));
    Serial.print(green);
    Serial.print(F(" / "));
    Serial.print(blue);  
  }
    
  analogWrite(RED_PIN,   _red);
  analogWrite(GREEN_PIN, _green);
  analogWrite(BLUE_PIN,  _blue);

  rgbDataStore.r = _red;
  rgbDataStore.g = _green;
  rgbDataStore.b = _blue;

  writeSettings(rgbDataStore);
}



// ---------------------------------------
//     Display Hilfsmethoden
// ---------------------------------------
/**
 * Display-Ausgabe aktualisieren
 */
void checkDisplayOutput(){
  if(useDisplay){
    if (red>=0 && green>=0 && blue>=0 && chkDisplaySum!=(red+(green*1000)+(blue*1000000))) {
      clearDisplay(false);
      oled.setCursor(0,5);
      oled.print(F("R: "));
      oled.print(red);
      oled.print("  ");
      
      oled.setCursor(45,5);
      oled.print(F("G: "));
      oled.print(green);
      oled.print("  ");
      
      oled.setCursor(90,5);
      oled.print(F("B: "));
      oled.print(blue);
      oled.print("  ");
  
      lastAction = millis();
      chkDisplaySum=(red+(green*1000)+(blue*1000000));
    }
      
    if(lastAction + displayTimeout < millis()){
      clearDisplay();
      idle=true;
    }
  }
}

/**
 * Display vollständig leeren
 */
void clearDisplay(){
  clearDisplay(true);
}

/**
 * Display leeren
 * 
 * @param boolean fullClear: 
 *   true  Bildschirm wird geleert
 *   false wird der Standard-Header/Footer 
 *         nach dem Clear eingeblendet.                   
 */
void clearDisplay(boolean fullClear){
  if(useDisplay){
    oled.clear();
    
    if (!fullClear){
      oled.setFont(Verdana12_bold);
      oled.setCursor(0, 1);
      oled.print(F("SmartHome yourself"));
      
      oled.setFont(System5x7);
      oled.setCursor(0, 2);
      oled.print(F("RGB Controller"));
  
      oled.setCursor(0, 7);
      oled.print(F("IP: "));
      oled.print(Ethernet.localIP());
    }
  }
}

/**
 * Intro auf Display abspielen
 */
void displayIntro(){
  if(useDisplay){
    if(serialOut){
      Serial.println("Display Intro");
    }
    oled.set2X();
    oled.setCursor(0, 1);
    oled.print(F("SmartHome"));
    delay(750);
    oled.setCursor(0, 3);
    oled.print(F("yourself"));
    delay(1500);
    oled.set1X();
    oled.setCursor(0, 6);
    oled.print(F("RGB Controller"));
  
    delay(5000);
    oled.clear();
  }
}



// ---------------------------------------
//     EEPROM Hilfsmethoden
// ---------------------------------------
/**
 * Farbeinstellungen im EEPROM speichern
 */
void writeSettings(struct RgbData myRgbData){
  if(!useEepromToStoreSettings){
    return;
  }
  
  int eeAddress = 0;

  EEPROM.put(eeAddress, eepromChkVal);
  eeAddress += sizeof(eepromChkVal);

  EEPROM.put(eeAddress, myRgbData.r);
  eeAddress += sizeof(myRgbData.r);

  EEPROM.put(eeAddress, myRgbData.g);
  eeAddress += sizeof(myRgbData.g);

  EEPROM.put(eeAddress, myRgbData.b);
  eeAddress += sizeof(myRgbData.b);
  if(serialOut){
    Serial.println(F("Write to EEPROM"));
    Serial.print(F("Red: "));
    Serial.println(rgbDataStore.r);
    Serial.print(F("Green: "));
    Serial.println(rgbDataStore.g);
    Serial.print(F("Blue: "));
    Serial.println(rgbDataStore.b);
  }
}

/**
 * Farbeinstellungen aus EEPROM auslesen.
 * (setzt red, green und blue)
 * Sofern noch keine Daten hinterlegt sind, 
 * werden aktuelle Farbwerte gespeichert.
 *(Wenn useEepromToStoreSettings = false ist, werden red, green und blue nur auf 0 gesetzt)
 */
void readSettings(){
  if(!useEepromToStoreSettings){
    red = 0;
    green = 0;
    blue = 0;
    
    return;
  }
  int eeAddress = 0;
  int tmp = -1;
  
  EEPROM.get(eeAddress, tmp);
  // Wenn Prüfwert auf Adresse 0 OK ist (= Daten wurden bereits gespeichert)
  if(tmp == eepromChkVal){
    eeAddress += eeAddress+sizeof(eepromChkVal);
    EEPROM.get(eeAddress, rgbDataStore);

    red = rgbDataStore.r;
    green = rgbDataStore.g;
    blue = rgbDataStore.b;
    if(serialOut){
      Serial.println(F("Read from EEPROM"));
      Serial.print(F("Red: "));
      Serial.println(rgbDataStore.r);
      Serial.print(F("Green: "));
      Serial.println(rgbDataStore.g);
      Serial.print(F("Blue: "));
      Serial.println(rgbDataStore.b);
    }
  } else {
    if(serialOut){
      Serial.println(F("First Time Write defaults to EEPROM"));
    }

    writeSettings(rgbDataStore);
  }
}




// ---------------------------------------
//     Webserver Hilfsmethoden
// ---------------------------------------
/**
 * Auswerten der URL Parameter
 */
void pruefeURLParameter(char* tmpName, char* value){
  if(strcmp(tmpName, "red")==0 && strcmp(value, "")!=0){
    strcpy(rawCmdParam, value);
    red = atoi(rawCmdParam);
  }  
  if(strcmp(tmpName, "green")==0 && strcmp(value, "")!=0){
    strcpy(rawCmdParam, value);
    green = atoi(rawCmdParam);
  }  
  if(strcmp(tmpName, "blue")==0 && strcmp(value, "")!=0){
    strcpy(rawCmdParam, value);
    blue = atoi(rawCmdParam);
  }  
}


/**
 *  URL auswerten und entsprechende Seite aufrufen
 */
void showWebsite(EthernetClient client){
  char * HttpFrame =  readFromClient(client);
  
 // delay(200);
  boolean pageFound = false;
  
  char *ptr = strstr(HttpFrame, "favicon.ico");
  if(ptr){
    pageFound = true;
  }
  /*ptr = strstr(HttpFrame, "index.html");
  if (!pageFound && ptr) {
    runIndexWebpage(client);
    pageFound = true;
  }*/
  ptr = strstr(HttpFrame, "rawCmd");
  if(!pageFound && ptr){
    runRawCmdWebpage(client, HttpFrame);
    pageFound = true;
  } 

  delay(200);

  ptr=NULL;
  HttpFrame=NULL;

 if(!pageFound){
    runRawCmdWebpage(client, HttpFrame);
  }
  delay(20);
}




// ---------------------------------------
//     Webseiten
// ---------------------------------------
/**
 * Startseite anzeigen
 */
void  runIndexWebpage(EthernetClient client){
  showHead(client);

  client.print(F("<h4>Navigation</h4><br/>"
    "<a href='/rawCmd'>Manuelle Schaltung</a><br>"));

  showFooter(client);
}


/**
 * rawCmd anzeigen
 */
void  runRawCmdWebpage(EthernetClient client, char* HttpFrame){
  if (red>=0 && green>=0 && blue>=0 && chkSum!=(red+(green*1000)+(blue*1000000))) {
    switchRGB(red, green, blue);
    chkSum = (red+(green*1000)+(blue*1000000));
  }
  delay(100);
  showHead(client);
  
  client.println(F(  "<form  method='GET' action='/rawCmd'>"));
  client.print   (F( "<table><tr>"));
  client.print   (F( "<td></td><td><center>Rot</center></td><td><center>Gr&uuml;n</center></td><td></center>Blau</td>"));
  client.print   (F( "</tr><tr>"));

  client.print   (F( "<td><b>Farbe: </b></td><td>" 
                     "<input type='input' name='red' value='"));
  client.print   (red);
  client.println (F( "' maxlength='4' size='4'></td><td><input type='input' name='green' value='"));
  client.print   (green);
  client.println (F( "' maxlength='4' size='4'></td><td><input type='input' name='blue' value='"));
  client.print   (blue);
  client.println (F( "' maxlength='4' size='4'></td>"));

  client.print   (F( "</tr><tr>"));
                    
  client.println (F( "<td colspan=4><br/><input type='submit' value='Abschicken'/></td>"
                     "</form>"));

  client.print   (F( "</tr></table>"));

  showFooter(client);
}





// ---------------------------------------
//     HTML-Hilfsmethoden
// ---------------------------------------

void showHead(EthernetClient client){
  client.println(htmlHeader);
  client.print("IP: ");
  client.println(Ethernet.localIP());
  client.println(htmlHead);
}


void showFooter(EthernetClient client){
  client.print(htmlFooter);
}


void initStrings(){
  htmlHeader = F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n");
  
  htmlHead = F("<html><head>"
    "<title>SmartHome yourself - RGB Controller</title>"
    "<style type=\"text/css\">"
    "body{font-family:sans-serif}"
    "*{font-size:14pt}"
    "a{color:#abfb9c;}"
    "</style>"
    "</head><body text=\"white\" bgcolor=\"#494949\">"
    "<center>"
    "<hr><h2>SmartHome yourself - RGB Controller</h2><hr>") ;
    
    htmlFooter = F( "</center>"
    "<a  style=\"position: absolute;left: 30px; bottom: 20px; \"  href=\"/\">Zurueck zum Hauptmenue;</a>"
    "</body></html>");   
}


// ---------------------------------------
//     Ethernet - Hilfsmethoden
// ---------------------------------------
/**
 * Zum auswerten der URL des ÃƒÂ¼bergebenen Clients
 * (implementiert um angeforderte URL am lokalen Webserver zu parsen)
 */
char* readFromClient(EthernetClient client){
  char paramName[20];
  char paramValue[20];
  char pageName[20];
  
  if (client) {
    while (client.connected()) {
      if (client.available()) {
        memset(buffer,0, sizeof(buffer)); // clear the buffer

        client.find("/");
        
        if(byte bytesReceived = client.readBytesUntil(' ', buffer, sizeof(buffer))){ 
          buffer[bytesReceived] = '\0';

          if(serialOut){
            Serial.print(F("URL: "));
            Serial.println(buffer);
          }
          
          if(strcmp(buffer, "favicon.ico\0")){
            char* paramsTmp = strtok(buffer, " ?=&/\r\n");
            int cnt = 0;
            
            while (paramsTmp) {
            
              switch (cnt) {
                case 0:
                  strcpy(pageName, paramsTmp);
                  if(serialOut){
                    Serial.print(F("Domain: "));
                    Serial.println(buffer);
                  }
                  break;
                case 1:
                  strcpy(paramName, paramsTmp);
                
                  if(serialOut){
                    Serial.print(F("Parameter: "));
                    Serial.print(paramName);
                  }
                  break;
                case 2:
                  strcpy(paramValue, paramsTmp);
                  if(serialOut){
                    Serial.print(F(" = "));
                    Serial.println(paramValue);
                  }
                  pruefeURLParameter(paramName, paramValue);
                  break;
              }
              
              paramsTmp = strtok(NULL, " ?=&/\r\n");
              cnt=cnt==0?1:cnt==1?2:1;
            }
          }
        }
      }// end if Client available
      break;
    }// end while Client connected
  } 

  return buffer;
}


