#include <Arduino.h>

#if defined(ARDUINO_ARCH_AVR)
#include <SoftwareSerial.h>
#endif

#if defined(ADAFRUIT_QTPY_M0)
#include <Adafruit_NeoPixel.h>
#endif

#ifndef UART_BAUD
#define UART_BAUD 9600
#endif

#ifndef USB_BAUD
#define USB_BAUD 115200
#endif

#ifndef SEND_INTERVAL_MS
#define SEND_INTERVAL_MS 5000UL
#endif

#ifndef TEST_NODE_ID
#define TEST_NODE_ID 7
#endif

#ifndef TEST_TEMP_C
#define TEST_TEMP_C 23.4f
#endif

#ifndef TEST_BATTERY_V
#define TEST_BATTERY_V 3.98f
#endif

#ifndef SOFTSERIAL_RX_PIN
#define SOFTSERIAL_RX_PIN 4
#endif

#ifndef SOFTSERIAL_TX_PIN
#define SOFTSERIAL_TX_PIN 2
#endif

#if defined(ARDUINO_ARCH_NRF52)
#include <Adafruit_TinyUSB.h>
#define DEBUG_SERIAL Serial
#define DATA_SERIAL Serial1
#define HAVE_DEBUG_SERIAL 1
#elif defined(ARDUINO_ARCH_SAMD)
#define DEBUG_SERIAL Serial
#define DATA_SERIAL Serial1
#define HAVE_DEBUG_SERIAL 1
#elif defined(ARDUINO_ARCH_AVR)
// Classic Nano uses a configurable software UART for the outgoing mesh line.
static SoftwareSerial dataSerial(SOFTSERIAL_RX_PIN, SOFTSERIAL_TX_PIN);
#define DEBUG_SERIAL Serial
#define DATA_SERIAL dataSerial
#define HAVE_DEBUG_SERIAL 1
#else
#define DEBUG_SERIAL Serial
#define DATA_SERIAL Serial1
#define HAVE_DEBUG_SERIAL 1
#endif

static unsigned long next_send_at = 0;
static unsigned long sample_counter = 0;

#if defined(ADAFRUIT_QTPY_M0)
static Adafruit_NeoPixel statusPixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
#endif

static void pulseLed() {
#if defined(ADAFRUIT_QTPY_M0)
  statusPixel.setPixelColor(0, statusPixel.Color(0, 20, 0));
  statusPixel.show();
  delay(40);
  statusPixel.clear();
  statusPixel.show();
#ifdef LED_BUILTIN
#else
  digitalWrite(LED_BUILTIN, HIGH);
  delay(40);
  digitalWrite(LED_BUILTIN, LOW);
#endif
#endif
}

static void sendSample() {
  float temp_c = TEST_TEMP_C + (sample_counter % 10) * 0.1f;

  // Emit the exact line format expected by companion_sensor_ble_uart_read.
  DATA_SERIAL.print("MC,N=");
  DATA_SERIAL.print(TEST_NODE_ID);
  DATA_SERIAL.print(",T=");
  DATA_SERIAL.print(temp_c, 1);
  DATA_SERIAL.print(",B=");
  DATA_SERIAL.print(TEST_BATTERY_V, 2);
  DATA_SERIAL.print('\n');

#if HAVE_DEBUG_SERIAL
  DEBUG_SERIAL.print("Sent: MC,N=");
  DEBUG_SERIAL.print(TEST_NODE_ID);
  DEBUG_SERIAL.print(",T=");
  DEBUG_SERIAL.print(temp_c, 1);
  DEBUG_SERIAL.print(",B=");
  DEBUG_SERIAL.println(TEST_BATTERY_V, 2);
#endif

  pulseLed();
  sample_counter++;
}

void setup() {
#if defined(ADAFRUIT_QTPY_M0)
  pinMode(12, OUTPUT);
  digitalWrite(12, HIGH);
  statusPixel.begin();
  statusPixel.clear();
  statusPixel.show();
#else
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
#endif

  DEBUG_SERIAL.begin(USB_BAUD);
  DATA_SERIAL.begin(UART_BAUD);

  delay(1500);
#if HAVE_DEBUG_SERIAL
  DEBUG_SERIAL.println("UART test ready");
#if defined(ARDUINO_ARCH_AVR)
  DEBUG_SERIAL.print("USB Serial: debug output; SoftwareSerial TX=D");
  DEBUG_SERIAL.println(SOFTSERIAL_TX_PIN);
#else
  DEBUG_SERIAL.println("USB Serial: debug output");
  DEBUG_SERIAL.println("UART TX: MC,N=<id>,T=<temp_c>,B=<battery_v>");
#endif
#endif

  next_send_at = millis() + 1000;
}

void loop() {
  unsigned long now = millis();
  if ((long)(now - next_send_at) >= 0) {
    sendSample();
    next_send_at = now + SEND_INTERVAL_MS;
  }
}
