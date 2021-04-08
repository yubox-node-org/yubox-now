#include <YuboxSimple.h>

#include <YuboxMQTTConfClass.h>

// El modelo viejo de YUBOX tiene este sensor integrado en el board
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

Adafruit_BMP280 sensor_bmp280;

AsyncEventSource eventosLector("/yubox-api/lectura/events");

void setup()
{
  // La siguiente demora es sólo para comodidad de desarrollo para enchufar el USB
  // y verlo en gtkterm. No es en lo absoluto necesaria como algoritmo requerido.
  delay(3000);
  Serial.begin(115200);

  yuboxAddManagedHandler(&eventosLector);

  YuboxMQTTConf.begin(yubox_HTTPServer);

  yuboxSimpleSetup();

  if (!sensor_bmp280.begin(BMP280_ADDRESS_ALT)) {
    Serial.println("ERR: no puede inicializarse el sensor BMP280!");
  }
}

void loop()
{
  yuboxSimpleLoopTask();

  if (YuboxNTPConf.isNTPValid(0)) {
    DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(3));
    json_doc["ts"] = 1000ULL * YuboxNTPConf.getUTCTime();
    json_doc["temperature"] = sensor_bmp280.readTemperature();
    json_doc["pressure"] = sensor_bmp280.readPressure();

    String json_output;
    serializeJson(json_doc, json_output);

    if (eventosLector.count() > 0) {
      eventosLector.send(json_output.c_str());
    }
  }

  delay(3000);
}
