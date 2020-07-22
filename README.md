# yubox-framework
Librería que consolida varias necesidades comunes a todos los proyectos basados en YUBOX ESP32 para IoT.

## Tabla de contenido
- [yubox-framework](#yubox-framework)
  - [Tabla de contenido](#tabla-de-contenido)
  - [Requerimientos del framework](#requerimientos-del-framework)
  - [Instalación](#instalación)
    - [Requisitos en PC de desarrollo](#requisitos-en-pc-de-desarrollo)
    - [Dependencias Arduino](#dependencias-arduino)
    - [Obtener biblioteca](#obtener-biblioteca)
  - [Modelo de funcionamiento](#modelo-de-funcionamiento)
  - [Estructura de directorios](#estructura-de-directorios)
  - [Creación de un nuevo proyecto](#creación-de-un-nuevo-proyecto)
    - [Archivos y directorios de proyecto](#archivos-y-directorios-de-proyecto)
    - [Desarrollo cliente - JavaScript](#desarrollo-cliente---javascript)
    - [Desarrollo server - Arduino C++](#desarrollo-server---arduino-c++)
    - [Transferencia de sketch al ESP32](#transferencia-de-sketch-al-ESP32)
  - [Configuración básica usando interfaz web](#configuración-básica-usando-interfaz-web)
    - [Ingreso a red softAP y configuración WiFi](#ingreso-a-red-softap-y-configuración-wifi)
    - [Credenciales y cambio de clave](#credenciales-y-cambio-de-clave)
    - [Configuración NTP](#configuración-ntp)
    - [Actualización de firmware](#actualización-de-firmware)
    - [Configuración MQTT (según proyecto)](#configuración-mqtt-según-proyecto)

## Requerimientos del framework

A partir de la historia de proyectos de IoT de [YUBOX](https://yubox.com/) se ha notado que las implementaciones
entregadas a los clientes tienen características en común que fue necesario implementar, o que hubiese sido
de mucha ayuda haber llegado a implementar. Una lista, no exhaustiva, de tales características se muestra
a continuación:
- Configuración del dispositivo a través de una interfaz Web
- Protección contra acceso no autorizado a la configuración del dispositivo mediante contraseña
- Almacenamiento de configuración en NVRAM en oposición a valores incrustados en el código fuente
- Descubrimiento y configuración in situ de redes WiFi para conectividad a Internet. Esto necesariamente implica
  el uso de la interfaz softAP del ESP32 para tener acceso al dispositivo antes de configurar credenciales
  WiFi.
- Configuración de servidor de hora (NTP) personalizado dentro de redes aisladas del exterior
- Descubrimiento de la existencia del dispositivo en una red local
- Configuración estandar de acceso a un servidor MQTT
- Actualización OTA (Over The Air) del firmware del dispositivo con uso del rollback disponible en el ESP32

La biblioteca de YUBOX Framework es un proyecto en desarrollo que implementa las funcionalidades descritas arriba
y presenta una interfaz web estandarizada usando [Bootstrap 4](https://getbootstrap.com/) para disponer de widgets
estándar. Se debe hacer notar que la porción C++ que se ejecuta en el ESP32 usa AJAX para su comunicación y no
depende estrictamente de Bootstrap. Sin embargo, cualquier intento de migrar la interfaz para usar otra biblioteca
de interfaz web cliente, requiere una reescritura de todas las porciones HTML y Javascript del framework, y está
más allá del alcance de esta documentación.

## Instalación

### Requisitos en PC de desarrollo

### Dependencias Arduino

### Obtener biblioteca

## Modelo de funcionamiento

## Estructura de directorios

## Creación de un nuevo proyecto

### Archivos y directorios de proyecto

### Desarrollo cliente - JavaScript

### Desarrollo server - Arduino C++

### Transferencia de sketch al ESP32

## Configuración básica usando interfaz web

### Ingreso a red softAP y configuración WiFi

### Credenciales y cambio de clave

### Configuración NTP

### Actualización de firmware

### Configuración MQTT (según proyecto)

