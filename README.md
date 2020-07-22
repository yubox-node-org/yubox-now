# YUBOX Framework
Librería que consolida varias necesidades comunes a todos los proyectos basados en YUBOX ESP32 para IoT.

## Tabla de contenido
- [YUBOX Framework](#yubox-framework)
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

Para poder construir un proyecto que use el Yubox Framework, se debe preparar la PC de desarrollo con los requisitos
descritos a continuación:

### Requisitos en PC de desarrollo

Esta guía ha sido construida desde el punto de vista de un desarrollo en una PC con una distro Linux. Consecuentemente
todos los comandos y utilidades indicados a continuación son presentados como los ejecutaría un usuario de Linux. El
desarrollo con YUBOX Framework en un sistema operativo distinto (MacOS o Windows) puede requerir pasos adicionales.

Se requieren los siguientes paquetes y componentes en la PC de desarrollo:
- Arduino IDE, en su versión 1.8 o superior. No está soportado el desarrollo usando versiones anteriores de Arduino IDE.
  En particular, las distros basadas en Ubuntu podrían tener un paquete arduino que es una versión muy vieja para
  funcionar correctamente con el resto de paquetes. Si la versión instalada es muy vieja, debe actualizarse con una
  versión más reciente instalada desde el zip o targz oficial de [Arduino](#https://www.arduino.cc/en/Main/Software).
  ATENCIÓN: algunas distros ofrecen la instalación de Arduino a través de Flatpak. Sin embargo, el modelo de ejecución
  de Flatpak puede obstruir el acceso a los puertos seriales y también negar el acceso al intérprete Python del sistema
  lo cual impide completamente subir programas al ESP32. Se recomienda no instalar el Arduino IDE desde Flatpak, sino
  usando el zip o targz oficial, o el paquete ofrecido por los repositorios de la distro (si es lo suficientemente
  reciente).
- Soporte de ESP32 para el Arduino IDE. Para instalar este soporte, ejecute el Arduino IDE, elija del menú la opción
  Archivo-->Preferencias, y en el cuadro de diálogo inserte o agregue en la caja de texto "Gestor de URLs Adicionales
  de Tarjetas" la siguiente URL: https://dl.espressif.com/dl/package_esp32_index.json .
  *NOTA*: Si no aparece la caja de texto para URLs de tarjetas, su versión de Arduino IDE podría ser muy vieja. Revise la
  versión de su IDE y actualice en caso necesario.
  A continuación pulse el botón OK, reinicie el Arduino IDE, y elija la opción de menú Herramientas-->Placa-->Gestor
  de tarjetas. Entonces debe mostrarse un diálogo donde se carga la lista de soportes de tarjetas, incluyendo el soporte
  "esp32". Pulse sobre esa fila el botón "Instalar", y deje que todos los componentes se descarguen. Luego de completar
  la instalación, reinicie el Arduino IDE, y elija las siguientes opciones de tarjeta en el menú Herramientas. Note que
  el orden de elección es importante, porque el tipo de tarjeta condiciona la aparición de las siguientes opciones:
  - Herramientas-->Placa-->ESP32 Arduino-->NodeMCU-32S
  - Herramientas-->Upload Speed-->921600
  - Herramientas-->Flash Frequency-->80MHz

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

