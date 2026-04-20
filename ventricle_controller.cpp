#include <Arduino.h>
#include <vector>
#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <string>
#include <Wire.h>

// Left device (ventricle controller): /dev/cu.usbmodem14401

// WiFi and MQTT configuration
WiFiClient wifi_client;
MqttClient mqtt_client(wifi_client);

const char* ssid = "AirPennNet-Device";
const char* password = "penn1740wifi";
const char* ADDRESS = {"mqtt-dev.precise.seas.upenn.edu"};
const int BROKER_PORT = 1883;
const char* BROKER_USERNAME = {"cis441-541_2025"};
const char* BROKER_PASSWORD = {"cukwy2-geNwit-puqced"};

const char* UPDATE_TOPIC {"cis441-541/PacingAhead/Update"};

const int sensePin = 7;   // INPUT: RX from heart (VSense)
const int pacePin  = 8;   // OUTPUT: TX to heart (VPace)
const int ledPin = LED_BUILTIN; // OUTPUT: Built in LED for Arduino

enum State { IDLE, SENSE, PACE};
State state = IDLE;

TaskHandle_t Task1;
TaskHandle_t Task2;

void TaskMQTT(void *pvParameters);
void TaskController(void *pvParameters);

bool hpEnable = false;
unsigned long lastBeat = 0;
const int LRI = 1500; // Slowest allowed interval when pacing: 40 bpm
const int URI = 333; // Fastest interval when pacing: 180 bpm
const int VRP = 150; // Ventricular Refractory Period
const int HRI = 1600; // Hysteris interval

const int QOS = 1;

String msg = "";

void setup() {
  Serial.begin(9600);

  pinMode(sensePin, INPUT);
  pinMode(pacePin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  
  while (!Serial); // Wait for the serial monitor to open

    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Connect to the broker using the options - sending from the client which needs to specify broker address, port, username, password
    mqtt_client.setId("nano33");
    mqtt_client.setUsernamePassword(BROKER_USERNAME,BROKER_PASSWORD);
    Serial.print("Connecting...");
    
    // Synchronous call that blocks until the client receives a success or fail connection 
    if (mqtt_client.connect(ADDRESS, BROKER_PORT)){
        Serial.print("Connected to broker succesfully!");
    }
    else{
        Serial.print("Connection to broker failed");
        while(true){}
    }

     mqtt_client.subscribe(UPDATE_TOPIC, QOS); // Subscribe to CGM topic 

    // Create FreeRTOS tasks for TaskMQTT and TaskOpenAPS
    BaseType_t task_mqtt = xTaskCreate(TaskMQTT, "TaskMQTT", 1000, NULL, 1, &Task1);
    BaseType_t task_controller =  xTaskCreate(TaskController, "TaskController", 1000, NULL, 2, &Task2);
    
    vTaskStartScheduler();
}

void TaskMQTT(void *pvParameters) {
  // Continuously poll for MQTT messages
  Serial.println("MQTT Task Running");
  while (1){
      // Lock the client to poll for messages
      mqtt_client.poll();
      
      // Unlock the client in case the other task needs it 
      vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void TaskController(void *pvParmeters){
  while (true){
    unsigned long time_ms = millis();

    if (digitalRead(sensePin) == HIGH){
      unsigned long temp_interval = time_ms - lastBeat;
      Serial.print("Natural beat with interval ");
      Serial.println(temp_interval);
      lastBeat = time_ms;

      // Send data to monitor
      mqtt_client.beginMessage(UPDATE_TOPIC, false, QOS);
      mqtt_client.print("VSense:");
      mqtt_client.print(temp_interval);
      mqtt_client.endMessage();

      hpEnable = true;
      vTaskDelay(pdMS_TO_TICKS(VRP)); // Wait VRP time before next event happens
    }
    else {
      // Find if we have passed the required interval amount. of time
      unsigned long RI = 0;
      if (hpEnable){
        RI = HRI;
      }
      else{
        RI = LRI;
      }
      // Now we are in pacing mode
      if (time_ms - lastBeat >= RI){
        lastBeat = time_ms;
        Serial.print("Starting pacing with interval ");
        Serial.println(RI);

        digitalWrite(pacePin, HIGH);

        // BLINK Logic to indicate we started sensing
        digitalWrite(ledPin, HIGH);
        delay(50);
        digitalWrite(ledPin, LOW);

        digitalWrite(pacePin, LOW);

        // Send data to monitor
        mqtt_client.beginMessage(UPDATE_TOPIC, false, QOS);
        mqtt_client.print("VPace:");
        mqtt_client.print(RI);
        mqtt_client.endMessage();

        hpEnable = false;
        vTaskDelay(pdMS_TO_TICKS(VRP)); // Wait VRP time before next event happens
      }
    }
  }
}

void loop() {
  // Do nothing
}