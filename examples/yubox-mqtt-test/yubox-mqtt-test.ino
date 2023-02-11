#include <YuboxSimple.h>
#include <YuboxMQTTConfClass.h>
#include <TaskScheduler.h>

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

#define ARDUINOJSON_USE_LONG_LONG 1
#define PERIODO_EVENTO_MQTT_SEC_DEFAULT 30

//----------------------- TASK -----------------------
Scheduler yuboxScheduler;

void yuboxUpdateLector(void);
Task task_yuboxUpdateLector(TASK_SECOND*5, TASK_FOREVER, &yuboxUpdateLector, &yuboxScheduler, true);

void yuboxEnviarMuestraMQTT(void);
Task task_yuboxPublicarMQTT(TASK_SECOND *PERIODO_EVENTO_MQTT_SEC_DEFAULT, TASK_FOREVER, &yuboxEnviarMuestraMQTT, &yuboxScheduler, true);

//---------------- CALLBACKS YUBOX MQTT ---------------
void onMqttConnect(bool sessionPresent);
void onMqttPublish(uint16_t packetId);
void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
void onMqttSubscribe(uint16_t packetId, uint8_t qos);

//-------------------------------------------------
AsyncEventSource eventosLector("/yubox-api/lectura/events");
void leerTempPress(float &temp, float &pression);

void setup()
{
  // La siguiente demora es sólo para comodidad de desarrollo para enchufar el USB
  // y verlo en gtkterm. No es en lo absoluto necesaria como algoritmo requerido.
  delay(3000);
  Serial.begin(115200);

  yuboxAddManagedHandler(&eventosLector);
  yuboxSimpleSetup();

  
  // Iniciamos la configuracion MQTT
  YuboxMQTTConf.begin(yubox_HTTPServer);
  
  // Funcion necesaria para YuboxNow se autoconecte a MQTT
  YuboxMQTTConf.setAutoConnect(true);
  YuboxMQTTConf.onMQTTInterval(mqtt_intervalchange);

  // Procedemos a configurar los callbacksMQTT
  AsyncMqttClient &mqttClient = YuboxMQTTConf.getMQTTClient();

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.onMessage(onMqttMessage);

  Serial.printf("Intervalo de envío MQTT inicia en %u milisegundos\n", YuboxMQTTConf.getRequestedMQTTInterval());
  task_yuboxPublicarMQTT.setInterval(TASK_MILLISECOND * YuboxMQTTConf.getRequestedMQTTInterval());

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
  yuboxScheduler.execute();
}

void leerTempPress(float &temperature, float &pressure){
#ifndef YUBOX_SENSOR_INTEGRADO
    float last_temperature = 25.0f;
    float last_pressure = 101325.0f;
#endif
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
}

void yuboxUpdateLector(void)
{
  if (YuboxNTPConf.isNTPValid(0)) {
    float temperature, pressure;
    leerTempPress(temperature, pressure);

    DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(3));
    json_doc["ts"] = 1000ULL * YuboxNTPConf.getUTCTime();
    json_doc["temperature"] = temperature;
    json_doc["pressure"] = pressure;

    String json_output;
    serializeJson(json_doc, json_output);

    if (eventosLector.count() > 0) {
      eventosLector.send(json_output.c_str());
    }
  }
}

void yuboxEnviarMuestraMQTT(void)
{
  // Obtengo la medicion del sensor
  float temperature, pressure;
  leerTempPress(temperature, pressure);

  // Creamos el JSON para publicar
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(3));
  json_doc["ts"] = 1000ULL * YuboxNTPConf.getUTCTime();
  json_doc["temperature"] = temperature;
  json_doc["pressure"] = pressure;

  String json_output;
  serializeJson(json_doc, json_output);

  // Intentamos conectarnos
  if (WiFi.isConnected())
  {
    AsyncMqttClient &mqttClient = YuboxMQTTConf.getMQTTClient();
    if (mqttClient.connected())
    {
      // Creamos el topico
      String yuboxMQTT_topic_full = "YUBOX/test/";
      yuboxMQTT_topic_full += YuboxWiFi.getMDNSHostname();

      // Publicamos el mensaje
      mqttClient.publish(
          yuboxMQTT_topic_full.c_str(), // Tópico sobre el cual publicar
          1,                            // QoS (0,1,2)
          false,                        // Poner a true para retener el mensaje luego de publicado. Nuevos clientes verán el mensaje luego de publicado.
          json_output.c_str()           // Payload, cadena de C delimitada por \0 si no se especifica parámetro siguiente de longitud
                                        // longitud_de_payload
      );
    }
  }
}

//-------------------------------------------------------
//------------------ CALLBACKS YUBOX MQTT ---------------
//-------------------------------------------------------
void mqtt_intervalchange(void)
{
    log_i("Intervalo de envío MQTT es ahora %u milisegundos", YuboxMQTTConf.getRequestedMQTTInterval());
    task_yuboxPublicarMQTT.setInterval(TASK_MILLISECOND * YuboxMQTTConf.getRequestedMQTTInterval());
}

void onMqttConnect(bool sessionPresent)
{
  log_d("Connected to MQTT.");
  log_d("Session present: %s", sessionPresent ? "YES" : "NO");
}

void onMqttPublish(uint16_t packetId)
{
  log_d("Publish acknowledged.");
  log_d("  packetId: %u", packetId);
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos)
{
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
  log_d("Publish received.");
  log_d("  topic: %s", topic);
  log_d("  qos: %hhu", properties.qos);
  log_d("  dup: %s", properties.dup ? "YES" : "NO");
  log_d("  retain: %s", properties.retain ? "YES" : "NO");
  log_d("  len: %u", len);
  log_d("  index: %u", index);
  log_d("  total: %u", total);

  // Se asume que se reciben secuencialemente los pedazos de un solo mensaje.
  static uint8_t *pbuf = NULL;

  // Procesamiento para primer chunk del mensaje...
  if (index == 0)
  {
    if (pbuf != NULL)
    {
      log_w("buffer de payload no era NULL al iniciar chunk! Esto no debería pasar.");
      free(pbuf);
      pbuf = NULL;
    }

    log_d("Se intenta asignar %u bytes para payload...", total + 1);
    pbuf = (uint8_t *)malloc(total + 1);
    if (pbuf == NULL)
    {
      log_e("no se puede asignar %u bytes para payload completo, se ignora mensaje!", total);
    }
    else
    {
      pbuf[total] = '\0'; // Para armar cadena ASCII a partir de payload
    }
  }
  if (pbuf == NULL)
    return;

  // Copiar segmento recibido de payload para armar mensaje completo...
  memcpy(pbuf + index, payload, len);

  if (index + len >= total)
  {
    // Se acaba de recibir el mensaje completo...
    Serial.printf("Recibido mensaje completo (%u bytes):\r\n\n%s", total, pbuf);
  }
}