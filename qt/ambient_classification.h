#pragma once
#include <cmath>

// Etapa 3 = indice 2 (FUERA), etapa 4 = indice 3 (DENTRO).
// Los dos promedios deben pertenecer a sus intervalos INCLUSIVOS.
namespace ambient {
inline int selectStage(double humidityAverage, double temperatureAverage,
                       double humidityMin, double humidityMax,
                       double temperatureMin, double temperatureMax) {
    const bool valid = std::isfinite(humidityAverage) && std::isfinite(temperatureAverage);
    const bool humidityInRange = humidityAverage >= humidityMin && humidityAverage <= humidityMax;
    const bool temperatureInRange = temperatureAverage >= temperatureMin && temperatureAverage <= temperatureMax;
    return (valid && humidityInRange && temperatureInRange) ? 3 : 2;
}
} // namespace ambient
