#include "../qt/ambient_classification.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main() {
    // Humedad 40-70 % y temperatura 15-30 C, ambos extremos inclusivos.
    assert(ambient::selectStage(40, 15, 40, 70, 15, 30) == 3);
    assert(ambient::selectStage(70, 30, 40, 70, 15, 30) == 3);
    assert(ambient::selectStage(55, 22, 40, 70, 15, 30) == 3);
    // Caso real reportado: HR promedio 20,5 %, T promedio 46,5 C.
    // Ambas fuera del rango: necesariamente va a la ETAPA 3 (indice 2).
    assert(ambient::selectStage(20.5, 46.5, 40, 70, 15, 30) == 2);
    assert(ambient::selectStage(40, 30, 40, 70, 15, 30) == 3);
    assert(ambient::selectStage(70, 15, 40, 70, 15, 30) == 3);
    assert(ambient::selectStage(39.9, 22, 40, 70, 15, 30) == 2);
    assert(ambient::selectStage(70.1, 22, 40, 70, 15, 30) == 2);
    assert(ambient::selectStage(55, 14.9, 40, 70, 15, 30) == 2);
    assert(ambient::selectStage(55, 30.1, 40, 70, 15, 30) == 2);
    assert(ambient::selectStage(39.9, 14.9, 40, 70, 15, 30) == 2);
    assert(ambient::selectStage(70.1, 30.1, 40, 70, 15, 30) == 2);
    assert(ambient::selectStage(70, 30.1, 40, 70, 15, 30) == 2);
    assert(ambient::selectStage(40, 14.9, 40, 70, 15, 30) == 2);
    assert(ambient::selectStage(39.9, 15, 40, 70, 15, 30) == 2);
    assert(ambient::selectStage(70.1, 30, 40, 70, 15, 30) == 2);
    // Rangos editables: se comprueba que NO esten fijos en la funcion.
    assert(ambient::selectStage(20, 10, 10, 30, 5, 12) == 3);
    assert(ambient::selectStage(31, 10, 10, 30, 5, 12) == 2);
    // No clasificar como dentro un promedio no numerico.
    assert(ambient::selectStage(std::numeric_limits<double>::quiet_NaN(), 20, 40, 70, 15, 30) == 2);
    assert(ambient::selectStage(50, std::numeric_limits<double>::infinity(), 40, 70, 15, 30) == 2);
    std::puts("20 pruebas de clasificacion ambiental: OK");
}
