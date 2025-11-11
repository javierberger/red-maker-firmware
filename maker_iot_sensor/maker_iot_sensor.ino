/*
 * Red Maker IoT - ESP32 Sensor Node (Kit Maker 2.0)
 *
 * Características:
 * - Sensor HTU21D (I2C) para temperatura y humedad
 * - Display OLED 128x64 con 4 pantallas rotativas
 * - LED RGB (NeoPixel) para indicar estado de conexión
 * - 3 botones multifunción (click y hold)
 * - Modo AP con portal cautivo + mDNS (maker.local)
 * - Sistema de activación con código único por sede
 *
 * Estados LED:
 * - PÚRPURA: Iniciando sistema
 * - AZUL: Modo configuración (AP)
 * - ROJO: Sin conexión WiFi
 * - NARANJA: WiFi OK pero servidor no responde
 * - VERDE: Todo funcionando correctamente
 * - AMARILLO (parpadeante): Presionando botón (hold)
 *
 * Botones:
 * - IZQUIERDO (GPIO 0): Click → Cambiar pantalla | Hold 3s → Mostrar debug info
 * - MEDIO (GPIO 15): Hold 3s → Reiniciar ESP32 | Hold 10s → Reset completo (funciona siempre, incluso durante fallo WiFi)
 * - DERECHO (GPIO 13): Hold 3s → Envío manual de datos
 *
 * Pantallas (rotar con botón izquierdo):
 * 1. Temperatura/Humedad - Datos del sensor en tiempo real
 * 2. WiFi Info - SSID, señal, IP local
 * 3. Sincronización - Última/próxima sincronización
 * 4. Estadísticas - Uptime, RAM libre, estado
 *
 * Primer arranque:
 * 1. ESP32 crea WiFi "REM-SETUP-XXXX"
 * 2. Conectarse (se abre automáticamente el portal cautivo)
 * 3. Opcionalmente visitar: http://maker.local o http://192.168.4.1
 * 4. Ingresar WiFi local + código de activación
 * 5. Credenciales se guardan en EEPROM
 *
 * Dependencias (instalar desde Gestor de Librerías):
 * - Adafruit HTU21D-F Library
 * - Adafruit SSD1306
 * - Adafruit GFX Library
 * - Adafruit NeoPixel
 * - DNSServer, ESPmDNS (incluidas en ESP32)
 * - EEPROM (incluida en ESP32)
 * - WiFi, WebServer, HTTPClient (incluidas en ESP32)
 *
 * ⚠️ IMPORTANTE - Core ESP32:
 * - Versión recomendada: ESP32 Core 2.0.x (2.0.0 a 2.0.17)
 * - Si tenés error de activación con código -1, actualiza el core:
 *   Arduino IDE → Herramientas → Placa → Gestor de Tarjetas → ESP32
 *   Buscar "esp32 by Espressif Systems" → Seleccionar versión 2.0.17
 */

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_HTU21DF.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// ============================================
// ⚠️⚠️⚠️ CONFIGURACIÓN OBLIGATORIA ⚠️⚠️⚠️
// DEBE reemplazar con la URL de su servidor ANTES de compilar
// Editar líneas 70-71 con la URL de tu servidor
// ============================================
const char* BACKEND_ACTIVATE_URL = "";  // ⚠️ COMPLETAR AQUÍ
const char* BACKEND_UPDATES_URL = "";   // ⚠️ COMPLETAR AQUÍ

// ============================================
// CONFIGURACIÓN DE HARDWARE
// ============================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

#define NEOPIXEL_PIN 27      // LED RGB en GPIO27
#define NEOPIXEL_COUNT 1
#define BUTTON_LEFT_PIN 0    // Botón IZQUIERDO - BOOT (GPIO0)
#define BUTTON_MIDDLE_PIN 15 // Botón MEDIO (GPIO15)
#define BUTTON_RIGHT_PIN 13  // Botón DERECHO (GPIO13)

#define EEPROM_SIZE 512
#define EEPROM_MAGIC 0xABCD  // Para validar si hay datos guardados

// Tiempos para detección de botones (en milisegundos)
#define DEBOUNCE_DELAY 50
#define HOLD_TIME_SHORT 3000   // 3 segundos para acciones normales
#define HOLD_TIME_RESET 10000  // 10 segundos para reset completo (botón medio)

// Estructura para guardar en EEPROM
struct Config {
  uint16_t magic;
  char wifi_ssid[32];
  char wifi_password[64];
  char sede_id[50];
  char sede_nombre[100];
  char api_key[65];
  char api_endpoint[128];
};

// ============================================
// OBJETOS GLOBALES
// ============================================
Adafruit_HTU21DF htu = Adafruit_HTU21DF();
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

Config config;
bool isConfigured = false;
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 300000;  // 5 minutos (300000ms)

// Variables de estado
float lastTemperatura = 0.0;
float lastHumedad = 0.0;
enum ConnectionState { STATE_NO_WIFI, STATE_NO_ENDPOINT, STATE_OK };
ConnectionState currentState = STATE_NO_WIFI;
unsigned long lastWiFiAttempt = 0;
const unsigned long WIFI_RETRY_INTERVAL = 30000;  // Reintentar WiFi cada 30 segundos

// Variables para manejo de botones
struct ButtonState {
  bool lastState;
  bool currentState;
  unsigned long pressStartTime;
  unsigned long lastDebounceTime;
};

ButtonState btnLeft = {HIGH, HIGH, 0, 0};
ButtonState btnMiddle = {HIGH, HIGH, 0, 0};
ButtonState btnRight = {HIGH, HIGH, 0, 0};

// Pantalla actual (para rotación con botón izquierdo)
int currentScreen = 0;
const int TOTAL_SCREENS = 4;
unsigned long lastScreenChange = 0;
const unsigned long SCREEN_DISPLAY_TIME = 5000; // 5 segundos mostrando pantalla

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=========================================");
  Serial.println("Red Maker IoT - ESP32 Sensor Node");
  Serial.println("Kit Maker 2.0 - FanIOT");
  Serial.println("=========================================\n");

  // Validar que las URLs del servidor estén configuradas
  if (strlen(BACKEND_ACTIVATE_URL) == 0 || strlen(BACKEND_UPDATES_URL) == 0) {
    Serial.println("\n\n");
    Serial.println("╔════════════════════════════════════════════╗");
    Serial.println("║  ⚠️  ERROR DE CONFIGURACIÓN  ⚠️           ║");
    Serial.println("╚════════════════════════════════════════════╝");
    Serial.println("");
    Serial.println("Las URLs del servidor están vacías.");
    Serial.println("DEBE editar el archivo .ino y configurar:");
    Serial.println("");
    Serial.println("  const char* BACKEND_ACTIVATE_URL = \"https://...\"");
    Serial.println("  const char* BACKEND_UPDATES_URL = \"https://...\"");
    Serial.println("");
    Serial.println("No se puede continuar sin esta configuración.");
    Serial.println("════════════════════════════════════════════");

    // Inicializar display para mostrar error
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("ERROR CONFIG");
      display.println("====================");
      display.println("");
      display.println("URLs del servidor");
      display.println("no configuradas");
      display.println("");
      display.println("Editar archivo .ino");
      display.println("lineas 70-71");
      display.display();
    }

    // Inicializar LED y parpadear rojo
    pixels.begin();
    while (1) {
      pixels.setPixelColor(0, pixels.Color(255, 0, 0));
      pixels.show();
      delay(500);
      pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      pixels.show();
      delay(500);
    }
  }

  // Inicializar EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // Inicializar NeoPixel
  pixels.begin();
  setLEDColor(255, 0, 255);  // Púrpura = iniciando
  pixels.show();

  // Inicializar display OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[ERROR] No se pudo inicializar el display OLED");
  } else {
    Serial.println("[OK] Display OLED inicializado");
    displayMessage("Red Maker", "Iniciando...");
  }

  // Inicializar sensor HTU21D
  if (!htu.begin()) {
    Serial.println("[ERROR] No se pudo encontrar el sensor HTU21D");
    displayMessage("ERROR", "Sensor HTU21D\nno encontrado");
    while (1) {
      setLEDColor(255, 0, 0);  // Rojo parpadeante
      pixels.show();
      delay(500);
      setLEDColor(0, 0, 0);
      pixels.show();
      delay(500);
    }
  }
  Serial.println("[OK] Sensor HTU21D inicializado");

  // Inicializar botones
  pinMode(BUTTON_LEFT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_MIDDLE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT_PIN, INPUT_PULLUP);
  Serial.println("[OK] Botones inicializados (GPIO 0, 15, 13)");

  // Cargar configuración desde EEPROM
  loadConfig();

  if (isConfigured) {
    Serial.println("[INFO] Dispositivo configurado. Modo normal.");
    Serial.print("[INFO] Sede: ");
    Serial.println(config.sede_nombre);
    String sedeNormalizada = normalizarTexto(config.sede_nombre);
    displayMessage("Sede:", sedeNormalizada.c_str());
    delay(2000);

    // Conectar a WiFi
    connectWiFi();

    // Enviar primer dato
    sendSensorData();
  } else {
    Serial.println("[INFO] Dispositivo NO configurado. Iniciando modo AP...");
    startAPMode();
  }

  Serial.println("\n[OK] Sistema listo\n");
}

// ============================================
// LOOP PRINCIPAL
// ============================================
void loop() {
  // Si está en modo AP, procesar peticiones web y DNS
  if (!isConfigured) {
    dnsServer.processNextRequest();
    server.handleClient();
    delay(10);
    return;
  }

  // Procesar botones
  handleButtons();

  // Verificar conexión WiFi (reintentar solo cada 30 segundos)
  if (WiFi.status() != WL_CONNECTED) {
    currentState = STATE_NO_WIFI;
    setLEDColor(255, 0, 0);  // ROJO
    pixels.show();

    // Solo reintentar si pasaron 30 segundos desde el último intento
    if (millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
      Serial.println("[ERROR] WiFi desconectado. Reconectando...");
      connectWiFi();
      lastWiFiAttempt = millis();
    }
  } else {
    // Si está conectado, resetear el timer
    lastWiFiAttempt = millis();
  }

  // Volver a pantalla principal después de 5 segundos
  if (currentScreen != 0 && millis() - lastScreenChange >= SCREEN_DISPLAY_TIME) {
    Serial.println("[SCREEN] Timeout - Volviendo a pantalla principal");
    currentScreen = 0;
    updateDisplay();
  }

  // Leer sensores y actualizar display cada 2 segundos (solo en pantalla principal)
  static unsigned long lastDisplayUpdate = 0;
  if (currentScreen == 0 && millis() - lastDisplayUpdate >= 2000) {
    lastDisplayUpdate = millis();
    updateSensorReadings();
  }

  // Enviar datos automáticamente cada SEND_INTERVAL
  if (millis() - lastSendTime >= SEND_INTERVAL) {
    sendSensorData();
    lastSendTime = millis();
  }

  delay(10);
}

// ============================================
// CONFIGURACIÓN Y EEPROM
// ============================================
void loadConfig() {
  EEPROM.get(0, config);

  if (config.magic == EEPROM_MAGIC) {
    isConfigured = true;
    Serial.println("[EEPROM] Configuración cargada correctamente");
  } else {
    Serial.println("[EEPROM] No hay configuración guardada");
    isConfigured = false;
  }
}

void saveConfig() {
  config.magic = EEPROM_MAGIC;
  EEPROM.put(0, config);
  EEPROM.commit();
  Serial.println("[EEPROM] Configuración guardada");
}

void resetConfig() {
  Serial.println("\n[RESET] Borrando configuración de EEPROM...");

  // Borrar EEPROM
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();

  Serial.println("[RESET] EEPROM borrada");
  Serial.println("[RESET] Reiniciando en modo AP...");

  displayMessage("RESET", "Reiniciando...");
  delay(2000);

  ESP.restart();
}

// ============================================
// MANEJO DE BOTONES
// ============================================
void handleButtons() {
  handleButton(BUTTON_LEFT_PIN, btnLeft, onButtonLeftClick, onButtonLeftHold);
  handleButton(BUTTON_MIDDLE_PIN, btnMiddle, nullptr, onButtonMiddleHold);
  handleButton(BUTTON_RIGHT_PIN, btnRight, nullptr, onButtonRightHold);
}

void handleButton(int pin, ButtonState &btn, void (*onClickCallback)(), void (*onHoldCallback)()) {
  int reading = digitalRead(pin);

  // Detectar cambio de estado con debounce
  if (reading != btn.lastState) {
    btn.lastDebounceTime = millis();
  }

  if ((millis() - btn.lastDebounceTime) > DEBOUNCE_DELAY) {
    // El estado es estable
    if (reading != btn.currentState) {
      btn.currentState = reading;

      // Botón presionado (LOW porque es INPUT_PULLUP)
      if (btn.currentState == LOW) {
        btn.pressStartTime = millis();
      }
      // Botón liberado
      else {
        unsigned long pressDuration = millis() - btn.pressStartTime;

        // Determinar si fue click (menos de 1 segundo) o hold (ignorado aquí)
        if (pressDuration < 1000) {
          // Click corto - ejecutar solo si tiene callback de click
          if (onClickCallback != nullptr) {
            onClickCallback();
          }
        }
        // Si fue hold, ya se ejecutó en el bloque de abajo
      }
    }

    // Mientras está presionado, verificar si llegó al tiempo de hold
    if (btn.currentState == LOW && onHoldCallback != nullptr) {
      unsigned long pressDuration = millis() - btn.pressStartTime;

      // Ejecutar callback cuando se alcanza el tiempo mínimo
      if (pressDuration >= HOLD_TIME_SHORT) {
        onHoldCallback();
        // El callback decide si bloquear o no según el tiempo
      }

      // Feedback visual durante hold
      if (pressDuration >= 1000 && pressDuration < HOLD_TIME_RESET) {
        // Amarillo parpadeante para hold 3s (parpadeo lento cada 500ms)
        if ((pressDuration % 500) < 250) {
          setLEDColor(255, 255, 0); // Amarillo
          pixels.show();
        }
      } else if (pressDuration >= HOLD_TIME_RESET) {
        // Rojo parpadeante para hold 10s (parpadeo rápido cada 250ms)
        if ((pressDuration % 250) < 125) {
          setLEDColor(255, 0, 0); // Rojo
          pixels.show();
        }
      }
    }
  }

  btn.lastState = reading;
}

// ============================================
// Callbacks de botones
// ============================================

// BOTÓN IZQUIERDO: Información/Visualización
void onButtonLeftClick() {
  Serial.println("[BTN-LEFT] Click - Cambiando pantalla");
  currentScreen = (currentScreen + 1) % TOTAL_SCREENS;
  lastScreenChange = millis(); // Guardar tiempo del cambio
  updateDisplay();

  // Feedback LED verde rápido
  setLEDColor(0, 255, 0);
  pixels.show();
  delay(100);
  restoreLEDState();
}

void onButtonLeftHold() {
  static bool leftHoldExecuted = false;
  unsigned long pressDuration = millis() - btnLeft.pressStartTime;

  // Hold 3 segundos: Mostrar debug info
  if (pressDuration >= HOLD_TIME_SHORT && !leftHoldExecuted) {
    Serial.println("[BTN-LEFT] Hold 3s - Mostrando debug info");
    displayDebugInfo();
    leftHoldExecuted = true;
    btnLeft.currentState = HIGH; // Evitar múltiples llamadas

    // Restaurar pantalla después de 3 segundos
    delay(3000);
    updateDisplay();
    restoreLEDState();
  }

  // Reset flag cuando se suelta el botón
  if (digitalRead(BUTTON_LEFT_PIN) == HIGH) {
    leftHoldExecuted = false;
  }
}

// BOTÓN MEDIO: Reinicios
void onButtonMiddleHold() {
  static bool middleHold3sExecuted = false;
  static bool middleHold10sExecuted = false;
  unsigned long pressDuration = millis() - btnMiddle.pressStartTime;

  // Hold 3-9.9 segundos: Reiniciar ESP32
  if (pressDuration >= HOLD_TIME_SHORT && pressDuration < HOLD_TIME_RESET && !middleHold3sExecuted) {
    Serial.println("[BTN-MIDDLE] Hold 3s - Reiniciando ESP32");
    displayMessage("Reiniciando", "ESP32...");
    middleHold3sExecuted = true;

    // Esperar a que suelte el botón o llegue a 10s
    while (digitalRead(BUTTON_MIDDLE_PIN) == LOW && pressDuration < HOLD_TIME_RESET) {
      pressDuration = millis() - btnMiddle.pressStartTime;
      delay(10);
    }

    // Si soltó antes de 10s, reiniciar
    if (pressDuration < HOLD_TIME_RESET) {
      delay(1000);
      ESP.restart();
    }
  }

  // Hold 10+ segundos: Reset completo a AP
  if (pressDuration >= HOLD_TIME_RESET && !middleHold10sExecuted) {
    Serial.println("[BTN-MIDDLE] Hold 10s - Reset completo a modo AP");
    displayMessage("RESET TOTAL", "Borrando...");
    middleHold10sExecuted = true;
    btnMiddle.currentState = HIGH; // Evitar múltiples llamadas

    delay(1000);
    resetConfig();
  }

  // Reset flags cuando se suelta el botón
  if (digitalRead(BUTTON_MIDDLE_PIN) == HIGH) {
    middleHold3sExecuted = false;
    middleHold10sExecuted = false;
  }
}

// BOTÓN DERECHO: Envío manual
void onButtonRightHold() {
  static bool rightHoldExecuted = false;
  unsigned long pressDuration = millis() - btnRight.pressStartTime;

  // Hold 3 segundos: Envío manual de datos
  if (pressDuration >= HOLD_TIME_SHORT && !rightHoldExecuted) {
    Serial.println("[BTN-RIGHT] Hold 3s - Enviando datos manualmente");
    displayMessage("Manual", "Enviando...");
    rightHoldExecuted = true;
    btnRight.currentState = HIGH; // Evitar múltiples llamadas

    delay(500);
    sendSensorData();

    // Volver a pantalla actual
    delay(1000);
    updateDisplay();
    restoreLEDState();
  }

  // Reset flag cuando se suelta el botón
  if (digitalRead(BUTTON_RIGHT_PIN) == HIGH) {
    rightHoldExecuted = false;
  }
}

// ============================================
// UTILIDADES
// ============================================
String getMacAddressString() {
  uint64_t mac = ESP.getEfuseMac();
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          (uint8_t)(mac >> 40), (uint8_t)(mac >> 32),
          (uint8_t)(mac >> 24), (uint8_t)(mac >> 16),
          (uint8_t)(mac >> 8), (uint8_t)mac);
  return String(macStr);
}

// ============================================
// MODO AP (Access Point)
// ============================================
void startAPMode() {
  // Generar SSID único usando los últimos 4 dígitos del MAC
  uint64_t mac = ESP.getEfuseMac();
  char macStr[5];
  sprintf(macStr, "%04X", (uint16_t)(mac & 0xFFFF));
  String apSSID = "REM-SETUP-" + String(macStr);

  Serial.println("[AP] Creando punto de acceso: " + apSSID);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID.c_str());

  IPAddress IP = WiFi.softAPIP();
  Serial.println("[AP] IP del portal: " + IP.toString());

  // Iniciar mDNS para acceder con maker.local
  if (MDNS.begin("maker")) {
    Serial.println("[mDNS] Respondiendo en: http://maker.local");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("[mDNS] Error al iniciar mDNS");
  }

  // Iniciar DNS Server para portal cautivo
  dnsServer.start(DNS_PORT, "*", IP);
  Serial.println("[AP] DNS Server iniciado (portal cautivo activo)");

  setLEDColor(0, 0, 255);  // AZUL = modo configuración
  pixels.show();

  // Mostrar mensaje en display
  displayAPInfo(apSSID);

  // Configurar rutas del servidor web
  server.on("/", HTTP_GET, handleRoot);
  server.on("/activate", HTTP_POST, handleActivate);
  server.on("/generate_204", HTTP_GET, handleRoot);  // Android captive portal
  server.on("/fwlink", HTTP_GET, handleRoot);        // Microsoft captive portal
  server.onNotFound(handleRoot);  // Captive portal - redirigir todo a /

  server.begin();
  Serial.println("[AP] Servidor web iniciado");
  Serial.println("[AP] Portal cautivo: se abre automáticamente al conectar");
  Serial.println("[AP] También disponible en: http://maker.local o http://192.168.4.1");
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Red Maker - Configuración</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; background: #1e3a5f; color: white; padding: 20px; margin: 0; }";
  html += ".container { max-width: 400px; margin: 0 auto; background: #2c5282; padding: 30px; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }";
  html += "h1 { color: #fbbf24; text-align: center; margin-bottom: 10px; }";
  html += ".subtitle { text-align: center; color: #93c5fd; font-size: 14px; margin-bottom: 30px; }";
  html += "label { display: block; margin-top: 15px; font-weight: bold; color: #93c5fd; }";
  html += "input { width: 100%; padding: 10px; margin-top: 5px; border: 2px solid #3b82f6; border-radius: 5px; box-sizing: border-box; font-size: 16px; }";
  html += "button { width: 100%; padding: 12px; margin-top: 20px; background: #10b981; color: white; border: none; border-radius: 5px; font-size: 16px; font-weight: bold; cursor: pointer; }";
  html += "button:hover { background: #059669; }";
  html += ".info { background: #1e40af; padding: 15px; border-radius: 5px; margin-top: 20px; font-size: 13px; line-height: 1.6; }";
  html += ".mac { color: #fbbf24; font-family: monospace; font-weight: bold; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>Red Maker Misiones</h1>";
  html += "<p class='subtitle'>Configuración Inicial</p>";
  html += "<form action='/activate' method='POST'>";
  html += "<label>WiFi (SSID)</label>";
  html += "<input type='text' name='ssid' required placeholder='Nombre de tu red WiFi'>";
  html += "<label>Contraseña WiFi</label>";
  html += "<input type='password' name='password' required placeholder='Clave de tu WiFi'>";
  html += "<label>Código de Activación</label>";
  html += "<input type='text' name='code' required placeholder='REM-XXXXXX-2025-XXXX' style='text-transform: uppercase;'>";
  html += "<button type='submit'>Activar Dispositivo</button>";
  html += "</form>";
  html += "<div class='info'>";
  html += "<strong>Instrucciones:</strong><br>";
  html += "1. Ingresa los datos de tu WiFi local<br>";
  html += "2. Ingresa el código de activación que te entregaron<br>";
  html += "3. El dispositivo se configurará automáticamente<br><br>";
  html += "<strong>MAC Address:</strong><br>";
  html += "<span class='mac'>" + getMacAddressString() + "</span>";
  html += "</div>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleActivate() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String code = server.arg("code");

  Serial.println("[ACTIVATE] Intentando activar dispositivo...");
  Serial.print("[ACTIVATE] SSID: ");
  Serial.println(ssid);
  Serial.print("[ACTIVATE] Código: ");
  Serial.println(code);

  displayMessage("Activando...", code.c_str());

  // Conectar a WiFi del usuario
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[ERROR] No se pudo conectar al WiFi");

    // Mostrar error en display
    displayMessage("ERROR WiFi", "Verificar SSID");
    setLEDColor(255, 0, 0);  // ROJO
    pixels.show();
    delay(3000);

    String errorHtml = "<html><body style='font-family:Arial; text-align:center; padding:50px;'>";
    errorHtml += "<h2 style='color:red;'>Error de WiFi</h2>";
    errorHtml += "<p>No se pudo conectar a la red WiFi.<br>Verifica el nombre y la contraseña.</p>";
    errorHtml += "<a href='/' style='color:blue;'>Volver a intentar</a>";
    errorHtml += "</body></html>";
    server.send(400, "text/html", errorHtml);

    // Volver a modo AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WiFi.softAPSSID().c_str());
    displayAPInfo(WiFi.softAPSSID());
    setLEDColor(0, 0, 255);  // AZUL
    pixels.show();
    return;
  }

  Serial.println("\n[OK] Conectado a WiFi");

  // Validar código de activación con el backend
  HTTPClient http;
  String activateURL = BACKEND_ACTIVATE_URL;

  http.begin(activateURL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("ngrok-skip-browser-warning", "true");

  String macAddress = getMacAddressString();
  String postData = "code=" + code + "&mac_address=" + macAddress;

  int httpCode = http.POST(postData);

  if (httpCode == 200) {
    String response = http.getString();
    Serial.print("[ACTIVATE] Respuesta del servidor: ");
    Serial.println(response);

    // Parsear respuesta JSON (simple parsing manual)
    // Formato esperado: {"success":true,"sede_id":"xxx","sede_nombre":"xxx","api_key":"xxx"}
    int sedeIdStart = response.indexOf("\"sede_id\":\"") + 11;
    int sedeIdEnd = response.indexOf("\"", sedeIdStart);
    String sedeId = response.substring(sedeIdStart, sedeIdEnd);

    int sedeNombreStart = response.indexOf("\"sede_nombre\":\"") + 15;
    int sedeNombreEnd = response.indexOf("\"", sedeNombreStart);
    String sedeNombre = response.substring(sedeNombreStart, sedeNombreEnd);

    int apiKeyStart = response.indexOf("\"api_key\":\"") + 11;
    int apiKeyEnd = response.indexOf("\"", apiKeyStart);
    String apiKey = response.substring(apiKeyStart, apiKeyEnd);

    Serial.println("[OK] Activación exitosa!");
    Serial.print("[OK] Sede: ");
    Serial.println(sedeNombre);

    // Guardar en EEPROM
    ssid.toCharArray(config.wifi_ssid, 32);
    password.toCharArray(config.wifi_password, 64);
    sedeId.toCharArray(config.sede_id, 50);
    sedeNombre.toCharArray(config.sede_nombre, 100);
    apiKey.toCharArray(config.api_key, 65);
    strcpy(config.api_endpoint, BACKEND_UPDATES_URL);

    saveConfig();
    isConfigured = true;

    String successHtml = "<html><body style='font-family:Arial; text-align:center; padding:50px; background:#1e3a5f; color:white;'>";
    successHtml += "<h1 style='color:#10b981;'>¡Activación Exitosa!</h1>";
    successHtml += "<p style='font-size:18px;'>Sede: <strong>" + sedeNombre + "</strong></p>";
    successHtml += "<p>El dispositivo se reiniciará en 5 segundos...</p>";
    successHtml += "<p style='color:#93c5fd; margin-top:30px;'>Sistema Red Maker Misiones</p>";
    successHtml += "</body></html>";

    server.send(200, "text/html", successHtml);

    String sedeNormalizada = normalizarTexto(sedeNombre.c_str());
    displayMessage("Activado!", sedeNormalizada.c_str());
    setLEDColor(0, 255, 0);  // VERDE
    pixels.show();

    delay(5000);
    ESP.restart();

  } else {
    Serial.print("[ERROR] Fallo en activación. HTTP code: ");
    Serial.println(httpCode);

    if (httpCode < 0) {
      Serial.println("[DEBUG] Error de conexión/SSL. Posibles causas:");
      Serial.println("  1. Core ESP32 desactualizado (recomendado: >=2.0.0)");
      Serial.println("  2. Servidor no accesible");
      Serial.println("  3. URL mal configurada");
      Serial.print("[DEBUG] Error string: ");
      Serial.println(http.errorToString(httpCode));
    }

    // Mostrar error en display según código HTTP
    if (httpCode == 404) {
      displayMessage("ERROR 404", "Codigo invalido");
    } else if (httpCode == 422) {
      displayMessage("ERROR 422", "Codigo usado");
    } else if (httpCode == 401) {
      displayMessage("ERROR 401", "No autorizado");
    } else if (httpCode == -1) {
      displayMessage("ERROR SSL/Red", "Ver Serial");
    } else if (httpCode < 0) {
      char errorMsg[20];
      sprintf(errorMsg, "ERR %d", httpCode);
      displayMessage("ERROR Conexion", errorMsg);
    } else {
      char errorMsg[20];
      sprintf(errorMsg, "HTTP %d", httpCode);
      displayMessage("ERROR", errorMsg);
    }
    setLEDColor(255, 0, 0);  // ROJO
    pixels.show();
    delay(3000);

    String errorHtml = "<html><body style='font-family:Arial; text-align:center; padding:50px;'>";
    errorHtml += "<h2 style='color:red;'>Código Inválido</h2>";
    errorHtml += "<p>El código de activación no es válido o ya fue usado.</p>";
    errorHtml += "<p>Código HTTP: " + String(httpCode) + "</p>";
    errorHtml += "<p>Verifica el código con el administrador.</p>";
    errorHtml += "<a href='/' style='color:blue;'>Volver a intentar</a>";
    errorHtml += "</body></html>";
    server.send(400, "text/html", errorHtml);

    // Volver a modo AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WiFi.softAPSSID().c_str());
    displayAPInfo(WiFi.softAPSSID());
    setLEDColor(0, 0, 255);  // AZUL
    pixels.show();
  }

  http.end();
}

// ============================================
// CONEXIÓN WIFI
// ============================================
void connectWiFi() {
  Serial.print("[WiFi] Conectando a: ");
  Serial.println(config.wifi_ssid);

  displayMessage("WiFi...", config.wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifi_ssid, config.wifi_password);

  int attempts = 0;
  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    // Permitir cancelar con botón medio (hold 10s durante conexión)
    if (digitalRead(BUTTON_MIDDLE_PIN) == LOW) {
      unsigned long holdTime = millis() - startTime;

      if (holdTime >= HOLD_TIME_RESET) {
        Serial.println("\n[BTN-MIDDLE] Reset solicitado durante conexión WiFi");
        displayMessage("RESET", "Cancelando...");
        delay(1000);
        resetConfig();
        return;  // resetConfig() reinicia el ESP32
      }

      // Feedback visual durante hold
      if (holdTime >= 1000) {
        if ((holdTime % 250) < 125) {
          setLEDColor(255, 0, 0); // Rojo parpadeante rápido
          pixels.show();
        }
      }
    } else {
      startTime = millis();  // Reset timer si suelta el botón
    }

    delay(500);
    Serial.print(".");

    // LED naranja parpadeante solo si no está presionando reset
    if (digitalRead(BUTTON_MIDDLE_PIN) == HIGH) {
      setLEDColor(255, 165, 0);  // NARANJA parpadeante
      pixels.show();
      delay(100);
      setLEDColor(0, 0, 0);
      pixels.show();
    }

    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[OK] WiFi conectado");
    Serial.print("[INFO] IP: ");
    Serial.println(WiFi.localIP());
    displayMessage("WiFi OK", WiFi.localIP().toString().c_str());
    delay(2000);
  } else {
    Serial.println("\n[ERROR] No se pudo conectar a WiFi");
    Serial.println("[INFO] Mantén botón medio 10s para resetear configuración");
    currentState = STATE_NO_WIFI;
    setLEDColor(255, 0, 0);  // ROJO
    pixels.show();
    displayMessage("Error WiFi", "Hold 10s=Reset");
  }
}

// ============================================
// SENSORES Y ENVÍO DE DATOS
// ============================================
void updateSensorReadings() {
  lastTemperatura = htu.readTemperature();
  lastHumedad = htu.readHumidity();

  if (!isnan(lastTemperatura) && !isnan(lastHumedad)) {
    displaySensorData(lastTemperatura, lastHumedad);
  }
}

void sendSensorData() {
  Serial.println("\n----------------------------------------");
  Serial.println("[INICIO] Enviando datos...");

  float temperatura = htu.readTemperature();
  float humedad = htu.readHumidity();

  if (isnan(temperatura) || isnan(humedad)) {
    Serial.println("[ERROR] Fallo al leer del sensor HTU21D");
    displayMessage("Error", "Sensor HTU21D");
    return;
  }

  lastTemperatura = temperatura;
  lastHumedad = humedad;

  Serial.print("[SENSOR] Temperatura: ");
  Serial.print(temperatura);
  Serial.println(" °C");
  Serial.print("[SENSOR] Humedad: ");
  Serial.print(humedad);
  Serial.println(" %");

  // Preparar JSON payload
  String jsonPayload = "{";
  jsonPayload += "\"temperatura\":" + String(temperatura, 1) + ",";
  jsonPayload += "\"humedad\":" + String(humedad, 1);
  jsonPayload += "}";

  Serial.print("[HTTP] Endpoint: ");
  Serial.println(config.api_endpoint);
  Serial.print("[HTTP] Payload: ");
  Serial.println(jsonPayload);

  HTTPClient http;
  http.begin(config.api_endpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", config.api_key);
  http.setTimeout(10000);  // Timeout de 10 segundos

  int httpCode = http.POST(jsonPayload);

  if (httpCode == 200) {
    String response = http.getString();
    Serial.print("[HTTP] Respuesta: ");
    Serial.println(response);
    Serial.println("[OK] Datos enviados exitosamente");

    currentState = STATE_OK;
    setLEDColor(0, 255, 0);  // VERDE
    pixels.show();

    // Crear mensaje temporal para el display
    char tempMsg[20];
    sprintf(tempMsg, "%.1fC %.0f%%", temperatura, humedad);
    displayMessage("Enviado OK", tempMsg);
    delay(2000);

  } else if (httpCode > 0) {
    Serial.print("[ERROR] HTTP code: ");
    Serial.println(httpCode);
    currentState = STATE_NO_ENDPOINT;
    setLEDColor(255, 165, 0);  // NARANJA
    pixels.show();

    // Crear mensaje de error con código HTTP
    char errorMsg[20];
    sprintf(errorMsg, "Codigo: %d", httpCode);
    displayMessage("Error HTTP", errorMsg);

  } else {
    Serial.print("[ERROR] Error de conexión: ");
    Serial.println(http.errorToString(httpCode));
    currentState = STATE_NO_ENDPOINT;
    setLEDColor(255, 165, 0);  // NARANJA
    pixels.show();
    displayMessage("Sin endpoint", "Verifique");
  }

  http.end();
  Serial.println("----------------------------------------\n");
}

// ============================================
// NORMALIZACIÓN DE TEXTO (PARA DISPLAY OLED)
// ============================================
// Convierte caracteres UTF-8 con acentos a ASCII (sin acentos)
// Necesario porque Adafruit_GFX solo soporta ASCII básico (0-127)
String normalizarTexto(const char* texto) {
  String resultado = "";
  int len = strlen(texto);

  for (int i = 0; i < len; i++) {
    unsigned char c = (unsigned char)texto[i];

    // Si es un byte de inicio UTF-8 (0xC3 para caracteres latinos)
    if (c == 0xC3 && i + 1 < len) {
      unsigned char siguiente = (unsigned char)texto[i + 1];
      i++; // Saltar el siguiente byte ya que lo procesamos aquí

      // Caracteres minúsculas con acentos
      if (siguiente == 0xA1) resultado += 'a';      // á
      else if (siguiente == 0xA9) resultado += 'e'; // é
      else if (siguiente == 0xAD) resultado += 'i'; // í
      else if (siguiente == 0xB3) resultado += 'o'; // ó
      else if (siguiente == 0xBA) resultado += 'u'; // ú
      else if (siguiente == 0xB1) resultado += 'n'; // ñ
      else if (siguiente == 0xBC) resultado += 'u'; // ü
      // Caracteres mayúsculas con acentos
      else if (siguiente == 0x81) resultado += 'A'; // Á
      else if (siguiente == 0x89) resultado += 'E'; // É
      else if (siguiente == 0x8D) resultado += 'I'; // Í
      else if (siguiente == 0x93) resultado += 'O'; // Ó
      else if (siguiente == 0x9A) resultado += 'U'; // Ú
      else if (siguiente == 0x91) resultado += 'N'; // Ñ
      else if (siguiente == 0x9C) resultado += 'U'; // Ü
      else {
        // Si no reconocemos el carácter, mantener el original
        resultado += (char)c;
        resultado += (char)siguiente;
      }
    }
    // Caracteres ASCII normales (0-127)
    else if (c < 128) {
      resultado += (char)c;
    }
    // Otros bytes UTF-8 - intentar mantener o ignorar
    else {
      // Ignorar bytes UTF-8 no reconocidos para evitar caracteres raros
    }
  }

  return resultado;
}

// ============================================
// DISPLAY OLED
// ============================================
void displayMessage(const char* line1, const char* line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 20);
  display.println(line1);

  if (line2 != nullptr) {
    display.setCursor(0, 40);
    display.println(line2);
  }

  display.display();
}

void displaySensorData(float temp, float hum) {
  display.clearDisplay();

  // Línea 1: Red Maker Misiones
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Red Maker Misiones");

  // Línea 2: Nombre de la sede (normalizado y capitalizado)
  display.setCursor(0, 10);
  String sedeNormalizada = normalizarTexto(config.sede_nombre);
  if (sedeNormalizada.length() > 0) {
    sedeNormalizada[0] = toupper(sedeNormalizada[0]);
  }
  display.println(sedeNormalizada);

  // Temperatura (más grande)
  display.setTextSize(2);
  display.setCursor(0, 26);
  display.print(temp, 1);
  display.setTextSize(1);
  display.print(" C");

  // Humedad (más grande)
  display.setTextSize(2);
  display.setCursor(0, 46);
  display.print(hum, 0);
  display.setTextSize(1);
  display.print(" %");

  display.display();
}

// Función para actualizar el display según pantalla actual
void updateDisplay() {
  switch (currentScreen) {
    case 0:
      // Pantalla 1: Temperatura/Humedad
      displaySensorData(lastTemperatura, lastHumedad);
      break;

    case 1:
      // Pantalla 2: WiFi e IP
      displayWiFiInfo();
      break;

    case 2:
      // Pantalla 3: Estado de Sincronización
      displaySyncInfo();
      break;

    case 3:
      // Pantalla 4: Estadísticas
      displayStats();
      break;
  }
}

void displayWiFiInfo() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("WiFi Info [2/4]");
  display.println("====================");

  // SSID
  display.println("");
  display.print("Red: ");
  display.println(config.wifi_ssid);

  // Señal WiFi
  int rssi = WiFi.RSSI();
  display.print("Senal: ");
  if (rssi > -50) display.println("Excelente");
  else if (rssi > -60) display.println("Buena");
  else if (rssi > -70) display.println("Regular");
  else display.println("Debil");

  // IP
  display.print("IP: ");
  display.println(WiFi.localIP().toString());

  display.display();
}

void displaySyncInfo() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("Sincronizacion [3/4]");
  display.println("====================");

  display.println("");
  display.println("Ultima sinc:");

  unsigned long timeSinceLastSend = millis() - lastSendTime;
  int minAgo = timeSinceLastSend / 60000;
  display.print("Hace ");
  display.print(minAgo);
  display.println(" min");

  display.println("");
  display.println("Proximo envio:");
  unsigned long timeToNextSend = SEND_INTERVAL - timeSinceLastSend;
  int minToNext = timeToNextSend / 60000;
  display.print("En ");
  display.print(minToNext);
  display.println(" min");

  display.display();
}

void displayStats() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("Estadisticas [4/4]");
  display.println("====================");

  // Uptime
  unsigned long uptimeSeconds = millis() / 1000;
  int days = uptimeSeconds / 86400;
  int hours = (uptimeSeconds % 86400) / 3600;
  int minutes = (uptimeSeconds % 3600) / 60;

  display.println("");
  display.print("Uptime: ");
  if (days > 0) {
    display.print(days);
    display.print("d ");
  }
  display.print(hours);
  display.print("h ");
  display.print(minutes);
  display.println("m");

  // RAM libre
  display.print("RAM libre: ");
  display.print(ESP.getFreeHeap() / 1024);
  display.println(" KB");

  // Estado
  display.println("");
  display.print("Estado: ");
  if (currentState == STATE_OK) display.println("OK");
  else if (currentState == STATE_NO_ENDPOINT) display.println("Sin Server");
  else display.println("Sin WiFi");

  display.display();
}

void displayDebugInfo() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("DEBUG INFO");
  display.println("====================");

  // MAC Address
  display.println("");
  display.println("MAC:");
  display.println(getMacAddressString());

  // IP
  display.print("IP: ");
  display.println(WiFi.localIP().toString());

  // RAM
  display.print("RAM: ");
  display.print(ESP.getFreeHeap() / 1024);
  display.println(" KB");

  display.display();

  Serial.println("\n[DEBUG] ===== INFO =====");
  Serial.print("[DEBUG] MAC: ");
  Serial.println(getMacAddressString());
  Serial.print("[DEBUG] IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("[DEBUG] RAM libre: ");
  Serial.print(ESP.getFreeHeap() / 1024);
  Serial.println(" KB");
  Serial.print("[DEBUG] Uptime: ");
  Serial.print(millis() / 1000);
  Serial.println(" seg");
  Serial.println("[DEBUG] ==============\n");
}

void displayAPInfo(String ssid) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("MODO CONFIG");
  display.println("====================");

  display.println("");
  display.println("1. Conectar WiFi:");
  display.println(ssid);

  display.println("");
  display.println("2. Abrir navegador");
  display.println("   (abre solo)");

  display.display();

  Serial.println("[DISPLAY] Instrucciones de configuración mostradas");
}

// ============================================
// LED RGB (NeoPixel)
// ============================================
void setLEDColor(uint8_t r, uint8_t g, uint8_t b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b));
}

void restoreLEDState() {
  // Restaurar color del LED según el estado actual
  if (currentState == STATE_OK) {
    setLEDColor(0, 255, 0);  // VERDE
  } else if (currentState == STATE_NO_ENDPOINT) {
    setLEDColor(255, 165, 0);  // NARANJA
  } else {
    setLEDColor(255, 0, 0);  // ROJO
  }
  pixels.show();
}
