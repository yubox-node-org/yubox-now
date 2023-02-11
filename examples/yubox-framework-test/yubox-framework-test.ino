#include <YuboxSimple.h>

/*
Si se dispone de una placa con el sensor BMP280 conectado en el bus I2C, como
el board original YUBOX, se puede activar el uso de este sensor de la siguiente
manera:
1) Se debe crear un archivo llamado build_opt.h en el directorio del proyecto
2) El contenido del archivo debe contener en una línea la siguiente directiva:

-DYUBOX_SENSOR_INTEGRADO=1

 */
#ifdef YUBOX_SENSOR_INTEGRADO
// El modelo viejo de YUBOX tiene este sensor integrado en el board
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

Adafruit_BMP280 sensor_bmp280;
#endif

AsyncEventSource eventosLector("/yubox-api/lectura/events");

void setup()
{
  // La siguiente demora es sólo para comodidad de desarrollo para enchufar el USB
  // y verlo en gtkterm. No es en lo absoluto necesaria como algoritmo requerido.
  delay(3000);
  Serial.begin(115200);

  yuboxAddManagedHandler(&eventosLector);

  yuboxSimpleSetup();

#ifdef YUBOX_SENSOR_INTEGRADO
  if (!sensor_bmp280.begin(BMP280_ADDRESS_ALT)) {
    Serial.println("ERR: no puede inicializarse el sensor BMP280!");
  }
#else
  Serial.println("Usando MOCKUP en lugar de un sensor válido.");
#endif
}

void loop()
{
  yuboxSimpleLoopTask();

  if (YuboxNTPConf.isNTPValid(0)) {
    float temperature, pressure;
#ifndef YUBOX_SENSOR_INTEGRADO
    float last_temperature = 25.0f;
    float last_pressure = 101325.0f;
#endif

    DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(3));
    json_doc["ts"] = 1000ULL * YuboxNTPConf.getUTCTime();
#ifdef YUBOX_SENSOR_INTEGRADO
    temperature = sensor_bmp280.readTemperature();
    pressure = sensor_bmp280.readPressure();
#else
    temperature = last_temperature + random(-1000, 1000) / 1000.f;
    pressure = last_pressure + random(-500, 500);
    if (temperature < 10.0f) temperature = 10.0f;
    if (temperature > 40.0f) temperature = 40.0f;
    if (pressure < 100000.0f) pressure = 100000.0f;
    if (pressure > 102000.0f) pressure = 102000.0f;
#endif

    json_doc["temperature"] = temperature;
    json_doc["pressure"] = pressure;

    String json_output;
    serializeJson(json_doc, json_output);

    if (eventosLector.count() > 0) {
      eventosLector.send(json_output.c_str());
    }
  }

  delay(3000);
}
