/*
  Informática II - Brazo robotico de 6 servos con ESP32 clasico.
  Arduino IDE (framework Arduino-ESP32) + ESP32Servo.
  Interfaz de PC: Qt/C++ (sin cambios). Protocolo ASCII 115200, 8N1, LF.

  S0 Pinza       MG90S   GPIO12 (pin de arranque: revisar advertencias)
  S1 Muneca 1    MG90S   GPIO14
  S2 Muneca 2    MG90S   GPIO27
  S3 Codo        MG996R  GPIO26
  S4 Hombro      MG996R  GPIO25
  S5 Base        MG996R  GPIO33

  Alimentacion externa 5-6 V para los servos. GND comun con ESP32.
  NO conectar los positivos de los servos a 3V3, USB ni al pin 5V del ESP32.
  IMPORTANTE: la pinza tiene recorrido restringido por precaucion. Los demas
  ejes usan un rango PWM ampliado inicial (750..2250 us), NO necesariamente
  180 grados fisicos. Calibrar sin carga y sin llegar a topes mecanicos.
*/
#include <Arduino.h>
#include <ESP32Servo.h>
#include <DHT.h>    // Biblioteca Adafruit DHT sensor library
#include <math.h>
#include "protocolo.h"          // Analizador en C, compartido con la version ESP-IDF

static const uint8_t PIN_SERVO[CANT_SERVOS] = {12, 14, 27, 26, 25, 33};
static const uint8_t PIN_DHT22 = 32; // DATA del sensor en la garra
static const uint32_t PERIODO_DHT_MS = 2500; // DHT22: no leer mas de una vez cada 2 s
static DHT dht(PIN_DHT22, DHT22);
static uint32_t ultimaLecturaDhtMs = 0;
static const uint8_t PIN_BOTON_PARADA = 23; // Pulsador opcional entre GPIO23 y GND
// Calibracion por servo del ANCHO DE PULSO (us), diferente de los angulos en Qt.
// S0 PINZA: conservar 1000..2000 por seguridad: ya se trabo/calento un MG90S.
// S1..S5: prueba inicial mas amplia: 750..2250 (mas recorrido que 1000..2000).
// Si, SIN CARGA Y SIN TOPES, el servo admite mayor recorrido, ajustar UN eje
// a la vez (p.ej. 650..2350, luego 550..2450); NO forzar 500..2500.
// Algunos servos no giran 180 grados reales, o la estructura mecanica lo impide.
static const int PULSO_MIN_US[CANT_SERVOS] = {1000, 750, 750, 750, 750, 750};
static const int PULSO_MAX_US[CANT_SERVOS] = {2000, 2250, 2250, 2250, 2250, 2250};
static const bool INVERTIDO[CANT_SERVOS] = {false, false, false, false, false, false};
static const uint32_t PERIODO_ACTUALIZACION_MS = 20;
static const int DURACION_MANUAL_MS = 350;

static Servo servo[CANT_SERVOS];
static float actual[CANT_SERVOS] = {90, 90, 90, 90, 90, 90};
static float origen[CANT_SERVOS] = {90, 90, 90, 90, 90, 90};
static int destino[CANT_SERVOS] = {90, 90, 90, 90, 90, 90};
static uint32_t inicioMs = 0;
static uint32_t duracionMs = 0;
static uint32_t ultimaActualizacionMs = 0;
static bool enMovimiento = false;
static bool botonPresionado = false;

static char trama[120];
static size_t tramaLen = 0;
static bool descartarHastaSalto = false;

static float limitar(float v, float minimo, float maximo) {
  if (v < minimo) return minimo;
  if (v > maximo) return maximo;
  return v;
}

// La posicion es la consigna PWM; NO es una medida real del servo.
static void escribirPosiciones() {
  for (int i = 0; i < CANT_SERVOS; ++i) {
    // La consigna 0..180 se traduce explicitamente al PWM calibrado del eje.
    // El movimiento real depende de la unidad de servo y de la mecanica.
    const float angulo = limitar(actual[i], 0, 180);
    const float consigna = INVERTIDO[i] ? (180.0f - angulo) : angulo;
    const float intervalo = float(PULSO_MAX_US[i] - PULSO_MIN_US[i]);
    const int pulsoUs = int(PULSO_MIN_US[i] + (consigna / 180.0f) * intervalo + 0.5f);
    servo[i].writeMicroseconds(pulsoUs);
  }
}

static void actualizarMovimiento() {
  if (!enMovimiento) return;
  const uint32_t transcurrido = millis() - inicioMs;
  float fraccion = (duracionMs == 0) ? 1.0f : (float)transcurrido / duracionMs;
  fraccion = limitar(fraccion, 0.0f, 1.0f);

  for (int i = 0; i < CANT_SERVOS; ++i)
    actual[i] = origen[i] + ((float)destino[i] - origen[i]) * fraccion;

  if (fraccion >= 1.0f) enMovimiento = false;
}

static void congelarMovimiento() {
  actualizarMovimiento();
  enMovimiento = false;
  for (int i = 0; i < CANT_SERVOS; ++i)
    destino[i] = (int)(actual[i] + 0.5f);
  escribirPosiciones(); // Sostiene posicion, NO desenergiza los motores.
}

static void comenzarMovimiento(const int angulos[CANT_SERVOS], int tiempoMs) {
  actualizarMovimiento();
  for (int i = 0; i < CANT_SERVOS; ++i) {
    origen[i] = actual[i];
    destino[i] = angulos[i];
  }
  inicioMs = millis();
  duracionMs = (uint32_t)tiempoMs;
  enMovimiento = true;
}

static void informarPosiciones() {
  Serial.print("POS");
  for (int i = 0; i < CANT_SERVOS; ++i) {
    Serial.print(',');
    Serial.print((int)(actual[i] + 0.5f));
  }
  Serial.println();
}

static void ejecutar(const Comando &c) {
  if (botonPresionado && (c.tipo == COMANDO_SET || c.tipo == COMANDO_MOVE)) {
    Serial.println("ERR,BOTON_STOP");
    return;
  }
  switch (c.tipo) {
    case COMANDO_PING:
      Serial.println("PONG");
      break;
    case COMANDO_GET:
      actualizarMovimiento();
      informarPosiciones();
      break;
    case COMANDO_STOP:
      congelarMovimiento();
      Serial.println("OK,STOP");
      break;
    case COMANDO_SET: {
      // Mantener destinos de los demas ejes mientras se mueve uno.
      int objetivos[CANT_SERVOS];
      for (int i = 0; i < CANT_SERVOS; ++i) objetivos[i] = destino[i];
      objetivos[c.indice] = c.angulo;
      comenzarMovimiento(objetivos, DURACION_MANUAL_MS);
      Serial.println("OK,SET");
      break;
    }
    case COMANDO_MOVE:
      comenzarMovimiento(c.angulos, c.tiempo_ms);
      Serial.println("OK,MOVE");
      break;
    default:
      Serial.println("ERR,TRAMA_INVALIDA");
      break;
  }
}

// Telemetria del ambiente de la garra, sin modificar comandos de servos.
// Tramas: DHT,<temperatura_C>,<humedad_porcentaje> o DHT,ERR.
static void actualizarDht22() {
  const uint32_t ahora = millis();
  if ((uint32_t)(ahora - ultimaLecturaDhtMs) < PERIODO_DHT_MS) return;
  ultimaLecturaDhtMs = ahora;
  const float h = dht.readHumidity();
  const float t = dht.readTemperature();
  if (isnan(h) || isnan(t) || h < 0 || h > 100) {
    Serial.println("DHT,ERR");
    return;
  }
  Serial.print("DHT,");
  Serial.print(t, 1);
  Serial.print(',');
  Serial.println(h, 1);
}

static void atenderSerial() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (descartarHastaSalto) {
        Serial.println("ERR,TRAMA_LARGA");
      } else if (tramaLen != 0) {
        trama[tramaLen] = '\0';
        Comando cmd = {};
        if (interpretar_trama(trama, &cmd)) ejecutar(cmd);
        else Serial.println("ERR,TRAMA_INVALIDA");
      }
      tramaLen = 0;
      descartarHastaSalto = false;
    } else if (!descartarHastaSalto) {
      if (tramaLen < sizeof(trama) - 1) trama[tramaLen++] = c;
      else descartarHastaSalto = true;
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOTON_PARADA, INPUT_PULLUP);
  dht.begin();
  ultimaLecturaDhtMs = millis();
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  for (int i = 0; i < CANT_SERVOS; ++i) {
    servo[i].setPeriodHertz(50);
    // 500..2500 son limites INTERNOS del canal PWM, NO el recorrido ordenado.
    // Los limites operativos se aplican en escribirPosiciones() por eje.
    servo[i].attach(PIN_SERVO[i], 500, 2500);
    servo[i].writeMicroseconds(1500); // Centro: revisar espacio libre al encender.
  }
  ultimaActualizacionMs = millis();
  Serial.println("READY,BRAZO,6,PWM_CAL2");
}

void loop() {
  const bool presionado = (digitalRead(PIN_BOTON_PARADA) == LOW);
  if (presionado && !botonPresionado) {
    congelarMovimiento();
    Serial.println("OK,BOTON_STOP");
  }
  botonPresionado = presionado;

  atenderSerial();
  actualizarDht22();
  actualizarMovimiento();
  if ((uint32_t)(millis() - ultimaActualizacionMs) >= PERIODO_ACTUALIZACION_MS) {
    ultimaActualizacionMs = millis();
    escribirPosiciones();
  }
}
