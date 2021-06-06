/* 
* Copyright (C) 2021 Hossein Ghorbanfekr [hgh.comphys@gmail.com]
*
*  This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*  ----------------------------------------------------------------------
*  
* Based on code and description presented in following references:
*  https://lastminuteengineers.com/sim800l-gsm-module-arduino-tutorial/
*  https://www.developershome.com/sms/cmglCommand3.asp
*  
*/

// OLED display
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// GSM module
#include <SoftwareSerial.h>

// **********************************************************************************

// General parameters
#define GSM_TX_PIN     17       // pin
#define GSM_RX_PIN     16       // pin
#define BUZZER_PIN     15       // pin
#define STATUS_LED     4        // pin
#define ALARM_LED      5        // pin
#define PUSH_BUTTON    2        // pin
#define BUZZER_FRQ     2000     // Hz    
#define ALARM_TIMER    1800000  // ms
#define ARM_BTN_DLY    8000     // ms
#define DISARM_BTN_DLY 4000     // ms
#define BUZZER_CHANNEL 0        // PWM channel
#define BUFFER_LEN     128      // size

// OLD display
#define SCREEN_WIDTH   128 // OLED display width, in pixels
#define SCREEN_HEIGHT  64  // OLED display height, in pixels
#define OLED_RESET     -1  // reset pin # (or -1 if sharing Arduino reset pin)

// SMS commands 
#define ALARM_SMS      "ALARM"
#define ARM_SMS        "ON"
#define DISARM_SMS     "OFF"

// GSM contact recipent
#define RECIPIENT      "+123456789"

// **********************************************************************************

// Create software serial object to communicate with modem (SIM800L)
SoftwareSerial modem(GSM_TX_PIN, GSM_RX_PIN);

// Global variables
char mbuf[BUFFER_LEN];   // a buffer for communicating with model
bool cmd_flag;           // a flag that indicates a command is sent to th modem and waiting for a response
unsigned long lastTime;  // tmp variable to keep track of time
int16_t skip_ncalls;     // tmp variable to be used for avoid calling a function too frequently

// Create display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// -----------------------------------------------------------------------------------

// Define a utility function to display text on display
void disp(const char *msg, const char *header = "", const bool keep_disp = true)
{
  Serial.print(header);
  Serial.println(msg); 

  if( keep_disp ) {
    display.println();
  }
  else {
    display.clearDisplay();
    display.setCursor(0, 0);
  }
  display.print(header);
  display.println(msg);
  display.display();
}

// Logo effect
void drawLogo(void) 
{
  // Draw an log effect
  display.clearDisplay();
  for(int16_t i=0; i<display.height()/2-2; i+=2) {
    display.drawRoundRect(i, i, display.width()-2*i, display.height()-2*i,
      display.height()/4, WHITE);
    display.display();
    delay(10);
  }
  delay(500);

  // Draw log
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(2);
  display.print(F("Remote\n Alarm\n  System"));
  display.display();
  delay(2000);
  display.startscrolldiagright(0x00, 0x07);
  delay(2000);
  display.startscrolldiagleft(0x00, 0x07);
  delay(2000);
  display.setTextSize(1);
  display.stopscroll();
  disp("\n\nConnecting...", "");
  checkLEDs();
  delay(6000);
  beep();
  display.clearDisplay(); 
}

// LED rolling effect to check all LEDs are functional
void checkLEDs() 
{
  const int wait = 100;
  for (int i=0; i<5; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(wait);
    digitalWrite(STATUS_LED, LOW);
    digitalWrite(ALARM_LED, HIGH);
    delay(wait);
    digitalWrite(ALARM_LED, LOW);
  }
}

// -----------------------------------------------------------------------------------

// Send a command to modem and read the modem reply to the global buffer
void sendCmd(const char *cmd)
{
  disp(cmd, "SND: ", false);
  
  modem.println(cmd);
  cmd_flag = true;
  delay(100);
  
  readModemToBuf();
  cmd_flag = false;
  delay(400);
}

// Create PWM tone on the buzzer pin
void tone(unsigned long duration)
{
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  ledcWriteTone(BUZZER_CHANNEL, BUZZER_FRQ);
  delay(duration);
  ledcDetachPin(BUZZER_PIN);
  ledcWriteTone(BUZZER_CHANNEL, 0);
}
void beep() { tone(100); }
void beepTwice() { beep(); delay(100); beep(); }

// If an error is detected
void raise_error(const char *msg)
{
  disp(msg, "ERR: ");
  tone(1000);
  cmd_flag = false;
  digitalWrite(STATUS_LED, HIGH);
  // Infinite loop while generating beep sound
  while(true) {
    delay(10000);
    tone(100);
  }
}

// Tasks to do when alram mode is detected: LED blinking, Buzzer beeping
void activate_alarm()
{
  bool alarm_deactivated = false;
  disp("Alarm activated!", "");
  lastTime = millis();
  while( (millis() - lastTime) < ALARM_TIMER ) {
    digitalWrite(ALARM_LED, HIGH);
    tone(100);
    digitalWrite(ALARM_LED, LOW);
    delay(50);
    // Deactivate if push button is pressed
    if( digitalRead(PUSH_BUTTON) == HIGH ) {
      alarm_deactivated = true;
      disp("Alarm deactivated", "");
      tone(500);
      delay(2000);
      break; 
    }
  }
  if( !alarm_deactivated ) {
    digitalWrite(ALARM_LED, HIGH);
  }
}

// Check wheather input message is Alarm message or not
bool isAlarm(char *string)
{
  string = strrchr(string, ALARM_SMS[0]);
  if( string != NULL )
    return ( strncmp(string, ALARM_SMS, 6) == 0 );
  return false ;
}

// Check modem's reply is a SMS message
void readSMS(char *buf)
{    
  if( strncmp(buf+1, "+CMT:", 5) != 0 )
   return;
   
  char *token = strtok(buf, "\"");
  char *token_prev;
  while (token != NULL) {
    //Serial.print("+CMT TOKEN: ");
    //Serial.println(token);
    token_prev = token;
    token = strtok(NULL, "\"");
  }
  char *sms = token_prev + 1;
 
  // React to the received SMS command 
  if( isAlarm(sms) ) {
    delay(200);
    disp(sms, "SMS: ", false);
    activate_alarm();
  }
//  else {
//    disp(sms, "SMS: ", false);
//    delay(5000);
//  }
}

// Read buffer (if contains modem's reply)
void readBuf(char *buf, uint16_t len)
{
  // Check is modem responding
  if( len == 0 ) {
    if( cmd_flag ) {
      raise_error("Modem is not responding!");
    }
    return;
  }
  // Check a sent command waiting for reply
  if( cmd_flag && !strcmp(buf+len-2, "OK") ) {
    raise_error("No reply for the pending command!");
  }
  // Check model not returning ERROR
  if( strncmp(buf+1, "ERROR", 5) == 0 ) {
    raise_error("Modem ERROR!");
  }
  
  disp(buf, "RCV: ", cmd_flag);
  readSMS(buf);
}

// Read modem's reply to the buffer
void readModemToBuf()
{
  uint16_t pos = 0;
  
  while(modem.available()) 
  {                                
    // read a byte from modem
    char ch = (char) modem.read();
    // skip first empty lines 
    if (ch == '\n') continue; 
    // write char to buffer
    mbuf[pos++] = ch ;
    mbuf[pos]   = '\0';
  }

  readBuf(mbuf, pos);
}

// Initialize modem
void initModem()
{ 
  // Once the handshake test is successful, it will back to OK
  sendCmd("AT");                        
  // Disable echo AT commands
  sendCmd("ATE0");                        
  // Signal quality test, value range is 0-31 , 31 is the best
  sendCmd("AT+CSQ");                    
  // Read SIM information to confirm whether the SIM is plugged
  sendCmd("AT+CCID");                   
  // Check whether it has registered in the network
  sendCmd("AT+CREG?");                  

  // Configure SMS messsage in TEXT mode
  sendCmd("AT+CMGF=1");                 
  // Decide how newly arrived SMS messages should be handled (redirect to Arduino)
  sendCmd("AT+CNMI=1,2,0,0,0");         
  // Show only new SMS messages
  sendCmd("AT+CMGL=\"REC UNREAD\"");    
}

// Forward IDE serial to modem
void forwardSerialToModem()
{
  while (Serial.available()) 
  {
    modem.write(Serial.read());      
  }
}

// Send a SMS message
void sendTextMessage(const char *sms)
{
  modem.print(F("AT+CMGF=1\r")); 
  delay(800);
  // contact number of recipient
  modem.print(F("AT+CMGS=\""RECIPIENT"\"\r"));  
  delay(800);
  // SMS text
  modem.print(sms);  
  modem.print(F("\r"));
  delay(800);
  modem.println((char)26);
  modem.println();
  delay(1000);
}

// Check modem signal and simcard status
void checkModem()
{
  // Check modem status every 10 calls
  if( ++skip_ncalls < 10 )
    return;
  skip_ncalls = 0;
     
  sendCmd("AT+CPIN?");
  sendCmd("AT+CSQ");
  //digitalWrite(ALARM_LED, HIGH);
  //delay(60);
  //digitalWrite(ALARM_LED, LOW);
}

// Check wheather button is pusshed and react accordingly
void checkButton()
{ 
  // Skip if button is not pressed
  if( digitalRead(PUSH_BUTTON) == LOW )
    return;

  // Turn off alarm LED if it is still on
  if( digitalRead(ALARM_LED) == HIGH ) {
    digitalWrite(ALARM_LED, LOW);
    return;    
  }
  
  bool arm = false;
  bool disarm = false;
  lastTime = millis();
  
  while( digitalRead(PUSH_BUTTON) == HIGH ) {
    // Activate disarm
    if( (millis() - lastTime) > DISARM_BTN_DLY && !disarm && !arm ) {
      disarm = true;
      disp("Disarming", "BTN: ", false);
      digitalWrite(STATUS_LED, HIGH);
      beep();
    }
    // Activate arm 
    if( (millis() - lastTime) > ARM_BTN_DLY && !arm ) {
      disarm = false;
      arm = true;
      disp("Arming", "BTN: ", false);
      digitalWrite(STATUS_LED, LOW);
      digitalWrite(ALARM_LED, HIGH);
      beepTwice();
    }
    delay(50);
  }
  digitalWrite(STATUS_LED, LOW);
  digitalWrite(ALARM_LED, LOW);

  if( disarm ) {
    disp("Sending SMS", "INF: ");
    delay(1000);
    sendTextMessage(DISARM_SMS);
  }
   
  if( arm ) {
    disp("Sending SMS", "INF: ");
    delay(1000);
    sendTextMessage(ARM_SMS);
  }
}

// ===================================================================================

// Main setup function
void setup()
{ 
  // Set status LED pin as output
  pinMode(STATUS_LED, OUTPUT); 
  pinMode(ALARM_LED, OUTPUT); 
  pinMode(PUSH_BUTTON, INPUT_PULLDOWN);
  digitalWrite(STATUS_LED, LOW);
  digitalWrite(ALARM_LED, LOW);
    
  // Begin serial communication with Arduino IDE (Serial Monitor)
  Serial.begin(9600);
  
  // Begin serial communication with modem 
  modem.begin(9600);
  cmd_flag = false;
  skip_ncalls = 0;

  // Initialize display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); /*don't proceed, loop forever*/
  }
  display.setTextColor(WHITE);   

  // Initialize Logo effect
  drawLogo();

  // Initialize
  initModem();
}


// Main loop function
void loop()
{
  // One second delay
  delay(1000); 
  // Check modem signal and simcard status
  checkModem();
  // Forward from IDE serial to modem
  forwardSerialToModem();
  // Read modem 
  readModemToBuf();
  // check button status
  checkButton();
}
