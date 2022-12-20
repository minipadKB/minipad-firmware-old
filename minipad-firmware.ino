// Important imports
#include "Keyboard.h"
#include "EEPROM.h"

// Define the ports used for the two hall effect sensors
#define PIN1 A1
#define PIN2 A2

// Define a safemode boundary that is checked for upon firmware bootup. If both pins read a value <= this value,
// a safemode is activated causing the config to temporarily switch to the default config
#define SAFEMODE_BOUNDARY 20

// Minimum sensitivity for rapid trigger, minimum amount the hysteresis have to be below the rest position and
// the minimum difference between the lower and upper hysteresis. Used to ensure that fluctuation will not accidentally trigger the key
#define TOLERANCE 8

// The version of the firmware
#define FIRMWARE_VERSION "20221219.2"

//
// Configuration of the behavior of the keypad
//

// Configuration struct stored in the EEPROM
struct Configuration 
{
    // A UID used to identify whether a configuration is already stored in the EEPROM
    int uid;

    // The name of the keypad, used to distinguish it from other
    char name[128];

    // Bool whether rapid trigger is enabled or not
    bool rapidTrigger;

    // The sensitivity of the rapid trigger
    int rapidTriggerSensitivity;

    // The value below which the button is pressed if not using rapid trigger
    int lowerHysteresis;

    // The value above which the button is released if not using rapid trigger
    int upperHysteresis;

    // The corresponding keys sent via HID interface
    char key1;
    char key2;

    // The value read when the keys are in rest position
    int key1RestPosition;
    int key2RestPosition;

    // The value read when the keys are pressed all the way down
    int key1DownPosition;
    int key2DownPosition;

    // Bools whether HID commands are sent on that key
    bool key1HIDEnabled;
    bool key2HIDEnabled;
};

//
// Firmware workflow
//

// Default configuration and configuration loaded from the EEPROM
#define CONFIG_UID 2247
Configuration defaultConfig = { CONFIG_UID, "minipad", true, 10, 300, 330, 'x', 'y', 450, 450, 150, 150, true, true };
Configuration config;

// Integers to remember the lowest/highest value the analog value gets to if rapid trigger is enabled.
// If the button is pressed down, it saves the lowest value it got to. If the value read is higher
// than this value by the rapid trigger sensitivity, the button is released. The other way around,
// if the button is not pressed down, it saves the highest value it got to. If the value read is lower
// than this value by the rapid trigger sensitivity, the button is pressed.
int lastRapidTriggerValueKey1 = 0;
int lastRapidTriggerValueKey2 = 0;

// Remember the state of both buttons to not send irrelevant HID commands
bool key1Pressed = false;
bool key2Pressed = false;

void setup()
{
  // Load the configuration
  EEPROM.get(0, config);

  // If the configuration UID does not match and therefore there is no configuration (or an old version) deposited yet,
  // write the default configuration to the EEPROM and set the current config to the default one
  if(config.uid != CONFIG_UID)
  {
    EEPROM.put(0, defaultConfig);
    config = defaultConfig;
  }

  // Check whether both buttons are held down while bootup and if so, temporarily switch to the default config to enter a safe mode.
  if((mapToRange400(analogRead(PIN1), config.key1RestPosition, config.key1DownPosition) <= SAFEMODE_BOUNDARY) && (mapToRange400(analogRead(PIN2), config.key2RestPosition, config.key2DownPosition) <= SAFEMODE_BOUNDARY))
  {
      Configuration newConfig = defaultConfig;
      newConfig.key1RestPosition = config.key1RestPosition;
      newConfig.key2RestPosition = config.key2RestPosition;
      newConfig.key1DownPosition = config.key1DownPosition;
      newConfig.key2DownPosition = config.key2DownPosition;
      newConfig.key1HIDEnabled = config.key1HIDEnabled;
      newConfig.key2HIDEnabled = config.key2HIDEnabled;
      config = newConfig;
  }

  // Initialize the serial and HID interface
  Keyboard.begin();
  Serial.begin(115200);
}

void loop() 
{
  // Check for any serial commands received for configuration
  if(Serial.available())
    handleSerialInput();

  // Read the hall effect sensors
  int value1 = analogRead(PIN1);
  int value2 = analogRead(PIN2);

  // Map the values to the 0-400 range
  value1 = mapToRange400(value1, config.key1DownPosition, config.key1RestPosition);
  value2 = mapToRange400(value2, config.key2DownPosition, config.key2RestPosition);
  
  int key1State = 750;
  int key2State = 750;
  if(key1Pressed)
    key1State = 650;
  if(key2Pressed)
    key2State = 650;

  //Serial.println("min_analog:0\tKey_1:" + String(value1) + "\tKey_2:" + String(value2) + "\tKey_1_State:" + String(key1State) + "\tKey_2_State:" + String(key2State) + "\tKey_1_Rapid_Trigger_Value:" + String(lastRapidTriggerValueKey1) + "\tKey_2_Rapid_Trigger_Value:" + String(lastRapidTriggerValueKey2) + "\tmax_analog:1024");
  

  if(config.rapidTrigger)
  {
    // Check whether the read value for key 1 dropped below/rises above the actuation point depending on the current button state.
    // If the button is not pressed and the value drops more than (sensitivity) below the highest recorded value, press the key
    // Furthermore, check if the value is at least 10 below the rest position such that the user won't accidentally
    // get stuck with a pressed down key while the key is all the way up.
    if(value1 <= lastRapidTriggerValueKey1 - config.rapidTriggerSensitivity && !key1Pressed)
      pressKey1();
    // If the button is pressed and the value rises more than (sensitivity) above the highest recorded value, release the key
    else if(value1 >= lastRapidTriggerValueKey1 + config.rapidTriggerSensitivity && key1Pressed)
      releaseKey1();

    // If the key is pressed, save the lowest value; If the key is not pressed, save the highest value
    if((key1Pressed && value1 < lastRapidTriggerValueKey1) || (!key1Pressed && value1 > lastRapidTriggerValueKey1))
      lastRapidTriggerValueKey1 = value1;    

    // Repeat the same for the second key
    if(value2 <= lastRapidTriggerValueKey2 - config.rapidTriggerSensitivity && !key2Pressed)
      pressKey2();
    else if(value2 >= lastRapidTriggerValueKey2 + config.rapidTriggerSensitivity && key2Pressed)
      releaseKey2();

    if((key2Pressed && value2 < lastRapidTriggerValueKey2) || (!key2Pressed && value2 > lastRapidTriggerValueKey2))
      lastRapidTriggerValueKey2 = value2;
  }
  else
  {
    // Handle actuation for key 1 by checking whether the value passes the lower or upper hysteresis
    if(value1 <= config.lowerHysteresis && !key1Pressed)
      pressKey1();
    else if(value1 >= config.upperHysteresis && key1Pressed)
      releaseKey1();

    // Handle actuation for key 2 by checking whether the value passes the lower or upper hysteresis
    if(value2 <= config.lowerHysteresis && !key2Pressed)
      pressKey2();
    else if(value2 >= config.upperHysteresis && key2Pressed)
      releaseKey2();
  }
}

void handleSerialInput()
{
  while(Serial.available())
  {
    String input = Serial.readStringUntil('\n');
    String key = getValue(input, 0);
    String value = getValue(input, 1);


    if(key == "reset")
    {
      Configuration newConfig = defaultConfig;
      newConfig.key1RestPosition = config.key1RestPosition;
      newConfig.key2RestPosition = config.key2RestPosition;
      newConfig.key1DownPosition = config.key1DownPosition;
      newConfig.key2DownPosition = config.key2DownPosition;
      newConfig.key1HIDEnabled = config.key1HIDEnabled;
      newConfig.key2HIDEnabled = config.key2HIDEnabled;
      config = newConfig;
      Serial.println("Config resetted.");
      continue;
    }
    else if(key == "save")
    {
      EEPROM.put(0, config);
      Serial.println("Config written to EEPROM.");
      continue;
    }
    else if(key == "ping")
    {
      Serial.println("PONG " + String(FIRMWARE_VERSION) + " | " + config.name);
      continue;
    }
    if(value == "")
    {
      if(key == "rt" || key == "get")
        Serial.println("GET rt=" + String(config.rapidTrigger));
      if(key == "rts" || key == "get")
        Serial.println("GET rts=" + String(config.rapidTriggerSensitivity));
      if(key == "lh" || key == "get")
        Serial.println("GET lh=" + String(config.lowerHysteresis));
      if(key == "uh" || key == "get")
        Serial.println("GET uh=" + String(config.upperHysteresis));
      if(key == "key1" || key == "get")
        Serial.println("GET key1=" + String(config.key1));
      if(key == "key2" || key == "get")
        Serial.println("GET key2=" + String(config.key2));
      if(key == "k1rp" || key == "get")
        Serial.println("GET k1rp=" + String(config.key1RestPosition));
      if(key == "k2rp" || key == "get")
        Serial.println("GET k2rp=" + String(config.key2RestPosition));
      if(key == "k1dp" || key == "get")
        Serial.println("GET k1dp=" + String(config.key1DownPosition));
      if(key == "k2dp" || key == "get")
        Serial.println("GET k2dp=" + String(config.key2DownPosition));
      if(key == "hid1" || key == "get")
        Serial.println("GET hid1=" + String(config.key1HIDEnabled));
      if(key == "hid2" || key == "get")
        Serial.println("GET hid2=" + String(config.key2HIDEnabled));
      if(key == "name" || key == "get")
        Serial.println("GET name=" + String(config.name));
      if(key == "tol" || key == "get")
        Serial.println("GET tol=" + String(TOLERANCE));
      if(key == "get")
        Serial.println("GET END");
      continue;
    }

    if(key == "name")
    {
      value.trim();
      if(value.length() < 1 || value.length() > 128)
      {
        Serial.println("The length of 'name' must be between 1 and 128.");
        continue;
      }

      strcpy(config.name, value.c_str());
      Serial.println("'name' was set to '" + value + "'");
      
      continue; 
    }

    int valueInt = atoi(value.c_str());
    if(String(valueInt) != value)
    {
      if(value.length() == 1)
        valueInt = value[0];
      else if(value == "true")
        valueInt = 1;
      else if(value == "false")
        valueInt = 0;
      else
        continue;
    }

    if(key == "rt")
    {
      if(valueInt != 0 && valueInt != 1)
      {
        Serial.println("Allowed values for 'rapidTrigger' are true/1 or false/0");
        continue;
      }

      config.rapidTrigger = valueInt == 1;
      Serial.println("'rapidTrigger' was set to '" + String(config.rapidTrigger ? "true" : "false") + "'");
    }
    else if(key == "rts")
    {
      if(valueInt < TOLERANCE || valueInt > 400)
      {
        Serial.println("Allowed range for 'rapidTriggerSensitivity' is " + String(TOLERANCE) + "-400");
        continue;
      }

      config.rapidTriggerSensitivity = valueInt;
      Serial.println("'rapidTriggerSensitivity' was set to '" + value + "'");
    }
    else if(key == "lh")
    {
      if(valueInt < 0 || valueInt > 400 - TOLERANCE)
      {
        Serial.println("Allowed range for 'lowerHysteresis' is 0-" + String(400 - TOLERANCE));
        continue;
      }
      else if(valueInt > config.upperHysteresis - TOLERANCE)
      {
        Serial.println("The value 'lowerHysteresis' must be at least " + String(TOLERANCE) + " less than the value 'upperHysteresis'");
        continue;
      }

      config.lowerHysteresis = valueInt;
      Serial.println("'lowerHysteresis' was set to '" + value + "'");
    }
    else if(key == "uh")
    {
      if(valueInt < 0 || valueInt > 400 - TOLERANCE)
      {
        Serial.println("Allowed range for 'upperHysteresis' is 0-" + String(400 - TOLERANCE));
        continue;
      }
      else if(valueInt < config.lowerHysteresis + TOLERANCE)
      {
        Serial.println("The value 'upperHysteresis' must be at least " + String(TOLERANCE) + " more than the value 'lowerHysteresis'");
        continue;
      }

      config.upperHysteresis = valueInt;
      Serial.println("'upperHysteresis' was set to '" + value + "'");
    }
    else if(key == "key1" || key == "key2")
    {
      if(valueInt < 97 || valueInt > 122)
      {
        Serial.println("The value '" + key + "' must be between 97 and 122 (a-z)");
        continue;
      }

       if(key == "key1")
         config.key1 = (char) valueInt;
       else
         config.key2 = (char) valueInt;

      Serial.println("'" + key + "' was set to '" + value + "'");
    }
    else if(key == "k1rp" || key == "k2rp")
    {
      if(valueInt < 0 || valueInt > 1023)
      {
        Serial.println("Allowed range for '" + key + "' is 0-1024");
        continue;
      }

      if(key == "k1rp")
        config.key1RestPosition = valueInt;
      else
        config.key2RestPosition = valueInt;

      Serial.println("'" + key + "' was set to '" + value + "'");
    }
    else if(key == "k1dp" || key == "k2dp")
    {
      if(valueInt < 0 || valueInt > 1023)
      {
        Serial.println("Allowed range for '" + key + "' is 0-1024");
        continue;
      }

      if(key == "k1dp")
        config.key1DownPosition = valueInt;
      else
        config.key2DownPosition = valueInt;

      Serial.println("'" + key + "' was set to '" + value + "'");
    }
    else if(key == "hid1" || key == "hid2")
    {
      if(valueInt != 0 && valueInt != 1)
      {
        Serial.println("Allowed values for '" + key + "' are true/1 or false/0");
        continue;
      }

      if(key == "hid1")
        config.key1HIDEnabled = valueInt == 1;
      else
        config.key2HIDEnabled = valueInt == 1;

      Serial.println("'" + key + "' was set to '" + String((key == "hid1" ? config.key1HIDEnabled : config.key2HIDEnabled) ? "true" : "false") + "'");
    }
  }
}

int mapToRange400(int value, int min, int max)
{  
  float multiplier = (value - min) * 1.0 / (max - min);
  
  return min(max(round(multiplier * 400), 0), 400);
}

void pressKey1()
{
  key1Pressed = true;
  if(config.key1HIDEnabled)
    Keyboard.press(config.key1);
}

void releaseKey1()
{
  if(config.key1HIDEnabled)
    Keyboard.release(config.key1);
  key1Pressed = false;
}

void pressKey2()
{
  key2Pressed = true;
  if(config.key2HIDEnabled)
    Keyboard.press(config.key2);
}

void releaseKey2()
{
  if(config.key2HIDEnabled)
    Keyboard.release(config.key2);
  key2Pressed = false;
}

String getValue(String data, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int length = data.length() - 1;

  for(int i = 0; i <= length && found <= index; i++)
  {
    if(data.charAt(i) == ' ' || i == length)
    {
        found++;
        strIndex[0] = strIndex[1] + 1;
        strIndex[1] = (i == length) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

