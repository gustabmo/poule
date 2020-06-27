// poule.ino by guexel@gmail.com
//
// Arduino ESP8266 software to control a chicken coop door
// opens at dawn and closes at dusk
// connects via wifi
// web server with information and manual control of the door
//
// compile in Arduino IDE https://www.arduino.cc/en/Main/Software
// with http://arduino.esp8266.com/stable/package_esp8266com_index.json
//
// components used:
// Arduino board ESP8266 NodeMCU https://shopofthings.ch/shop/iot-module/esp8266-12e-nodemcu-entwicklungsboard-v3/
// motor controller MX1508 (not actually a L298N) https://shopofthings.ch/shop/aktoren/motoren/dual-motor-driver-l298n-motorentreiber/
// photo sensor connected to the ADC https://shopofthings.ch/shop/sensoren/umwelt/photosensor-modul-digital-analog-mit-lm393-schaltung-kompakt/
// reed sensor to identify door open/close https://shopofthings.ch/shop/bauteile/schalter/reedschalter-sensor-magnetischer-schalter-magnetron-modul/


// enum TdoorState { dsUnknown, dsOpen, dsClosed, dsOpening, dsClosing, dsError };
#include "definitions.h"


// I've seen examples of L298N code using a third pin ENA to enable the motor, that's not the case of this MX1508 
#define PinMotorUp 4
#define PinMotorDown 5

#define PinDoorUpSensor 12
#define PinDoorDownSensor 14

// must be analog input
#define PinLightSensor A0



// ------ tools

unsigned int TimeDiff ( unsigned int b, unsigned int a ) {
  if (b < a) {
    return (0xffffffff - a) + b + 1;
  } else {
    return b-a;
  }
}



// ------ event log

#define EventLogLength 300
struct TEventLog {
  unsigned int millis;
  String event;
};
TEventLog EventLog[EventLogLength];
int EventLogLast=0;


void Loga ( unsigned int now, String event ) {
  Serial.println ( event );

  EventLogLast++;
  if (EventLogLast >= EventLogLength) EventLogLast=0;

  EventLog[EventLogLast].millis = now;
  EventLog[EventLogLast].event = event;
}



// ------ door

TdoorState currentDoorState = dsUnknown;
unsigned long currentDoorStateSince;

#define timeoutDoorMovement 20000

String doorStateName ( TdoorState DS ) {
  switch (DS) {
    case dsUnknown : return "Unknown";
    case dsOpen : return "Open";
    case dsClosed : return "Closed";
    case dsOpening : return "Opening";
    case dsClosing : return "Closing";
    case dsError : return "ERROR";
    default : return "DS#"+String(DS);
  }
}

void setDoorState ( unsigned int now, TdoorState NewState, String caller ) {
  currentDoorState = NewState;
  currentDoorStateSince = now;
  Loga ( now, "door "+doorStateName(currentDoorState)+" by "+caller );
}

void moveDoor ( TdoorState IfThis, TdoorState ThanThat, int Pin, unsigned int now, String caller ) {
  if ((currentDoorState==IfThis) || (currentDoorState==dsUnknown)) {
    setDoorState ( now, ThanThat, caller );
    analogWrite ( Pin, 100 );
  }
}

void stopDoor ( unsigned int now, TdoorState NewState, String caller ) {    
  analogWrite ( PinMotorUp, 0 );
  analogWrite ( PinMotorDown, 0 );
  setDoorState ( now, NewState, caller ); 
}

void closeDoor ( unsigned int now, String caller ) {
  moveDoor ( /*IfThis*/dsOpen, /*ThanThat*/dsClosing, PinMotorUp, now, caller );
}

void openDoor ( unsigned int now, String caller ) {
  moveDoor ( /*IfThis*/dsClosed, /*ThanThat*/dsOpening, PinMotorDown, now, caller );
}

void loopDoorMovement ( unsigned int now ) {
  int CheckPin;
  TdoorState NewDS;

  // what we do depends on the state we're in
  switch (currentDoorState) {
    case dsClosing : {
      CheckPin = PinDoorDownSensor;
      NewDS = dsClosed;
    }
    case dsOpening : {
      CheckPin = PinDoorUpSensor;
      NewDS = dsOpen;
    }
    default : CheckPin = -1;
  }

  // if there's a pin to check
  if (CheckPin != -1) {
    if (digitalRead(CheckPin) == HIGH) {
      stopDoor ( now, NewDS, "loopDoorMovement-pin" );
    } else if (TimeDiff ( now, currentDoorStateSince ) > timeoutDoorMovement) {
      stopDoor ( now, dsError, "loopDoorMovement-timeout" ); 
    }
  }
} // loopDoorMovement()



// ------ light measure

struct TLightMeasure {
  unsigned long millis;
  int light;
};

#define lastLMlength 4
TLightMeasure lastLM[lastLMlength];

#define changesLMlength 100
TLightMeasure changesLM[changesLMlength];
#define changesLMthreshold 50

#define LMinterval 60000

#define LMDoorOpen 300
#define LMDoorClose 200


void loopLightMeasure ( unsigned int now ) {
  if (TimeDiff ( now, lastLM[0].millis ) > LMinterval) {
    memmove ( &lastLM[1], &lastLM[0], (lastLMlength-1)*sizeof(lastLM[0]) );
    lastLM[0].millis = now;
    lastLM[0].light = analogRead ( PinLightSensor );

    // calculates average lighting
    int lightAvg = 0;
    int lightAvgValid = 1;
    for ( int i = 0; i++; i < lastLMlength ) {
      lightAvg += lastLM[i].light;
      lightAvgValid = lightAvgValid && (lastLM[i].millis != 0);
    }
    lightAvg /= lastLMlength;

    if (lightAvgValid) {
      // opens and closes door depending on lighting
      if (lightAvg < LMDoorClose) {
        closeDoor ( now, "loopLightMeasure:"+String(lightAvg) );
      } else if (lightAvg > LMDoorOpen) {
        openDoor ( now, "loopLightMeasure:"+String(lightAvg) );
      }

      // keep a record of changes in lighting
      if (abs(lightAvg-changesLM[0].light) > changesLMthreshold) {
        memmove ( &changesLM[1], &changesLM[0], (changesLMlength-1)*sizeof(changesLM[0]) );
        changesLM[0].millis = now;
        changesLM[0].light = lightAvg;
      }
    } // if lightAvgValid
  } // if LMinterval
} // loopLightMeasure()





// ------ web server


// Wi-Fi library
#include <ESP8266WiFi.h>


// private.h : network credentials, this file is NOT INCLUDED in the git repository
// you should create it yourself with these two lines:
// const char* ssid     = "WifiSSID";
// const char* password = "WifiPassword";
#include "private.h"

#define timeoutWiFiReconnect 300000
#define timeoutWebServerRequest 2000

WiFiServer server(80);
WiFiClient client;
int ClientInUse = 0; // there might be a better way to signal the client is free after a stop(), but I couldn't find it

String ReqHeader = "";
char ReqLastChar;

unsigned int lastWiFiActivity;
int lastWiFiStatus;


void WebServerProcessRequest ( unsigned int now ) {
  int i;

  if (ReqHeader.indexOf("GET /door/open") >= 0) {
    openDoor ( now, "Web Server request" );
  } else if (ReqHeader.indexOf("GET /door/close") >= 0) {
    closeDoor ( now, "Web Server request" );
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();

  client.println("<!DOCTYPE html><html>");
  client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  client.println("<link rel=\"icon\" href=\"data:,\">");
  client.println("<style>");
  client.println("html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: left;}");
  client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
  client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
  client.println(".button2 {background-color: #77878A;}");
  client.println("</style></head>");

  client.println("<body><h1>ESP8266 poule</h1>");

  client.println("<table>");
  i = EventLogLast;
  do {
    client.println ( "<tr><td>T-"+String(round(TimeDiff(now,EventLog[i].millis)/1000))+"s</td>" );
    client.println ( "<td>"+EventLog[i].event+"</td></tr>" );
    i--;
    if (i<0) i=EventLogLength-1;
  } while ((i != EventLogLast) && (EventLog[i].millis != 0));
  client.println("</table>");
  client.println("</body></html>");

  // The HTTP response ends with another blank line
  client.println();
} // WebServerProcessRequest()


void loopWebServerConnected ( unsigned int now ) {
  int ReqHeaderComplete = 0;
  char c;

  if (!client || !ClientInUse) {
    client = server.available();   // Listen for incoming clients
    if (client) {
      ClientInUse=1;
      Loga ( now, "@@ server.available -> ClientInUse=1" );
      lastWiFiActivity = now;
    }
  }

  if (client && ClientInUse) {
    while (!ReqHeaderComplete && client.connected() && client.available()) {
      c = client.read();
      if (c != '\r') { // ignores CR, considers only LF for line break
        ReqHeader += c;
        if ((c == '\n') && (ReqLastChar == '\n')) {
          // empty line = end of the client HTTP request
          ReqHeaderComplete = 1;
        }
        ReqLastChar = c;
      }
    }

    int DestroyClient = 1;

    if (ReqHeaderComplete) {
      Loga ( now, "WebServer request" );
      WebServerProcessRequest ( now );
    } else if (!client.connected()) {
      Loga ( now, "WebServer connection aborted" );
    } else if (TimeDiff ( now, lastWiFiActivity ) > timeoutWebServerRequest ) {
      Loga ( now, "WebServer connection timeout" );
    } else {
      DestroyClient=0;
    }

    if (DestroyClient) {
      client.stop();
      ClientInUse=0;
      Loga ( now, "@@ client.stop + ClientInUse=0" );
      ReqHeader="";
    }
  }
} // loopWebServerConnected()


void loopWebServer ( unsigned int now ) {
  int WiFiStatus = WiFi.status();

  if (lastWiFiStatus != WiFiStatus) {
    lastWiFiStatus = WiFiStatus;
    // informs new status only if it's not "connecting"=WL_IDLE_STATUS
    if (WiFiStatus != WL_IDLE_STATUS) {
      Loga ( now, "WiFi.status : "+String(WiFiStatus) );
    }
    if (WiFiStatus == WL_CONNECTED) {
      server.begin();
    }
  }

  if (WiFiStatus == WL_CONNECTED) { 

    loopWebServerConnected ( now );

  } else {

    if (TimeDiff ( now, lastWiFiActivity ) > timeoutWiFiReconnect) {
      Loga ( now, "WiFi.begin" );
      WiFi.disconnect();
      WiFi.begin ( ssid, password );
      lastWiFiActivity = now;
      Serial.println ( "- *A status "+String(WiFiStatus)+" -> "+String(WiFi.status()) );
    }
  }
} // loopWebServer()




// ------ main program

// the setup function runs once when you press reset or power the board

void setup() {
  Serial.begin ( 9600 );
  delay ( 1000 );

  pinMode ( PinMotorUp, OUTPUT );
  pinMode ( PinMotorDown, OUTPUT );
  pinMode ( PinDoorUpSensor, INPUT );
  pinMode ( PinDoorDownSensor, INPUT );
  pinMode ( PinLightSensor, INPUT );

  unsigned int now = millis();
  Loga ( now, "program starting v1" );

  analogWrite ( PinMotorUp, 0 );
  analogWrite ( PinMotorDown, 0 );
  
  TdoorState DS;
  if (digitalRead(PinDoorUpSensor) == HIGH) {
    DS = dsOpen;
  } else if (digitalRead(PinDoorDownSensor) == HIGH) {
    DS = dsClosed;
  } else {
    DS = dsUnknown;
  }
  setDoorState ( now, DS, "program setup" );
}


// the loop function runs over and over again forever

void loop() {
  unsigned int now = millis(); // use a single "now" moment for all routines

  // each procedure gets a chance to shorten the pause delay
  loopLightMeasure ( now );
  loopDoorMovement ( now );
  loopWebServer ( now );
  
  delay ( 200 );
}
