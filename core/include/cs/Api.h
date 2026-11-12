#pragma once
// API de comandos en JSON para las interfaces (Android vía JNI, iOS vía Swift/ObjC++).
// Una sola puerta de entrada mantiene el contrato idéntico en ambas plataformas y se prueba en escritorio.
//   entrada: {"cmd":"addTrack","name":"Voz"}
//   salida:  {"ok":true,"id":"trk_..."}  o  {"ok":false,"error":"..."}
// Solo hilo de control. Las consultas de alta frecuencia (posición, medidores, formas de onda) tienen funciones
// directas en el puente nativo para no serializar JSON 60 veces por segundo.
#include "cs/Engine.h"
#include "cs/Session.h"

#include <string>

namespace cs
{
std::string handleCommand (Session& session, Engine& engine, const std::string& requestJson);
}
