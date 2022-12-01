#include <YuboxSimple.h>
#include <YuboxEspNowClass.h>
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
Task task_yuboxUpdateLector(TASK_SECOND * 5, TASK_FOREVER, &yuboxUpdateLector, &yuboxScheduler, true);

void yuboxEnviarMuestra(void);
Task task_yuboxPublicar(TASK_SECOND * 1, TASK_FOREVER, &yuboxEnviarMuestra, &yuboxScheduler, true);

//---------------- CALLBACKS YUBOX ESPNOW ---------------
void onEspMessage(const uint8_t *address, const uint8_t *data, int dataLen);
void onEspSend(const uint8_t *address, bool status);

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

  // Iniciamos la configuracion ESP-NOW
  YuboxEspNowConfig.begin();

  AsyncEspNow &espNow = YuboxEspNowConfig.getEspNowClient();
  espNow.onMessage(onEspMessage);
  espNow.onSend(onEspSend);

#ifdef YUBOX_SENSOR_INTEGRADO
  if (!sensor_bmp280.begin(BMP280_ADDRESS_ALT))
  {
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

void leerTempPress(float &temperature, float &pressure)
{
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
  if (temperature < 10.0f)
    temperature = 10.0f;
  if (temperature > 40.0f)
    temperature = 40.0f;
  if (pressure < 100000.0f)
    pressure = 100000.0f;
  if (pressure > 102000.0f)
    pressure = 102000.0f;
#endif
}

void yuboxUpdateLector(void)
{
  if (YuboxNTPConf.isNTPValid(0))
  {
    float temperature, pressure;
    leerTempPress(temperature, pressure);

    DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(3));
    json_doc["ts"] = 1000ULL * YuboxNTPConf.getUTCTime();
    json_doc["temperature"] = temperature;
    json_doc["pressure"] = pressure;

    String json_output;
    serializeJson(json_doc, json_output);

    if (eventosLector.count() > 0)
    {
      eventosLector.send(json_output.c_str());
    }
  }
}

uint8_t peerAddress[] = {0xC8, 0x2B, 0x96, 0xA8, 0xF6, 0x50};

typedef struct msgData
{
  float temperature;
  float pressure;
};

void yuboxEnviarMuestra(void)
{
  // Obtengo la medicion del sensor
  float temperature, pressure;
  leerTempPress(temperature, pressure);

  // Creamos el JSON para publicar
  msgData msg;
  msg.temperature = temperature;
  msg.pressure = pressure;

  AsyncEspNow &espNow = YuboxEspNowConfig.getEspNowClient();
  espNow.send(peerAddress, ((uint8_t *)&msg), sizeof(msg));
}

//-------------------------------------------------------
//------------------ CALLBACKS YUBOX MQTT ---------------
//-------------------------------------------------------
void onEspMessage(const uint8_t *address, const uint8_t *data, int dataLen)
{
  msgData msg;
  memcpy(&msg, data, sizeof(msg));

  String mac = formatMacAddress(address);
  Serial.printf("Recive to %s \n", mac.c_str());
  Serial.print("Bytes received: ");
  Serial.println(dataLen);
  Serial.print("Temperature: ");
  Serial.println(msg.temperature);
  Serial.print("Presion: ");
  Serial.println(msg.pressure);
  Serial.println("");
}

void onEspSend(const uint8_t *address, bool status)
{
  String mac = formatMacAddress(address);
  log_d("Send message to %s, Status: %s\n", mac.c_str(), status ? "true" : "false");
}
