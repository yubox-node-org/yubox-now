#include <YuboxSimple.h>

#include <YuboxMQTTConfClass.h>

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
  
}

void loop()
{
  yuboxSimpleLoopTask();

  if (YuboxNTPConf.isNTPValid(0)) {
    DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(3));
    json_doc["ts"] = 1000ULL * YuboxNTPConf.getUTCTime();
    json_doc["temperature"] = random(1,100);
    json_doc["pressure"] = random(1,100);

    String json_output;
    serializeJson(json_doc, json_output);

    if (eventosLector.count() > 0) {
      eventosLector.send(json_output.c_str());
    }
  }

  delay(3000);
}
