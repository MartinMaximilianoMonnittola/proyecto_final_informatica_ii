#pragma once
/*
  Proyecto final Informatica 2 - protocolo ASCII en formato CSV.
  No depende de un archivo .c: el analizador esta incluido en este header.
  Cada orden termina con LF ('\n') a 115200 baudios.

  PING
  GET
  STOP
  SET,<indice 0..5>,<angulo 0..180>
  MOVE,<duracion_ms 100..10000>,<s0>,<s1>,<s2>,<s3>,<s4>,<s5>
*/
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static const int CANT_SERVOS = 6;

enum TipoComando {
  COMANDO_INVALIDO = 0,
  COMANDO_PING,
  COMANDO_GET,
  COMANDO_STOP,
  COMANDO_SET,
  COMANDO_MOVE
};

struct Comando {
  TipoComando tipo;
  int indice;
  int angulo;
  int tiempo_ms;
  int angulos[CANT_SERVOS];
};

static bool parsear_entero(const char *texto, int min, int max, int *resultado) {
  if (!texto || !*texto) return false;
  char *fin = nullptr;
  const long valor = strtol(texto, &fin, 10);
  if (fin == texto || *fin != '\0' || valor < min || valor > max) return false;
  *resultado = (int)valor;
  return true;
}

static bool interpretar_trama(const char *entrada, Comando *out) {
  if (!entrada || !out) return false;
  const size_t n = strlen(entrada);
  if (n == 0 || n > 119) return false;
  char copia[120];
  memcpy(copia, entrada, n + 1);
  char *save = nullptr;
  char *parte = strtok_r(copia, ",", &save);
  if (!parte) return false;
  Comando cmd = {};
  if (strcmp(parte, "PING") == 0) cmd.tipo = COMANDO_PING;
  else if (strcmp(parte, "GET") == 0) cmd.tipo = COMANDO_GET;
  else if (strcmp(parte, "STOP") == 0) cmd.tipo = COMANDO_STOP;
  else if (strcmp(parte, "SET") == 0) {
    cmd.tipo = COMANDO_SET;
    if (!parsear_entero(strtok_r(nullptr, ",", &save), 0, CANT_SERVOS - 1, &cmd.indice)) return false;
    if (!parsear_entero(strtok_r(nullptr, ",", &save), 0, 180, &cmd.angulo)) return false;
  } else if (strcmp(parte, "MOVE") == 0) {
    cmd.tipo = COMANDO_MOVE;
    if (!parsear_entero(strtok_r(nullptr, ",", &save), 100, 10000, &cmd.tiempo_ms)) return false;
    for (int i = 0; i < CANT_SERVOS; ++i) {
      if (!parsear_entero(strtok_r(nullptr, ",", &save), 0, 180, &cmd.angulos[i])) return false;
    }
  } else return false;
  if (strtok_r(nullptr, ",", &save) != nullptr) return false;
  *out = cmd;
  return true;
}
