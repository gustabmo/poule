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
// Arduino board ESP8266 NodeMCU - https://shopofthings.ch/shop/iot-module/esp8266-12e-nodemcu-entwicklungsboard-v3/
// motor controller MX1508 (not actually a L298N) - https://shopofthings.ch/shop/aktoren/motoren/dual-motor-driver-l298n-motorentreiber/
// photo sensor connected to the ADC - https://shopofthings.ch/shop/sensoren/umwelt/photosensor-modul-digital-analog-mit-lm393-schaltung-kompakt/
// reed sensor (x2) to identify door open/close - https://shopofthings.ch/shop/bauteile/schalter/reedschalter-sensor-magnetischer-schalter-magnetron-modul/


// this definition must be in a .h to be used as index in an array
// enum TdoorState { dsUnknown, dsOpen, dsClosed, dsOpening, dsClosing, dsError };
#include "definitions.h"


// I've seen examples of L298N code using a third pin ENA to enable the motor
// that's not the case of this MX1508
#define PinMotorDown 4
#define PinMotorUp 5

#define MotorPowerUp 700
#define MotorPowerDown 400

#define PinDoorUpSensor 12
#define PinDoorDownSensor 14
#define DoorHere LOW

// must be an analog input pin
#define PinLightSensor A0



// ------ tools

String CopyUpTo ( String s, char c ) {
  int p = s.indexOf ( c );
  if (p < 0) {
    return s;
  } else {
    return s.substring ( 0, p );
  }
}


// ------ system clock

unsigned int LastMillis = 0;
int MillisOverflow = 0;

void CheckClockOverflow ( unsigned int now ) {
  if (now < LastMillis) {
    MillisOverflow++;
  }
  LastMillis = now;
}

String TimeTicksToStr ( unsigned int t ) {
  double x;
  char unit;

  x = (double)t / 1000;
  unit='s';
  if (x >= 60) {
    x=x/60;
    unit='m';
    if (x >= 60) {
      x=x/60;
      unit='h';
      if (x >= 24) {
        x=x/24;
        unit='d';
      }
    }
  }
  
  return String(x,1) + unit;
}

String DeltaTimeToStr ( unsigned int t ) {
  return "T-" + TimeTicksToStr(t);
}



// ------ event log

#define EventLogLength 300
struct TEventLog {
  unsigned int millis;
  String event;
};
TEventLog EventLog[EventLogLength];
int EventLogLast = 0;


void Loga ( unsigned int now, String event ) {
  Serial.println ( event );

  EventLogLast++;
  if (EventLogLast >= EventLogLength) EventLogLast = 0;

  EventLog[EventLogLast].millis = now;
  EventLog[EventLogLast].event = event;
}



// ------ door

TdoorState CurrentDoorState = dsUnknown;
unsigned long CurrentDoorStateSince = 0;

#define TimeoutDoorMovement 20000

String DoorPinStateName ( int pin ) {
  if (digitalRead(pin) == DoorHere) {
    return "Here!";
  } else {
    return "--";
  }
}

String DoorStateName ( TdoorState DS ) {
  switch (DS) {
    case dsUnknown : return "Unknown";
    case dsOpen : return "Open";
    case dsClosed : return "Closed";
    case dsOpening : return "Opening";
    case dsClosing : return "Closing";
    case dsError : return "ERROR";
    default : return "DS#" + String(DS);
  }
}

void SetDoorState ( unsigned int now, TdoorState NewState, String caller ) {
  CurrentDoorState = NewState;
  CurrentDoorStateSince = now;
  Loga ( now, "door now " + DoorStateName(CurrentDoorState) + " by " + caller );
}

void ReadDoorState ( unsigned int now, String caller ) {
  TdoorState DS;
  if (digitalRead(PinDoorUpSensor) == DoorHere) {
    DS = dsOpen;
  } else if (digitalRead(PinDoorDownSensor) == DoorHere) {
    DS = dsClosed;
  } else {
    DS = dsUnknown;
  }
  SetDoorState ( now, DS, "sensors at "+caller );
}

void MoveDoor ( TdoorState IfThis, TdoorState ThanThat, int Pin, int MotorPower, unsigned int now, String caller ) {
  if ((CurrentDoorState == IfThis) || (CurrentDoorState == dsUnknown)) {
    SetDoorState ( now, ThanThat, caller );
    analogWrite ( Pin, MotorPower );
  }
}

void StopDoor ( unsigned int now, TdoorState NewState, String caller ) {
  analogWrite ( PinMotorUp, 0 );
  analogWrite ( PinMotorDown, 0 );
  SetDoorState ( now, NewState, caller );
}

void CloseDoor ( unsigned int now, String caller ) {
  MoveDoor ( /*IfThis*/dsOpen, /*ThanThat*/dsClosing, PinMotorDown, MotorPowerDown, now, caller );
}

void OpenDoor ( unsigned int now, String caller ) {
  MoveDoor ( /*IfThis*/dsClosed, /*ThanThat*/dsOpening, PinMotorUp, MotorPowerUp, now, caller );
}

void LoopDoorMovement ( unsigned int now ) {
  int CheckPin;
  TdoorState NewDS;

  // what we do depends on the state we're in
  switch (CurrentDoorState) {
    case dsClosing :
      CheckPin = PinDoorDownSensor;
      NewDS = dsClosed;
      break;
    case dsOpening : 
      CheckPin = PinDoorUpSensor;
      NewDS = dsOpen;
      break;
    default :
      CheckPin = -1;
  }
  
  // if there's a pin to check
  if (CheckPin != -1) {
    if (digitalRead(CheckPin) == DoorHere) {
      StopDoor ( now, NewDS, "check pin "+String(CheckPin) );
    } else if (now-CurrentDoorStateSince > TimeoutDoorMovement) {
      StopDoor ( now, dsError, "timeout" );
    }
  }
} // LoopDoorMovement()



// ------ light measure

struct TLightMeasure {
  unsigned long millis;
  int light;
};

#define LastLMLength 5
TLightMeasure LastLM[LastLMLength];
int LastLMAvg;
int LastLMMin;
int LastLMMax;

#define LMChangesLength 100
TLightMeasure LMChanges[LMChangesLength];
#define LMChangesThreshold 50

unsigned int PauseDoorMovementByLightSince = 0;
#define PauseDoorMovementByLightLength 120000

#define LMInterval 15000

#define LMDoorOpen 600
#define LMDoorClose 700


void LoopLightMeasure ( unsigned int now ) {
  if (now-LastLM[0].millis > LMInterval) {
    memmove ( &LastLM[1], &LastLM[0], (LastLMLength - 1)*sizeof(LastLM[0]) );
    LastLM[0].millis = now;
    LastLM[0].light = analogRead ( PinLightSensor );

    // calculates average lighting
    LastLMAvg = 0;
    LastLMMin = 9999999;
    LastLMMax = -9999999;
    int LastLMAvgValid = 1;
    for ( int i = 0; i < LastLMLength; i++ ) {
      LastLMAvg += LastLM[i].light;
      if (LastLM[i].light > LastLMMax) LastLMMax = LastLM[i].light;
      if (LastLM[i].light < LastLMMin) LastLMMin = LastLM[i].light;
      LastLMAvgValid = LastLMAvgValid && (LastLM[i].millis != 0);
    }
    LastLMAvg /= LastLMLength;

    if (LastLMAvgValid) {
      // opens and closes door depending on lighting
      if (
        PauseDoorMovementByLightSince != 0 
        && 
        (now-PauseDoorMovementByLightSince) < PauseDoorMovementByLightLength
      ) {
        // don't open/close door based on the lighting: pause
      } else {
        PauseDoorMovementByLightSince = 0;
        if (LastLMMin > LMDoorClose) {
          CloseDoor ( now, "min light " + String(LastLMMin) );
        } else if (LastLMMax < LMDoorOpen) {
          OpenDoor ( now, "max light " + String(LastLMMax) );
        }
      }

      // keep a record of changes in lighting
      if (
        (LMChanges[0].millis == 0)
        ||
        (abs(LastLMAvg - LMChanges[0].light) > LMChangesThreshold) 
      ) {
        memmove ( &LMChanges[1], &LMChanges[0], (LMChangesLength - 1)*sizeof(LMChanges[0]) );
        LMChanges[0].millis = now;
        LMChanges[0].light = LastLMAvg;
      }
    } // if LastLMAvgValid
  } // if LMInterval
} // LoopLightMeasure()





// ------ web server


// Wi-Fi library
#include <ESP8266WiFi.h>


// private.h : network credentials, this file is NOT INCLUDED in the git repository
// you should create it yourself with these two lines:
// const char* ssid     = "WifiSSID";
// const char* password = "WifiPassword";
#include "private.h"

#define TimeoutWiFiReconnect 300000
#define TimeoutWebServerRequest 2000

WiFiServer server(80);
WiFiClient client;
int ClientInUse = 0; // there might be a better way to signal the client is free after a stop(), but I couldn't find it

String ReqHeader = "";
char ReqLastChar;

unsigned int LastWiFiActivity;
int LastWiFiStatus;


String WiFiStatusName ( int status ) {
  switch(status) {
    case WL_CONNECTED : return "Connected";
    case WL_DISCONNECTED : return "Disconnected";
    default : return "#" + String(status);
  }
}


String Button ( String text, String href ) {
  return "<p><a href=\""+href+"\"><button class=\"bluebutton\">"+text+"</button></a></p>";
}

void WebServerWriteStatusPage ( unsigned int now ) {
  int i;
  
  client.println ( "<h3>Status</h3>" );
  client.println ( "<table class=\"bluetable\">" );
  { // table
    client.println ( "<tr><td>Running time</td><td>" + TimeTicksToStr(now) + "</td></tr>" );
    client.println ( "<tr><td>Clock Overflow (+-49d)</td><td>" + String(MillisOverflow) + "</td></tr>" );
    client.println ( "<tr><td>Door</td><td>" + DoorStateName(CurrentDoorState) + "</td></tr>" );
    client.println ( "<tr><td>Since</td><td>" + DeltaTimeToStr(now-CurrentDoorStateSince) + "</td></tr>" );
    client.println ( "<tr><td>Pause</td><td>" );
    { // td
      if (PauseDoorMovementByLightSince == 0) {
        client.println ( "no" );
      } else {
        client.println ( TimeTicksToStr ( PauseDoorMovementByLightLength - (now-PauseDoorMovementByLightSince) ) );
      }
    }
    client.println ( "</td></tr>" );
    client.println ( "<tr><td>Door Up</td><td>" + DoorPinStateName( PinDoorUpSensor ) + "</td></tr>" );
    client.println ( "<tr><td>Door Down</td><td>" + DoorPinStateName( PinDoorDownSensor ) + "</td></tr>" );
  }
  client.println ( "</table>" );

  switch (CurrentDoorState) {
    case dsClosed : 
      client.println ( Button("Open Door","/door/open") );
      break;
    case dsOpen : 
      client.println ( Button("Close Door","/door/close") );
      break;
    case dsError : 
      client.println ( Button("Reset Door","/door/reset") );
      break;
    case dsUnknown : 
      client.println ( Button("Open Door","/door/open") );
      client.println ( Button("Close Door","/door/close") );
      break;
  }

  client.println ( "<h3>Light</h3>" );

  client.println ( "<table><tr><td class=\"bigtd\">" );

  { // td
    client.println ( "<table class=\"bluetable\">" );
    { // table
      client.println ( "<tr><td>Open</td><td>&lt;" + String(LMDoorOpen) + "</td></tr>" );
      client.println ( "<tr><td>Close</td><td>&gt;" + String(LMDoorClose) + "</td></tr>" );
      client.println ( "<tr><td>Min</td><td>" + String(LastLMMin) + "</td></tr>" );
      client.println ( "<tr><td>Max</td><td>" + String(LastLMMax) + "</td></tr>" );
      client.println ( "<tr><td>Average</td><td>" + String(LastLMAvg) + "</td></tr>" );
    }
    client.println ( "</table>" );
  }

  client.println ( "</td><td class=\"bigtd\">" );

  { // td
    client.println ( "<table class=\"bluetable\">" );
    for ( i = 0; i < LastLMLength; ++i ) {
      client.println ( 
        "<tr><td>" + DeltaTimeToStr(now-LastLM[i].millis) + "</td>"
        +"<td>" + String(LastLM[i].light) + "</td></tr>"
      );
    }
    client.println ( "</table>" );
  }

  client.println ( "</td></tr></table>" );

  client.println ( "<h3>Light Changes</h3>" );
  client.println ( "<table class=\"bluetable\">" );
  for ( i = 0; i < LMChangesLength && LMChanges[i].millis != 0; ++i ) {
    client.println ( 
      "<tr><td>" + DeltaTimeToStr(now-LMChanges[i].millis) + "</td>"
      +"<td>" + String(LMChanges[i].light) + "</td></tr>"
    );
  }
  client.println ( "</table>" );

  client.println ( "<h3>Event Log</h3>" );
  client.println ( "<table class=\"bluetable\">" );
  i = EventLogLast;
  do {
    client.println ( "<tr><td>" + DeltaTimeToStr(now-EventLog[i].millis) + "</td>" );
    client.println ( "<td>" + EventLog[i].event + "</td></tr>" );
    i--;
    if (i < 0) i = EventLogLength - 1;
  } while ((i != EventLogLast) && (EventLog[i].millis != 0));
  client.println ( "</table>" );
} // WebServerWriteStatusPage


void WebServerProcessRequest ( unsigned int now ) {
  bool BackButton = 1;
  String operation = "";

  if (ReqHeader.indexOf("GET /door/open") >= 0) {
    OpenDoor ( now, "Web " + CopyUpTo ( ReqHeader, '\n' ) );
    operation = "Open Door !";
    PauseDoorMovementByLightSince = now;
  } else if (ReqHeader.indexOf("GET /door/close") >= 0) {
    CloseDoor ( now, "Web " + CopyUpTo ( ReqHeader, '\n' ) );
    operation = "Close Door !";
    PauseDoorMovementByLightSince = now;
  } else if (ReqHeader.indexOf("GET /door/reset") >= 0) {
    ReadDoorState ( now, "Web " + CopyUpTo ( ReqHeader, '\n' ) );
    operation = "Reset Door !";
  } else {
    BackButton = 0;
  }

  client.println ( "HTTP/1.1 200 OK" );
  client.println ( "Content-type:text/html" );
  client.println ( "Connection: close" );
  client.println();

  client.println ( "<!DOCTYPE html><html>" );
  client.println ( "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">" );
  client.println ( "<link rel=\"icon\" href=\"data:,\">" );
  client.println ( "<style>" );
  client.println ( "html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: left;}" );
  client.println ( ".bluetable { padding: 10px 15px; border-radius:10px; background-color:lightblue; }" );
  client.println ( "td { padding: 2px 8px; background-color:aliceblue; }" );
  client.println ( ".bigtd { padding: 0px 6px; background-color:white; vertical-align:baseline}" );
  client.println ( ".bluebutton { padding: 20px 45px; border-radius:12px; background-color:lightskyblue; font-weight:bold; cursor:pointer;}" );
  client.println ( "</style></head>" );

  client.println ( "<body><h1>Poule</h1>" );
  client.println ( "<p>Automatic control of your chicken coop door" );

  if (BackButton) {
    client.println ( "<p><large>" + operation + "</large></p>" );
    client.println ( Button ( "Status Page", "/" ) );
  } else {
    WebServerWriteStatusPage ( now );
  }

  client.println ( "<p>by <a href=\"mailto:guexel@gmail.com\">guexel@gmail.com</a>" );
  client.println ( "<br>available at <a href=\"https://github.com/gustabmo/poule\">github.com/gustabmo/poule</a>" );
  
  client.println ( "</body></html>" );

  // The HTTP response ends with another blank line
  client.println();
} // WebServerProcessRequest()


void LoopWebServerConnected ( unsigned int now ) {
  int ReqHeaderComplete = 0;
  char c;

  if (!client || !ClientInUse) {
    client = server.available();   // Listen for incoming clients
    if (client) {
      ClientInUse = 1;
      LastWiFiActivity = now;
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
      WebServerProcessRequest ( now );
    } else if (!client.connected()) {
      // do nothing, but client will be stopped
    } else if (now-LastWiFiActivity > TimeoutWebServerRequest ) {
      // do nothing, but client will be stopped
    } else {
      DestroyClient = 0;
    }

    if (DestroyClient) {
      client.stop();
      ClientInUse = 0;
      ReqHeader = "";
    }
  }
} // LoopWebServerConnected()


void LoopWebServer ( unsigned int now ) {
  int WiFiStatus = WiFi.status();

  if (LastWiFiStatus != WiFiStatus) {
    LastWiFiStatus = WiFiStatus;
    // informs new status only if it's not "connecting"=WL_IDLE_STATUS
    if (WiFiStatus != WL_IDLE_STATUS) {
      Loga ( now, "WiFi.status : " + WiFiStatusName(WiFiStatus) );
    }
    if (WiFiStatus == WL_CONNECTED) {
      server.begin();
    }
  }

  if (WiFiStatus == WL_CONNECTED) {

    LoopWebServerConnected ( now );

  } else {

    if (now-LastWiFiActivity > TimeoutWiFiReconnect) {
      Loga ( now, "WiFi.begin" );
      WiFi.disconnect();
      WiFi.begin ( ssid, password );
      LastWiFiActivity = now;
    }
  }
} // LoopWebServer()




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
  Loga ( now, "program starting" );

  analogWrite ( PinMotorUp, 0 );
  analogWrite ( PinMotorDown, 0 );

  ReadDoorState ( now, "setup" );
}


// the loop function runs over and over again forever

void loop() {
  unsigned int now = millis(); // use a single "now" moment for all routines

  CheckClockOverflow ( now );
  LoopLightMeasure ( now );
  LoopDoorMovement ( now );
  LoopWebServer ( now );

  delay ( 200 );
}
