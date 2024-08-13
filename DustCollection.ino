#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <SPI.h>
#include <WiFiS3.h>      // For connecting Uno R4 to WiFi
#include <ArduinoOTA.h>  // For enabling over-the-air updates
#include "ArduinoGraphics.h"     // Needed to send to LCD
#include "Arduino_LED_Matrix.h"  // Needed to send text to LCD

#include "arduino_secrets.h"  //Wifi username and password

char ssid[] = SECRET_SSID;    // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password
int status = WL_IDLE_STATUS;  //Wifi status
WiFiServer server(80); // Initialize web server
// bool refreshRequired = false;

ArduinoLEDMatrix IPmatrix;  //Initialize LED matrix to display IP
ArduinoLEDMatrix matrix;    //Initialize LED matrix to display gate info

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();  //Initalize the PWM driver

// Dust collector values
#define dustCollectionRelayPin A1         // Output pin to activate DC relay and bin sensor relay
bool collectorIsOn = 0;                   // Flag whether the DC is on
const int DC_spindown = 30 * 1000;        // Time to run the DC after the last tool is turned off
bool collectorShutdownInitiated = false;  // Whether the timer is activated to shutdown the DC
long shutdown_millis = 0;                 // Timestamp the shutdown was initiated

// Tool (button) values
const int NUMBER_OF_TOOLS = 11; // Tool (button) count
const String tool_name[NUMBER_OF_TOOLS] = { "CNC", "Miter Saw", "Table Saw", "Band Saw", "Jointer", "Planer", "Sander", "Shopsmith", "Router Table", "Drum Sander", "Shutdown" };  // Enumerating the tools
const byte button_pin[NUMBER_OF_TOOLS] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 }; // Enumerating the pins the tool buttons will use
#define DEBOUNCE 10 // millis to prevent errant button presses
unsigned long button_millis[NUMBER_OF_TOOLS] = { 0 };  // Store the milliseconds since last press for each button for debouncing
unsigned long loop_millis;  // How many millis in the current loop, to compare against last press and debounce value
byte button_state[NUMBER_OF_TOOLS]; // Store last state for each button for debouncing
int activeTool = 50;  // a number that will never happen that indicates no tool is selected
bool cncActive = false;  // State for the CNC, as we allow it to run concurrently with other tools

// Gate values
const int number_of_gates = 12;  // Gate count
const int open_rate = 2; // Delay time between steps when opening
const int close_rate = 2; // Delay time between steps when closing
const int gateMinMax[number_of_gates][2] = {
  //Array for individual open / close pulse lengths for each gate
  /*open, close*/  //Approximate 90° swing is 210 us
  { 200, 410 },    // CNC
  { 300, 510 },    // Miter Saw
  { 203, 410 },    // Table Saw
  { 200, 410 },    // Band Saw
  { 200, 410 },    // Jointer
  { 200, 410 },    // Planer
  { 200, 410 },    // Sander
  { 200, 410 },    // Shopsmith
  { 200, 410 },    // Router table
  { 200, 410 },    // Vacuum
  { 200, 410 },    // Miter WYE
  { 200, 410 },    // Island WYE
};
byte gate_state[number_of_gates];    // Store last state for each gate
int gate_position[number_of_gates];  // Store last position for each gate
const byte gate_increment[7] = { 1, 5, 10, 25, 50, 100, 200}; // Used by the manual open / close buttons on the web server to control how much each press moves
int gate_increment_index = 3; // Which is the current increment to use from the above array

const int gates[NUMBER_OF_TOOLS][number_of_gates] = // Keep track of gates to be toggled ON/OFF for each tool
{
  { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // CNC
  { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 },  // Miter Saw
  { 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1 },  // Table Saw
  { 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1 },  // Band Saw
  { 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1 },  // Jointer
  { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 },  // Planer
  { 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 },  // WEN Sander
  { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },  // Shopsmith
  { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0 },  // Router table
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 },  // Vacuum
};

// LCD Display values
String tooltext = "";
unsigned long display_refresh = 0;               // Last time the display was refreshed
const int display_refresh_interval = 60 * 1000;  // Refresh the display every x seconds

void setup() 
{
  analogWrite(dustCollectionRelayPin, 255);    // Set the DC pin to high as soon as possible to avoid inadvertent relay trigger
  pinMode(dustCollectionRelayPin, OUTPUT);     // Set the DC to an output.
  digitalWrite(dustCollectionRelayPin, HIGH);  // Set the DC pin to high as soon as possible to avoid inadvertent relay trigger
  // PrintFileNameDateTime();
  pwm.begin();  // Setup PWM build
  pwm.setPWMFreq(50); // Typical servo frequency
  for (byte i = 0; i < NUMBER_OF_TOOLS; i++)  // Initialize tool button inputs
  {
    if ( i = 10 )
    {
      pinMode(button_pin[i], INPUT_PULLUP);  // We do not yet have external pullup resistor
    }
    else 
    {
      pinMode(button_pin[i], INPUT);  // We have external pullup resistors
    }
    button_state[i] = HIGH;  // Preset all states as high, or unpressed
  }
  for (byte i = 0; i < number_of_gates; i++)  // Initialize gates to assume open status, which is how we should normally leave them on power off
  {
    gate_state[i] = HIGH;  // Preset gates as open
    gate_position[i] = gateMinMax[i][1]; // Preset position as max open position
  }
  Serial.begin(115200);  // Initialize serial
  while (status != WL_CONNECTED) // attempt to connect to Wifi network:
  {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, pass);  // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
  }
  Serial.println("You are connected to the network");
  ArduinoOTA.begin(WiFi.localIP(), "Arduino", "password", InternalStorage); // start the WiFi OTA library with internal (flash) based storage
  printWifiStatus();  // Sends IP address to serial port and LED matrix
  server.begin(); // Initialize web server
  matrix.begin();  // Initialize matrix used to send gate to LED matrix
  OpenAllGates();  // First time, open all gates to make sure state is known
}

void incrementServoUp(uint8_t num) // Function used by + button on web server to move individual gates to find exact position
{
  for (uint16_t pulselen = gate_position[num]; pulselen <= (gate_position[num] + gate_increment[gate_increment_index]); pulselen++)  // Start 50 under the max value
  {
    pwm.setPWM(num, 0, pulselen);
    // gate_position[num] = pulselen;
    delay(open_rate);
  }
  gate_position[num] = (gate_position[num] + gate_increment[gate_increment_index]);
  // Serial.print("Incremented ");
  // Serial.print(num);
  // Serial.print(" to ");
  // Serial.println(gate_position[num]);
}

void incrementServoDown(uint8_t num) // Function used by - button on web server to move individual gates to find exact position
{
  for (uint16_t pulselen = gate_position[num]; pulselen >= (gate_position[num] - gate_increment[gate_increment_index]); pulselen--)
  {
    pwm.setPWM(num, 0, pulselen);
    // gate_position[num] = pulselen;
    delay(close_rate);
  }
  gate_position[num] = (gate_position[num] - gate_increment[gate_increment_index]);
  // Serial.print("Incremented ");
  // Serial.print(num);
  // Serial.print(" to ");
  // Serial.println(gate_position[num]);
}

void gateProcessing() // Function to open/close gates as appropriate for the active tool(s)
{
  // Serial.print("Opening gates: ");
  for (int s = 0; s < number_of_gates; s++)  // Loop through opens first to avoid an all-closed situation
  {
    if ((gates[activeTool][s] == 1 || (cncActive == true && gates[0][s] == 1)) && gate_state[s] != HIGH)  // Open both current tool gates and if the CNC is active any gates it needs
    {
      // Serial.print(s);
      // Serial.print(", ");
      openGate(s);
    }
  }
  // Serial.println(" --END");
  // Serial.print("Closing gates: ");
  for (int s = 0; s < number_of_gates; s++)  // Loop through closes
  {
    if (cncActive == true) {
      if (gates[activeTool][s] == 0 && gates[0][s] == 0 && gate_state[s] != LOW) // If the CNC is active, only close gates both it and the active tool do not need and that are not already closed
      {
        // Serial.print(s);
        // Serial.print(", ");
        closeGate(s);
      }
    } else if (gates[activeTool][s] == 0 && gate_state[s] != LOW) // If the CNC is NOT active, close any gates the current tool does not need and are not already closed
    {
      // Serial.print(s);
      // Serial.print(", ");
      closeGate(s);
    }
  }
  // Serial.println(" --END");
}

void OpenAllGates() {
  Serial.println("Opening all gates");
  for (int s = 0; s < number_of_gates; s++) {
    openGate(s);  // Loop to open each gate
  }
}

void turnOnDustCollection() {
  if (collectorIsOn != true)  //If it's off, then turn it on!
  {
    // Serial.println("turnOnDustCollection");
    digitalWrite(dustCollectionRelayPin, LOW);    // Relay pin activate
    collectorIsOn = true;                         // Set state for tracking
  } else if (collectorShutdownInitiated == true)  // If we're waiting to turn off the DC, interrupt and end the timer
  {
    collectorShutdownInitiated = false;
    // Serial.println("DC shutdown aborted");
  }
}

void turnOffDustCollection() {
  // Serial.println("DC shutdown timer initiated");
  collectorShutdownInitiated = true; // Flag to have loop check the timer
  shutdown_millis = millis(); // Set time for start of shutdown timer
  activeTool = 50; // Default off tool
}

void collectorShutdownTimer() // Called by loop to check for the shutdown timer being on
{
  if (collectorShutdownInitiated == true && loop_millis - shutdown_millis > DC_spindown)  // If the timer was initiated and we've exceeded the delay
  {
    // Serial.println("DC shutdown timer limit reached.  DC shutting down...");
    digitalWrite(dustCollectionRelayPin, HIGH);  // Turn off DC relay pin
    collectorIsOn = false; // Set flag that DC is off
    collectorShutdownInitiated = false; // End timer flag
  }
}

void closeGate(uint8_t num) // Called to close individual gates
{
  if (gate_position[num] <= gateMinMax[num][0]) // If current position is less than the minimum, set the current state to be just above the minimum
  {
    gate_position[num] = gateMinMax[num][0] + 20;
  }
  for (uint16_t pulselen = gate_position[num]; pulselen >= gateMinMax[num][0]; pulselen--) // Use a loop to close smoothly
  {
    pwm.setPWM(num, 0, pulselen);
    delay(close_rate);
  }
  gate_state[num] = LOW; // Set gate state so status is known
  gate_position[num] = gateMinMax[num][0]; // Set gate position to prevent unnecessary movement 
}

void openGate(uint8_t num) 
{
  if (gate_position[num] >= gateMinMax[num][1]) // If current position is greater than the maximum, set the current state to be just below the maximum
  {
    gate_position[num] = gateMinMax[num][1] - 20;
  }
  for (uint16_t pulselen = gate_position[num]; pulselen <= gateMinMax[num][1]; pulselen++)  // Start at current position
  {
    pwm.setPWM(num, 0, pulselen);
    delay(open_rate);
  }
  gate_state[num] = HIGH;  // Set gate state so status is known
  gate_position[num] = gateMinMax[num][1]; // Set gate position to prevent unnecessary movement 
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  // Serial.print("SSID: ");
  // Serial.println(WiFi.SSID());

  String ipstring = WiFi.localIP().toString();
  String ipstringtext = "IP: " + ipstring;  // Used for LCD printout
  // Serial.println(ipstringtext);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  // Serial.print("signal strength (RSSI):");
  // Serial.print(rssi);
  // Serial.println(" dBm");

  // Setup LCD matrix to print IP
  IPmatrix.begin();
  IPmatrix.beginDraw();
  IPmatrix.stroke(0xFFFFFFFF);
  IPmatrix.textScrollSpeed(60);
  IPmatrix.textFont(Font_5x7);
  IPmatrix.beginText(0, 1, 0xFFFFFF);
  IPmatrix.println(ipstringtext);
  IPmatrix.endText(SCROLL_LEFT);
  IPmatrix.endDraw();
  IPmatrix.end();
}

void displayToolStatus() {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textScrollSpeed(60);
  if (activeTool == 50) // No tool is avtive
  {
    if (collectorShutdownInitiated == true) // DC shutdown timer is happening
    {
      tooltext = "Shutting down"; // Display that we're shutting down
    } else 
    {
      tooltext = "Off"; // Otherwise just say we're off
    }
  } else 
  {
    tooltext = tool_name[activeTool];  // Get the name of the active tool
    if (cncActive == true && activeTool != 0) // If the CNC is active, add that to the text
    {
      tooltext = tooltext + " + CNC";
    }
  }
  // Serial.println(tooltext);
  matrix.textFont(Font_5x7);
  matrix.beginText(12, 1, 0xFFFFFF);
  matrix.println(tooltext);
  matrix.endText(SCROLL_LEFT);
  matrix.endDraw();
}

bool button_pressed(byte index) 
{
  //Debounce by seeing if the difference between now and the last button press is less than the debounce value
  if (loop_millis - button_millis[index] < DEBOUNCE) return (button_state[index] == LOW);

  //Check state
  byte state = digitalRead(button_pin[index]);  //read the pin corresponding to button index into state var
  if (state != button_state[index])             //did the value change from the last stored status
  {
    button_state[index] = state;         // flip the state to reflect current status
    button_millis[index] = loop_millis;  // update that button's last button press time
  }
  return (state == LOW);
}

void diplayWebServerV2() {
  // listen for incoming web clients
  WiFiClient client = server.available();
  if (client) {
    Serial.println("new client");
    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    String buffer = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        // Serial.print(c);
        buffer += c;
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          //HTML START
          client.println("<head>");
          client.println("<title>Dust Collector Controller</title>");
          client.println("<meta name=\"viewport\" content=\"width=device-width\"/>");
          String ipstring = WiFi.localIP().toString();
          client.print("<meta http-equiv=\"refresh\" content=\"30;url=http://");  // 30 seconds web page refresh
          client.print(ipstring); // 
          client.println("/\"/>");
          client.println("</head>");
          client.println("<script>");
          client.println("function gtu(path, params, method) {");
          client.println("    method = method || \"GET\";");
          client.println("    var form = document.createElement(\"form\");");
          client.println("    form.setAttribute(\"method\", method);");
          client.println("    form.setAttribute(\"action\", path);");
          client.println("    for(var key in params) {");
          client.println("        if(params.hasOwnProperty(key)) {");
          client.println("            var hF = document.createElement(\"input\");");
          client.println("            hF.setAttribute(\"type\", \"hidden\");");
          client.println("            hF.setAttribute(\"name\", key);");
          client.println("            hF.setAttribute(\"value\", params[key]);");
          client.println("            form.appendChild(hF);");
          client.println("         }");
          client.println("    }");
          client.println("    document.body.appendChild(form);");
          client.println("    form.submit();");
          client.println("}");
          client.println("</script>");
          client.println("<body>");
          client.println("<table width=\"500\" height=\"287\" border=\"1\">");
          client.println("  <tr>");
          client.println("    <th width=\"150\" scope=\"col\">Device</th>");
          client.println("    <th width=\"160\" scope=\"col\">Status Info</th>");
          client.println("    <th width=\"160\" scope=\"col\"><div align=\"center\">Actions</div></th>");
          client.println("  </tr>");
          client.println("  ");
          client.println("  <tr>");

          //Dust collector info
          client.println("    <td>Dust Collector</td>");  //DC name
          client.print("<td>Shutdown timer ");            // DC timer status
          if (collectorShutdownInitiated != true) {
            client.println("is not active</td>");
          } else {
            client.print("has ");
            client.print((DC_spindown - (loop_millis - shutdown_millis)) / 1000);
            client.println(" seconds remaining</td>");
          }

          if (collectorIsOn == true)  // DC power status
          {
            client.print("<td><div align=\"center\"><input name=\"b1\" button style=\"background-color:lightgreen; width: 140px\" type=\"button\" onClick=\"gtu('http://");
            client.print(ipstring); // 
            client.println("/\',{'DC':'OFF'})\" value=\"Power ON\" /></div></td>");
          } else {
            client.print("<td><div align=\"center\"><input name=\"b1\" button style=\"background-color:red; width: 140px\" type=\"button\" onClick=\"gtu('http://");
            client.print(ipstring); // 
            client.println("/\',{'DC':'ON'})\" value=\"Power OFF\" /></div></td>");
          }
          client.println("</tr>");
          client.println("<tr></tr>");

          //Tool info
          client.println("<tr>");
          client.println("<td>Active Tool</td>");
          if (activeTool == 50) {
            if (collectorShutdownInitiated == true) {
              tooltext = "Shutting down";
            } else {
              tooltext = "Off";
            }
          } else {
            tooltext = tool_name[activeTool];
            if (cncActive == true && activeTool != 0) {
              tooltext = tooltext + " + CNC";
            }
          }
          client.println("<td><div align=\"center\"><b>");
          client.println(tooltext);
          client.println("</b></div></td>");
          client.println("</tr>");

          client.println("<tr>");
          client.println("<td>");
          long lastpress_millis = 0;
          for (int tool = 0; tool < NUMBER_OF_TOOLS; tool++) {
            if (button_millis[tool] > lastpress_millis) {
              lastpress_millis = button_millis[tool];
            }
          }
          client.println("Time since last button press:");  // time since last press
          client.println("</td>");
          client.println("<td>");
          client.println((loop_millis - lastpress_millis) / 1000);
          client.println("</td>");
          client.println("</tr>");
          client.println("<tr></tr>");

          //Gate Info - loop through gates
          for (int s = 0; s < number_of_gates; s++)  // Loop through
          {
            client.println("<tr>");
            client.println("<td>");
            client.print("Gate ");
            client.print(s);
            client.println("</td>");
            client.println("<td><div align=\"center\">");
            client.print("Set to  ");
            client.println(gate_position[s]);
            client.println("</div></td>");
            client.println("<td><div align=\"center\">");
            client.print("<input name=\"gate");
            client.print(s);
            if (gate_state[s] == HIGH) 
            {
              client.print("\" type=\"button\" button style=\"background-color:lightgreen; height: 25px; width: 60px\" onClick=\"gtu('http://");
              client.print(ipstring); // 
              client.print("/\',{'GT");
              if (s < 10) 
              {
                client.print("0");
              }
              client.print(s);
              client.println("':'Close'})\" value=\"OPEN\" />");
            } 
            else 
            {
              client.print("\" type=\"button\" button style=\"background-color:red; height: 25px; width: 60px\" onClick=\"gtu('http://");
              client.print(ipstring); // 
              client.print("/\',{'GT");
              if (s < 10) 
              {
                client.print("0");
              }
              client.print(s);
              client.println("':'Open'})\" value=\"CLOSED\" />");
            }
            client.print("<input name=\"gate");
            client.print(s);
            client.print("up\" button style=\"margin-left: 15px; height: 25px; width: 30px\" type=\"button\" onClick=\"gtu('http://");
            client.print(ipstring); //   
            client.print("/\',{'GT");
            if (s < 10) 
            {
              client.print("0");
            }
            client.print(s);
            client.println("':'Up'})\" value=\"+\" />");
            client.print("<input name=\"gate");
            client.print(s);
            client.print("down\" type=\"button\" button style=\"height: 25px; width: 30px\" onClick=\"gtu('http://");
            client.print(ipstring); // 
            client.print("/\',{'GT");
            if (s < 10) 
            {
              client.print("0");
            }
            client.print(s);
            client.println("':'Down'})\" value=\"-\" />");
            client.println("</div></td>");
            client.println("</tr>");
          }
          client.println("<tr></tr>");
          client.println("<tr>");
          client.println("<td>");
          client.print("Set gate increment ");
          client.println("</td>");
          client.println("<td><div align=\"center\">");
          client.print("Set to  ");
          client.println(gate_increment[gate_increment_index]);
          client.println("</div></td>");
          client.println("<td><div align=\"center\">");
          
          client.print("<input name=\"incup\" button style=\"margin-left: 15px; height: 25px; width: 30px\" type=\"button\" onClick=\"gtu('http://");
          client.print(ipstring); //   
          client.print("/\',{'IC");
          client.println("':'Up'})\" value=\"+\" />");

          client.print("<input name=\"incdown\" button style=\"margin-left: 15px; height: 25px; width: 30px\" type=\"button\" onClick=\"gtu('http://");
          client.print(ipstring); //   
          client.print("/\',{'IC");
          client.println("':'Down'})\" value=\"-\" />");
          client.println("</div></td>");
          client.println("</tr>");
          client.println("  ");
          client.println("</table>");
          client.print("Last compiled ");
          client.print(__DATE__);
          client.print(" ");
          client.println(__TIME__);
          client.println("</body>");
          //HTML END
          break;
        }
        if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
          buffer = "";
        }

        else if (c == '\r') {
          // you've gotten a character on the current line

          // Serial.println(buffer);

          //DC
          if (buffer.indexOf("GET /?DC=ON") >= 0) {
            turnOnDustCollection();
            // refreshRequired = true;
          } else if (buffer.indexOf("GET /?DC=OFF") >= 0) {
            turnOffDustCollection();
            // refreshRequired = true;
          } 
          else if (buffer.indexOf("GET /?GT") >= 0) 
          {
            unsigned long gate = buffer.substring(8, 10).toInt();
            if (buffer.indexOf("=Close") >= 0) {
              closeGate(gate);
            }
            if (buffer.indexOf("=Open") >= 0) {
              openGate(gate);
            }
            if (buffer.indexOf("=Up") >= 0) {
              incrementServoUp(gate);
            }
            if (buffer.indexOf("=Down") >= 0) {
              incrementServoDown(gate);
            }
          }
          else if (buffer.indexOf("GET /?IC") >= 0) 
          {
            if (buffer.indexOf("=Down") >= 0)
            {
              if (gate_increment_index >= 1)
              {
                gate_increment_index--;
              }
            }
            else if (buffer.indexOf("=Up") >= 0)
            {
              if (gate_increment_index < 7)
              {
                gate_increment_index++;
              }
            }
          }
        } else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);

    //
    // if (refreshRequired == true) {
      // Serial.println("refreshRequired");
      // refreshRequired = false;
      //Added to prevent a manual refresh of the page changing a powerstate due to content in the address bar
      // client.println("<meta http-equiv=\"refresh\" content=\"5;url=http:// /\" >");
    // }

    // close the connection:
    client.stop();
    Serial.println("client disconnected");
  }
}

void loop() {
  loop_millis = millis();  // Get current loop timestamp
  ArduinoOTA.handle();       // Handles a code update request
  collectorShutdownTimer();  // Check to see if the timer is running to shutdown the DC
  diplayWebServerV2();
  for (byte i = 0; i < NUMBER_OF_TOOLS; i++)
    if (button_pressed(i))  // Loop to check for and process button presses
    {
      // Serial.println(tool_name[i] + " was pressed");
      if (i == 0)  // The CNC button was pressed, so update CNC status
      {
        cncActive = !cncActive;  // Flip CNC active each time its button is pressed
        activeTool = i;  // This is now our active tool
      }
      if (activeTool == i || i == 10)  // The current tool's button or the shutdown button is pressed
      {
        if (cncActive == false || i == 10 )  // If the current tool was the only one running or the shutdown button was pressed open gates and shut off DC
        {
          turnOffDustCollection();  // Will shut down after set delay
          OpenAllGates();           // Set gates to known state for power down
          activeTool = 50;          // Reset back to default tool #
          // Serial.println("CNC State is false and active tool turned off");
        } else  // CNC was also active, so only shutdown current gate and leave DC on
        {
          activeTool = 0;    // CNC is now left as the active tool
          gateProcessing();  // Run standard gate processing so only the CNC's gates are open
        }
      } else  // A button was pressed for a tool that is not the current tool, or this is the first tool button pressed
      {
        activeTool = i;    // This is now our active tool
        gateProcessing();  // Run gate processing to set gates to appropriate states
        turnOnDustCollection();
      }
      displayToolStatus();  // Run the LED matrix function to display the tool selected
    } else if (loop_millis - display_refresh > display_refresh_interval) // On an interval, display the current tool info on the LED matrix
    {
      display_refresh = loop_millis;
      displayToolStatus();
    }
}