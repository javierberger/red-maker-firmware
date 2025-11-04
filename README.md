# 🌡️ Red Maker IoT - Firmware ESP32

Firmware para el **Kit Maker 2.0** de monitoreo ambiental (temperatura y humedad) para la Red de Espacios Makers de Misiones, Argentina.

## 📋 Tabla de Contenidos

- [Características](#características)
- [Hardware Requerido](#hardware-requerido)
- [Instalación Rápida](#instalación-rápida)
- [Configuración del Servidor](#configuración-del-servidor)
- [Primer Uso](#primer-uso)
- [Funcionalidades](#funcionalidades)
- [Troubleshooting](#troubleshooting)
- [Consideraciones de Seguridad](#consideraciones-de-seguridad)
- [Contribuir](#contribuir)

---

## ✨ Características

- ✅ **Sensor HTU21D** - Medición de temperatura y humedad
- ✅ **Display OLED 128x64** - 4 pantallas informativas rotativas
- ✅ **LED RGB** - Indicador visual de estado del sistema
- ✅ **3 Botones multifunción** - Control sin necesidad de reprogramar
- ✅ **Portal cautivo WiFi** - Configuración fácil sin hardcodear credenciales
- ✅ **Sistema de activación único** - Un código por sede para seguridad
- ✅ **Almacenamiento persistente** - Configuración guardada en EEPROM
- ✅ **Reconexión automática** - Maneja caídas de WiFi y servidor
- ✅ **Envío periódico** - Datos cada 5 minutos automáticamente
- ✅ **Reset durante fallo WiFi** - Botón de reset funciona siempre

---

## 🔧 Hardware Requerido

### Kit Maker 2.0

| Componente | Especificación | Conexión |
|------------|----------------|----------|
| **ESP32** | Compatible con Arduino IDE | - |
| **Sensor HTU21D** | I2C temperatura/humedad | SDA=GPIO21, SCL=GPIO22 |
| **Display OLED** | SSD1306 128x64 (I2C 0x3C) | SDA=GPIO21, SCL=GPIO22 |
| **LED RGB** | NeoPixel WS2812B (1 pixel) | GPIO27 |
| **Botón Izquierdo** | INPUT_PULLUP | GPIO0 (BOOT) |
| **Botón Medio** | INPUT_PULLUP | GPIO15 |
| **Botón Derecho** | INPUT_PULLUP | GPIO13 |

### Esquema de Conexiones

```
ESP32 GPIO21 (SDA) ──┬── HTU21D SDA
                     └── OLED SDA

ESP32 GPIO22 (SCL) ──┬── HTU21D SCL
                     └── OLED SCL

ESP32 GPIO27 ────────── NeoPixel DIN

ESP32 3.3V ──────────┬── HTU21D VCC
                     └── OLED VCC

ESP32 GND ───────────┬── HTU21D GND
                     ├── OLED GND
                     └── NeoPixel GND
```

---

## 🚀 Instalación Rápida

### 1. Instalar Arduino IDE

- Descargar desde: https://www.arduino.cc/en/software
- Versión recomendada: 1.8.19 o superior / 2.x

### 2. Instalar Soporte para ESP32

**Arduino IDE:**
```
1. Archivo → Preferencias
2. URLs Adicionales de Gestor de Tarjetas:
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
3. Herramientas → Placa → Gestor de Tarjetas
4. Buscar "esp32" by Espressif Systems
5. Instalar última versión disponible (recomendado: 3.2.2 o superior)
   ✅ Compatible con versiones 2.0.17 y superiores
   ✅ Probado con versión 3.2.2
```

### ⚙️ Versión de Core ESP32 Recomendada

Este código funciona con **ESP32 Core 2.0.17 y superiores** (probado con 3.2.2)

**Recomendación:** Instalar la última versión disponible desde el Gestor de Tarjetas de Arduino IDE.

### 3. Instalar Librerías

**Desde el Gestor de Librerías** (Herramientas → Administrar Bibliotecas):

| Librería | Autor |
|----------|-------|
| `Adafruit HTU21DF Library` | Adafruit |
| `Adafruit SSD1306` | Adafruit |
| `Adafruit GFX Library` | Adafruit |
| `Adafruit NeoPixel` | Adafruit |

**Librerías incluidas en ESP32 Core** (no instalar):
- WiFi, WebServer, HTTPClient, DNSServer, ESPmDNS, EEPROM, Wire

### 4. Descargar el Código

**Opción A: Clonar repositorio**
```bash
git clone https://github.com/javierberger/red-maker-firmware.git
cd red-maker-firmware/maker_iot_sensor
```

**Opción B: Descargar ZIP**
- Ir a: https://github.com/javierberger/red-maker-firmware
- Clic en: **Code** → **Download ZIP**
- Descomprimir y abrir `maker_iot_sensor/maker_iot_sensor.ino`

---

## ⚙️ Configuración del Servidor

### ⚠️ IMPORTANTE - Configuración Obligatoria

Antes de compilar, **DEBES** configurar las URLs de tu servidor backend:

**Editar:** `maker_iot_sensor.ino` (líneas 70-71)

```cpp
// ============================================
// ⚠️⚠️⚠️ CONFIGURACIÓN OBLIGATORIA ⚠️⚠️⚠️
// DEBE reemplazar con la URL de su servidor ANTES de compilar
// ============================================
const char* BACKEND_ACTIVATE_URL = "";  // ⚠️ COMPLETAR AQUÍ
const char* BACKEND_UPDATES_URL = "";   // ⚠️ COMPLETAR AQUÍ
```

**Ejemplo de configuración correcta:**

```cpp
const char* BACKEND_ACTIVATE_URL = "https://mi-servidor.ngrok-free.app/api/activate";
const char* BACKEND_UPDATES_URL = "https://mi-servidor.ngrok-free.app/api/updates";
```

### ✅ Validación Automática

Si olvidas configurar las URLs:
- ❌ El ESP32 **NO arrancará**
- 🖥️ Display mostrará: `"ERROR CONFIG - URLs no configuradas"`
- 🔴 LED parpadeará en ROJO continuamente
- 📟 Serial Monitor mostrará instrucciones claras

---

## 📱 Primer Uso

### Paso 1: Compilar y Cargar

```
1. Abrir maker_iot_sensor.ino en Arduino IDE
2. ⚠️ Configurar URLs del servidor (líneas 70-71)
3. Herramientas → Placa → ESP32 Dev Module
4. Herramientas → Puerto → Seleccionar puerto COM
5. Verificar/Compilar (Ctrl+R)
6. Subir (Ctrl+U)
7. Abrir Monitor Serial (115200 baud)
```

### Paso 2: Conectar al WiFi del ESP32

Al iniciar por primera vez, el ESP32 crea una red WiFi:

```
SSID: REM-SETUP-XXXX
Contraseña: (ninguna - red abierta)
```

**Desde tu celular/computadora:**
1. Buscar red WiFi `REM-SETUP-XXXX`
2. Conectarse
3. El portal cautivo se abre automáticamente
4. Si no abre, ir a: `http://192.168.4.1` o `http://maker.local`

### Paso 3: Configurar Dispositivo

En el formulario web que se abre:

| Campo | Descripción | Ejemplo |
|-------|-------------|---------|
| **WiFi (SSID)** | Nombre de tu red WiFi local | `MiWiFi2024` |
| **Contraseña WiFi** | Clave de tu WiFi | `MiClave123` |
| **Código de Activación** | Código único proporcionado | `REM-MONTECARLO-14` |

Hacer clic en **"Activar Dispositivo"**

### Paso 4: Activación Automática

El ESP32:
1. ✅ Se conecta a tu WiFi local
2. ✅ Valida el código con el servidor backend
3. ✅ Recibe credenciales (API Key, nombre de sede)
4. ✅ Guarda todo en EEPROM
5. ✅ Se reinicia automáticamente

### Paso 5: Verificar Funcionamiento

**Display OLED:**
```
Red Maker Misiones
[Nombre de tu Sede]
24.5 °C
67 %
```

**LED RGB:**
- 🟢 **Verde**: Todo funcionando correctamente
- 🟡 **Naranja**: WiFi OK, servidor no responde
- 🔴 **Rojo**: Sin conexión WiFi

**Serial Monitor:**
```
[OK] Sistema listo
[OK] WiFi conectado
[OK] Datos enviados exitosamente
```

---

## 🎮 Funcionalidades

### Estados del LED RGB

| Color | Significado |
|-------|-------------|
| 🟣 **PÚRPURA** | Iniciando sistema |
| 🔵 **AZUL** | Modo configuración (AP activo) |
| 🔴 **ROJO** | Sin conexión WiFi |
| 🟠 **NARANJA** | WiFi OK, servidor no responde |
| 🟢 **VERDE** | Todo funcionando correctamente |
| 🟡 **AMARILLO** (parpadeante) | Botón presionado (hold) |

### Botones y Controles

#### Botón IZQUIERDO (GPIO 0)
- **Click corto**: Cambiar pantalla del display
- **Hold 3 segundos**: Mostrar información de debug (MAC, IP, RAM)

#### Botón MEDIO (GPIO 15) 🆕
- **Hold 3 segundos**: Reiniciar ESP32
- **Hold 10 segundos**: Reset completo a modo AP (borra configuración)
  - ⚠️ **Funciona SIEMPRE**, incluso si WiFi está fallando
  - LED parpadea rojo rápido al detectar hold
  - Display muestra "RESET - Cancelando..."

#### Botón DERECHO (GPIO 13)
- **Hold 3 segundos**: Envío manual de datos (sin esperar los 5 minutos)

### Pantallas del Display

Rotar con **botón izquierdo** (click corto):

**Pantalla 1/4 - Temperatura y Humedad**
```
Red Maker Misiones
[Nombre Sede]
24.5 °C
67 %
```

**Pantalla 2/4 - WiFi Info**
```
WiFi Info [2/4]
====================
Red: MiWiFi2024
Senal: Buena
IP: 192.168.1.45
```

**Pantalla 3/4 - Sincronización**
```
Sincronizacion [3/4]
====================
Ultima sinc:
Hace 2 min

Proximo envio:
En 3 min
```

**Pantalla 4/4 - Estadísticas**
```
Estadisticas [4/4]
====================
Uptime: 5h 23m
RAM libre: 245 KB
Estado: OK
```

---

## 🔧 Troubleshooting

### Error: URLs del servidor vacías

**Síntoma:**
```
╔════════════════════════════════════════════╗
║  ⚠️  ERROR DE CONFIGURACIÓN  ⚠️           ║
╚════════════════════════════════════════════╝
Las URLs del servidor están vacías.
```

**Solución:**
1. Editar `maker_iot_sensor.ino`
2. Configurar líneas 70-71 con las URLs del servidor
3. Re-compilar y subir

---

### 🆕 Error de activación con código -1 (ERROR SSL/Red)

**Síntoma:**
```
[ERROR] Fallo en activación. HTTP code: -1
ERROR SSL/Red - Ver Serial
```

**Causa:**
- Core ESP32 desactualizado con problemas SSL/TLS

**Solución:**
1. Arduino IDE → Herramientas → Placa → Gestor de Tarjetas
2. Buscar "esp32" by Espressif Systems
3. Actualizar a versión >= 2.0.0
4. Reiniciar Arduino IDE
5. Re-compilar y subir código

**Verificación:**
El código detecta automáticamente este error y muestra:
```
[DEBUG] Error de conexión/SSL. Posibles causas:
  1. Core ESP32 desactualizado (recomendado: >=2.0.0)
  2. Servidor no accesible
  3. URL mal configurada
```

---

### 🆕 WiFi no conecta - Reset no funciona

**Síntoma anterior (SOLUCIONADO):**
- LED rojo permanente
- ESP32 bloqueado reintentando WiFi
- Botón medio no responde

**Solución implementada:**
✅ **Ahora funciona siempre**
1. Mantener botón medio 10 segundos durante fallo WiFi
2. LED parpadea rojo rápido (feedback visual)
3. Display muestra "RESET - Cancelando..."
4. ESP32 borra EEPROM y reinicia en modo AP

**Notas:**
- El reset funciona incluso durante el bucle de reconexión WiFi
- Los reintentos WiFi ahora esperan 30 segundos entre intentos
- Los botones responden mientras espera reconexión

---

### Sensor HTU21D no encontrado

**Síntoma:**
```
[ERROR] No se pudo encontrar el sensor HTU21D
```

**Solución:**
1. Verificar conexiones I2C (SDA=GPIO21, SCL=GPIO22)
2. Verificar alimentación 3.3V
3. Ejecutar I2C Scanner:

```cpp
#include <Wire.h>
void setup() {
  Wire.begin();
  Serial.begin(115200);
  Serial.println("Scanning I2C...");
  for(byte i = 0; i < 127; i++) {
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Device found at 0x%02X\n", i);
    }
  }
}
void loop() {}
```

Debería encontrar:
- `0x3C` → Display OLED
- `0x40` → Sensor HTU21D

---

### Código de activación inválido (HTTP 404/422)

**Síntoma:**
```
ERROR 404 - Codigo invalido
ERROR 422 - Codigo usado
```

**Causa:**
- Código mal ingresado (case-sensitive)
- Código ya usado anteriormente
- Código no generado en el servidor

**Solución:**
1. Verificar el código (respetar mayúsculas/minúsculas)
2. Solicitar un nuevo código al administrador
3. Intentar activación nuevamente

---

---

## 🤝 Contribuir

¡Las contribuciones son bienvenidas!

### Reportar Bugs

Abrir un Issue en GitHub con:
- Descripción del problema
- Pasos para reproducir
- Versión de Core ESP32
- Serial Monitor output
- Fotos/capturas si aplica

### Sugerir Mejoras

Abrir un Issue con:
- Descripción de la funcionalidad
- Caso de uso
- Beneficios esperados

### Pull Requests

1. Fork del proyecto
2. Crear branch (`git checkout -b feature/nueva-funcionalidad`)
3. Commit cambios (`git commit -am 'Agregar nueva funcionalidad'`)
4. Push al branch (`git push origin feature/nueva-funcionalidad`)
5. Abrir Pull Request

---
## 📄 Licencia

Este proyecto es parte de la **Red de Espacios Makers de Misiones, Argentina**.

Software desarrollado con fines educativos.

---

## 🎓 Créditos

Desarrollado por **Javier Berger** para la **Red de Espacios Makers de Misiones**

**Hardware:** Kit Maker 2.0 - FanIOT
**Software:** Firmware ESP32 con Arduino Framework

---

**Versión:** 1.0.0
**Última actualización:** Noviembre 2025