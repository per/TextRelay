#include <SIM900.h>
#include <sms.h>
#include <SoftwareSerial.h>
#include <LowPower.h>
 
SMSGSM sms;

SoftwareSerial debug(10, 11);

int value=0;
int value_old=0;
char number[18];
char message[180];
char value_str[4];
int pin = A0;

#define MODEM_BAUD_RATE 4800

#define USE_SLEEP_INTERRUPT
#define SLEEP_MODE
  
#define RELAY   12
/* 
 Patched board to free up pin 2,3 for interrupt handling
 Now uses 9.10 on the LCD connector, changed in GSM.cpp in GSMSHIELD library.
 http://elecfreaks.com/store/download/datasheet/wireless/EFcom_v1.2.pdf
*/
#ifdef SLEEP_MODE
#define GSM_DTR   2
#define GSM_RI    3
#endif

/*
  Sleep Mode for modem:
  AT+CSCLK=1 to set mode.

  To be able to issue any commands to the SIM900 after this requires that DTR is low.
  Note: DTR should be driven with open collector/drain output or use a level translator.
  The sleep mode is autonomous operation in the SIM900, after DTR is set high sleep mode 
  is entered after a couple of seconds and from time to time the SIM900 will "wake".
  To confirm sleep mode is active with DTR high the SIM900 will not process commands on the serial port.
  
  Incoming SMS or Call is indicated by the RI line (see SIM900 hardware reference doc for timing and phase)
  which can be used to wake up controlling MCU if also in low power mode.
  
  http://elecfreaks.com/store/download/datasheet/rf/SIM900/SIM900_Hardware%20Design_V2.00.pdf
*/
void setup() {
  debug.begin(19200);
  debug.println("Starting foderautomat controller");
 
  
#ifdef USE_SLEEP_INTERRUPT
  //Pin for Interrupt when sim900 has data for us.
  pinMode(GSM_RI, INPUT);
#endif
 
  // Pin to controll relay
  pinMode(RELAY, OUTPUT);

#ifdef SLEEP_MODE
 // When in sleep mode, pin GSM_DTR controlls access (active low)
 // Use auto mode instead...
  pinMode(GSM_DTR, OUTPUT);
  digitalWrite(GSM_DTR, HIGH);

  setSleepMode(false);
#endif

  int started = gsm.begin(MODEM_BAUD_RATE);
  debug.println(started?"status READY":"FAILED to init module!");

  delay(10);

  byte registration = gsm.CheckRegistration();
  debug.print("CheckRegistration: ");
  debug.println(registration);
 
//  int status = sms.SendSMS((char*)"+46709794697", (char*) (registration == REG_REGISTERED ? "Foderautomat started" : "Foderautomat failed to init"));
//  debug.print("Send status is ");
//  debug.println(status);

#ifdef SLEEP_MODE
  // Enable sleep mode
  int sleepmode = gsm.sleepMode(true);
  debug.println(sleepmode?"Sleepy sim900 enabled":"SIM900 on cafeein, not wanna sleep");
#endif

}

void loop() {
  int pos;
  char *p;
  int failedRegistrations = 0;

#ifdef SLEEP_MODE
  setSleepMode(false);
#endif
  
  debug.println("Waiting a moment to wake up...");
  delay(1000);

  // Idea: Pick up incomming call to enable listening to the forrest!
  // Idea: Use a rörelsedetektor to detect motion and call someone to listen.

  while (1 != gsm.CheckRegistration()) {
    debug.println("Check registration failed, retrying");
    delay(5000);
    
    if (failedRegistrations > 20) {
  //    resetModem();
      gsm.begin(MODEM_BAUD_RATE);
      delay(5000);
    }
    failedRegistrations++;
  }

  debug.println("Checking SMS");
  //Check for incoming SMS
  pos = sms.IsSMSPresent(SMS_UNREAD);
  if (pos > 0 && pos <= 20) {
    debug.print("New message, pos=");
    debug.println(pos);
    message[0]=0;
    int gotSMS = sms.GetSMS(pos, number, message, 180);
    debug.print("GetSMS returned: ");
    debug.println(gotSMS);
    capitalize();
    debug.println(message); 
    p=strstr(message,"AUTOMAT");
    if (p) {
      debug.println("COMMAND OK");
      p=strstr(message,"ON");
      if (p) {
        debug.println("ON");
        digitalWrite(RELAY ,HIGH);
        delay(2000);
        sendConfirmation();
      } else {
        p=strstr(message,"OFF");
        if (p) {
          debug.println("OFF");
          digitalWrite(RELAY, LOW);
          delay(2000);
          sendConfirmation();
        }
      }
    } else {
      sendFailure();     
    }
    debug.println("Deleting SMS");
    sms.DeleteSMS((int)pos);
  } else {
    debug.println("No SMS, yet");
  }

#ifdef SLEEP_MODE  
  //Put modem to sleep
  setSleepMode(true);
#endif

#ifdef USE_SLEEP_INTERRUPT
  debug.println("Entering deep sleep!");
  // Allow wake up pin to trigger interrupt on low.
  attachInterrupt(1, wakeUp, LOW);

  // Enter power down state with ADC and BOD module disabled.
  // Wake up when wake up pin is low.
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);

  // Disable external pin interrupt on wake up pin.
  detachInterrupt(1);
#else
  int sleep_cycles = 5;
  int cycle_nbr = 0;
  debug.println("Try to sleep for a while");
  for (cycle_nbr = 0; cycle_nbr < sleep_cycles; cycle_nbr++) {
    // Enter power down state for 8 s with ADC and BOD module disabled
    debug.println("Sleeping for 8s");
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  }
#endif
};

#ifdef SLEEP_MODE
void setSleepMode(boolean enable)
{
  if (enable) {
    digitalWrite(GSM_DTR, HIGH);
  } else {
    //Go out of sleep mode
    digitalWrite(GSM_DTR, LOW);
  }
}
#endif

void resetModem()
{
 gsm.forceON();
 delay(4*1000);
}

void modemPowerOff()
{
  digitalWrite(GSM_ON, HIGH);
  delay(1200);
  digitalWrite(GSM_ON, LOW);
  delay(10000);
}

void wakeUp()
{
  /* Interrupt triggered */
  
}

void capitalize() {
  char *l = message;
  while (*l) {
    *l = toupper(*l);
    l++;
  }
}

void sendConfirmation() {
  int value = bitRead(PORTB, RELAY-8);

  debug.print("Read value ");
  debug.println(value);

  message[0]='\0';
  strcat(message,"STATUS: AUTOMAT ");
  if (value == 1) 
    strcat(message, "ON");
  else
    strcat(message, "OFF");
  sms.SendSMS(number,message);
}

void sendFailure() {
  int value = bitRead(PORTB, RELAY-8);

  debug.print("Read value ");
  debug.println(value);

  message[0]='\0';
  strcat(message,"UNKNOWN COMMAND! CURRENT STATUS: AUTOMAT ");
  if (value == 1) 
    strcat(message, "ON");
  else
    strcat(message, "OFF");
  sms.SendSMS(number,message);
}

