#include <ESP32Servo.h>
#include <WiFi.h>
#include <DHT.h>

const char* ssid = "RW5800-273C";
const char* password = "Almazan2024";

WiFiServer server(8888);
WiFiClient client;

// Relay pins for each room (OCCUPIED, LIGHT)
int roomRelays[4][2] = {
  { 13, 14 },  // ROOM 101
  { 26, 25 },  // ROOM 201
  { 0, 0 },    // ROOM 301
  { 0, 0 }     // ROOM 401
};

// Reset buttons
int resetButtons[4] = { 32, 33, 0, 0 };
unsigned long lastDebounceTime[4] = { 0 };
bool lastButtonState[4] = { HIGH };

// DHT22 setup
#define DHTPIN 15
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Servo for ROOM 201
Servo room201Servo;
#define SERVO_PIN 27

bool room101Active = false;
unsigned long lastDHTRead = 0;
unsigned long lastTempSend = 0;
const long tempSendInterval = 5000;
const float tempThreshold = 25.0;

// Numerical Method
#define MAX_READINGS 10
float tempReadings[MAX_READINGS];
int tempIndex = 0;
bool readingsFilled = false;

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  server.begin();
  Serial.println("Server started");

  for (int i = 0; i < 4; i++) {
    if (roomRelays[i][0]) pinMode(roomRelays[i][0], OUTPUT);
    if (roomRelays[i][1]) pinMode(roomRelays[i][1], OUTPUT);
    digitalWrite(roomRelays[i][0], HIGH);
    digitalWrite(roomRelays[i][1], HIGH);
    if (resetButtons[i]) pinMode(resetButtons[i], INPUT_PULLUP);
  }

  dht.begin();
  room201Servo.attach(SERVO_PIN);
  room201Servo.write(0);
}

void loop() {
  if (!client || !client.connected()) {
    client = server.available();
    return;
  }

  if (client.available()) {
    String command = client.readStringUntil('\n');
    command.trim();
    Serial.println("Received: " + command);
    handleCommand(command);
  }

  // --- FIXED PHYSICAL RESET BUTTON LOGIC ---
  for (int i = 0; i < 4; i++) {
    if (resetButtons[i] == 0) continue;

    bool buttonPressed = digitalRead(resetButtons[i]) == LOW;
    if (buttonPressed && lastButtonState[i] == HIGH) {
      if (millis() - lastDebounceTime[i] > 300) {
        String msg = "RESET:" + String(i + 1) + ":01";
        client.println(msg);
        Serial.println("Sent: " + msg);

        digitalWrite(roomRelays[i][0], HIGH);
        digitalWrite(roomRelays[i][1], HIGH);
        lastDebounceTime[i] = millis();

        if (i == 0) {
          room101Active = false;
          tempIndex = 0;
          readingsFilled = false;
          for (int j = 0; j < MAX_READINGS; j++) tempReadings[j] = 0.0;
          client.println("TEMP:0.0");
          lastTempSend = 0;
        }

        if (i == 1) {
          room201Servo.write(0);
        }
      }
    }
    lastButtonState[i] = !buttonPressed;
  }

  // Temperature updates
  if (room101Active && millis() - lastTempSend > tempSendInterval) {
    float temp = dht.readTemperature();
    if (!isnan(temp)) {
      tempReadings[tempIndex] = temp;
      tempIndex = (tempIndex + 1) % MAX_READINGS;
      if (tempIndex == 0) readingsFilled = true;

      float predictedTemp = temp;
      if (readingsFilled) {
        float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
        for (int i = 0; i < MAX_READINGS; i++) {
          sumX += i;
          sumY += tempReadings[i];
          sumXY += i * tempReadings[i];
          sumX2 += i * i;
        }

        float slope = (MAX_READINGS * sumXY - sumX * sumY) / (MAX_READINGS * sumX2 - sumX * sumX);
        float intercept = (sumY - slope * sumX) / MAX_READINGS;
        predictedTemp = slope * MAX_READINGS + intercept;

        client.println("PREDICT:" + String(predictedTemp, 1));
        Serial.println("Sent: PREDICT:" + String(predictedTemp, 1));
      }

      client.println("TEMP:" + String(temp, 1));
      Serial.println("Sent: TEMP:" + String(temp, 1));

      if (temp >= tempThreshold) {
        client.println("ALERT:HIGH_TEMP");
        Serial.println("Sent: ALERT:HIGH_TEMP");
      }

      lastTempSend = millis();
    } else {
      Serial.println("Failed to read temperature from DHT sensor");
    }
  }
}

void handleCommand(String cmd) {
  if (cmd.startsWith("ROOM:")) {
    int roomNum = cmd.substring(5, 8).toInt();
    String state = cmd.substring(9);
    int index = (roomNum / 100) - 1;

    if (index >= 0 && index < 4 && roomRelays[index][0] != 0) {
      if (state == "OCCUPIED") {
        digitalWrite(roomRelays[index][0], LOW);
        if (index == 0) {
          room101Active = true;
          tempIndex = 0;
          readingsFilled = false;
          for (int i = 0; i < MAX_READINGS; i++) tempReadings[i] = 0.0;
        }
        if (index == 1) {
          room201Servo.write(180);
          Serial.println("ROOM 201 OCCUPIED: Servo moved to 180°");
        }
      } else {
        digitalWrite(roomRelays[index][0], HIGH);
        if (index == 0) {
          room101Active = false;
          tempIndex = 0;
          readingsFilled = false;
          for (int i = 0; i < MAX_READINGS; i++) tempReadings[i] = 0.0;
          client.println("TEMP:0.0");
          lastTempSend = 0;
        }
        if (index == 1) {
          room201Servo.write(0);
          Serial.println("ROOM 201 VACANT: Servo moved to 0°");
        }
      }
    }

  } else if (cmd.startsWith("LIGHT:")) {
    int roomNum = cmd.substring(6, 9).toInt();
    String state = cmd.substring(10);
    int index = (roomNum / 100) - 1;

    if (index >= 0 && index < 4 && roomRelays[index][1] != 0) {
      if (state == "ON") {
        digitalWrite(roomRelays[index][1], LOW);
      } else {
        digitalWrite(roomRelays[index][1], HIGH);
      }
    }

  } else if (cmd.startsWith("RESET:")) {
    int parts[3];
    int partIndex = 0;
    int start = 6;  // start after "RESET:"
    for (int i = start; i < cmd.length() && partIndex < 3; i++) {
      int colon = cmd.indexOf(':', i);
      if (colon == -1) colon = cmd.length();
      parts[partIndex++] = cmd.substring(i, colon).toInt();
      i = colon;
    }

    int roomIndex = parts[0] - 1;
    if (roomIndex >= 0 && roomIndex < 4) {
      if (roomRelays[roomIndex][0] != 0) digitalWrite(roomRelays[roomIndex][0], HIGH);
      if (roomRelays[roomIndex][1] != 0) digitalWrite(roomRelays[roomIndex][1], HIGH);

      if (roomIndex == 0) {
        room101Active = false;
        tempIndex = 0;
        readingsFilled = false;
        for (int i = 0; i < MAX_READINGS; i++) tempReadings[i] = 0.0;
        client.println("TEMP:0.0");
        lastTempSend = 0;
      }

      if (roomIndex == 1) {
        room201Servo.write(0);
        Serial.println("ROOM 201 RESET: Servo moved to 0°");
      }

      client.println("RESET:" + String(roomIndex + 1) + ":" + String(parts[1]) + ":OK");
    }
  }
}
