#include "../firmware/BrazoArduino/protocolo.h"
#include <cassert>
#include <cstdio>
int main() {
    Comando c = {};
    assert(interpretar_trama("PING", &c) && c.tipo == COMANDO_PING);
    assert(interpretar_trama("GET", &c) && c.tipo == COMANDO_GET);
    assert(interpretar_trama("STOP", &c) && c.tipo == COMANDO_STOP);
    assert(interpretar_trama("SET,0,90", &c) && c.tipo == COMANDO_SET && c.angulo == 90);
    assert(interpretar_trama("MOVE,1000,80,90,90,95,100,90", &c)
           && c.tipo == COMANDO_MOVE && c.angulos[3] == 95);
    assert(!interpretar_trama("SET,6,90", &c));
    assert(!interpretar_trama("MOVE,50,80,90,90,95,100,90", &c));
    assert(!interpretar_trama("MOVE,1000,80,90,90,95,100,181", &c));
    assert(!interpretar_trama("MOVE,1000,80,90,90", &c));
    assert(!interpretar_trama("PING,EXTRA", &c));
    std::puts("10 pruebas de protocolo: OK");
}
