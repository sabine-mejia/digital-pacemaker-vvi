#include <Arduino.h>
#include <vector>
#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <string>
#include <Wire.h>

// Device: /dev/cu.usbmodem14301
// Right device - random heart

// All the necessary pacing information
const int LRI = 1500; // Slowest allowed interval when pacing: 40 bpm
const int URI = 333; // Fastest interval when pacing: 180 bpm
const int VRP = 150; // Ventricular Refractory Period
const int HRI = 1600; // Hysteris interval

const int pacerPin = 8;   // INPUT RX signal from pacemaker
const int beatPin  = 7;   // OUTPUT TX signal to pacemaker
const int ledPin   = LED_BUILTIN;

const int min_wait = 200; // Fastest time that the heart can beat, counting for the VRP
const int max_wait = 3000; // Longest time - to indicate heart is beating too slow
unsigned long lastBeat = 0;

unsigned long rand_interval = 0;

void setup() {
  Serial.begin(9600);

  pinMode(pacerPin, INPUT);
  pinMode(beatPin, OUTPUT);
  pinMode(ledPin, OUTPUT);

  randomSeed(analogRead(A0));
}

void loop() {
    unsigned long time_ms = millis();

    if (digitalRead(pacerPin) == HIGH){
        Serial.println("Received a pace signal from the pacemaker");
        lastBeat = time_ms;
        rand_interval = 0;
        delay(50);
        //rand_interval = 0;
    }
    else{
        if (rand_interval == 0){
            rand_interval = random(min_wait, max_wait);
        }

        // We have passed interval time without a pace from the pacemaker so we can create a beat
        if (rand_interval <= time_ms - lastBeat){
            lastBeat = time_ms;
            Serial.print("Natural beat with interval ");
            Serial.println(rand_interval);

            digitalWrite(beatPin, HIGH);

            // BLINK Logic to indicate a natural heartbeat
            digitalWrite(ledPin, HIGH);
            delay(50);
            digitalWrite(ledPin, LOW);

            digitalWrite(beatPin, LOW);

            rand_interval = 0;
        }
    }
}

