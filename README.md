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
Además se asume que el usuario Linux tiene conocimiento de línea de comando básica (shell), y que tiene permisos para
instalar paquetes adicionales en caso necesario, tanto localmente como a nivel global del sistema.

Se requieren los siguientes paquetes y componentes en la PC de desarrollo:
- Arduino IDE, en su versión 1.8 o superior. No está soportado el desarrollo usando versiones anteriores de Arduino IDE.
  En particular, las distros basadas en Ubuntu podrían tener un paquete arduino que es una versión muy vieja para
  funcionar correctamente con el resto de paquetes. Si la versión instalada es muy vieja, debe actualizarse con una
  versión más reciente instalada desde el zip o targz oficial de [Arduino](https://www.arduino.cc/en/Main/Software).

  *ATENCIÓN*: algunas distros ofrecen la instalación de Arduino a través de Flatpak. Sin embargo, el modelo de ejecución
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

  Al llegar a este punto, recomendamos que verifique que su Arduino IDE efectivamente puede compilar y enviar un sketch
  mínimo a su YUBOX Node a través del puerto USB Serial, y que el sketch efectivamente se ejecute correctamente.
- Comando `make` instalado correctamente en su sistema. Si el comando `make` no está instalado, es probable que se lo
  pueda instalar con un comando similar a `sudo apt-get install make` (para distros basadas en Debian o Ubunto), o
  `sudo yum install make` o `sudo dnf install make` para distros basadas en RedHat como CentOS, SuSE o Fedora. Otras
  distros pueden requerir sus propias herramientas de instalación de paquetes.
- Intérprete `python3`. Se requiere Python 3 para el comando `yubox-framework-assemble`, el cual no ha sido diseñado para
  ser ejecutado en Python2. En Fedora 32 el intérprete Python por omisión es Python 3.8. En otras distros puede que sea
  necesario instalar explícitamente Python3 con el comando `sudo apt-get install python3` o `sudo yum install python3`
  o `sudo dnf install python3` según su distro.
- Paquete `pip3` que provee [Pip](https://pypi.org/project/pip/) para Python 3. Este paquete puede que sea necesario si
  los paquetes de Python 3 indicados a continuación no están disponibles como paquetes de su distro. Primero verifique
  si existe el comando `/usr/bin/pip3`. Si el comando no existe, instálelo con `sudo apt-get install python3-pip`,
  `sudo yum install python3-pip`, o `sudo dnf install python3-pip` según su distro de Linux.
- El comando `yubox-framework-assemble` requiere además de los siguientes paquetes. Para cada uno, intente primero
  instalar una versión provista por el repositorio de su distro Linux. Si la distro no provee el paquete, entonces ejecute
  el comando `pip3 install PAQUETE` donde PAQUETE debe reemplazarse por el paquete a instalar:
  - `pystache`, intérprete de plantillas [Mustache](http://mustache.github.io/) para Python, posiblemente disponible como `python3-pystache`.

### Dependencias Arduino

Se requieren las siguientes bibliotecas de código como dependencias de YUBOX Framework:
- `NTPClient` que es una biblioteca para realizar peticiones NTP (Network Time Protocol) para obtener y actualizar la
  hora en el dispositivo ESP32. Esta biblioteca se la puede instalar desde el gestor de bibliotecas de Arduino IDE.
- `ArduinoJSON` que es una biblioteca para serializar y deserializar JSON, para Arduino. Esta biblioteca se usa
  principalmente para construir respuestas JSON a las peticiones AJAX. Esta biblioteca se la puede instalar desde el
  gestor de bibliotecas del Arduino IDE. El YUBOX Framework ha sido probado con la versión 6.15.2 al 22 de julio de
  2020.
- `AsyncTCP` que es una biblioteca para realizar conexiones TCP/IP de forma asíncrona en una tarea separada del ESP32.
  Esta biblioteca **NO** está disponible en el gestor de bibliotecas de Arduino IDE. Para instalarla, visite
  https://github.com/me-no-dev/AsyncTCP y descargue un zip con el código fuente desde https://github.com/me-no-dev/AsyncTCP/archive/master.zip
  o (para uso avanzado) haga un checkout usando `git`. En cualquier caso, debe existir un directorio con el código de
  la biblioteca debajo de `$(HOME)/Arduino/libraries` . Por ejemplo, `/home/fulano/Arduino/libraries/AsyncTCP`. Esta
  biblioteca es un requisito para las dos bibliotecas siguientes.
- `ESPAsyncWebServer` que es una biblioteca para exponer un servidor web en el ESP32, usando `AsyncTCP`. Para instalar
  esta biblioteca, visite https://github.com/me-no-dev/ESPAsyncWebServer y descargue un zip con el código fuente desde
  https://github.com/me-no-dev/ESPAsyncWebServer/archive/master.zip . Debe existir eventualmente un directorio con el
  código debajo de `$(HOME)/Arduino/libraries` . Por ejemplo, `/home/fulano/Arduino/libraries/ESPAsyncWebServer`.
- `Async MQTT client for ESP8266 and ESP32` que es una biblioteca para un cliente MQTT, construida sobre `AsyncTCP`.
  Para instalar esta biblioteca, visite https://github.com/marvinroger/async-mqtt-client y descargue un zip con el código
  fuente desde https://github.com/marvinroger/async-mqtt-client/archive/master.zip . Debe existir eventualmente un
  directorio con el código debajo de `$(HOME)/Arduino/libraries` . Por ejemplo, `/home/fulano/Arduino/libraries/async-mqtt-client`.

Además se recomienda instalar el siguiente addon de Arduino IDE:
- [Arduino ESP32 filesystem uploader](https://github.com/me-no-dev/arduino-esp32fs-plugin), el cual es un addon al Arduino
  IDE que permite poblar la partición SPIFFS del ESP32 con archivos preparados desde un directorio del proyecto. Visite
  https://github.com/me-no-dev/arduino-esp32fs-plugin y siga las instrucciones para instalar el plugin.

### Obtener biblioteca

Para instalar el YUBOX Framework, visite https://github.com/yubox-node-org/yubox-framework y descargue un zip con el código
fuente desde https://github.com/yubox-node-org/yubox-framework/archive/master.zip . Debe existir eventualmente un directorio
con el código debajo de `$(HOME)/Arduino/libraries` . Por ejemplo, `/home/fulano/Arduino/libraries/yubox-framework`.

En este punto, verifique si se puede ejecutar correctamente el programa `yubox-framework-assemble` ejecutando lo siguiente en
una consola de línea de comando:

```
$ chmod +x ~/Arduino/libraries/yubox-framework/yubox-framework-assemble
$ ~/Arduino/libraries/yubox-framework/yubox-framework-assemble
```

Si se han instalado todos los requisitos, la salida debería ser la siguiente línea:

```
Uso: /home/alex/Arduino/libraries/yubox-framework/yubox-framework-assemble /data/template/dir1:/data/template/dir2:(...) module1 (module2 ...)
```

Si en su lugar se muestra un mensaje aludiendo a python3 inexistente, como el siguiente:

```
bash: /home/alex/Arduino/libraries/yubox-framework/yubox-framework-assemble: /usr/bin/python3: intérprete erróneo: No existe el fichero o el directorio
```
Verifique que se dispone de Python 3 en su sistema.

Si en su lugar se muestra un mensaje que contiene mención de ModuleNotFoundError, como el siguiente:

```
Traceback (most recent call last):
  File "/home/alex/Arduino/libraries/yubox-framework/yubox-framework-assemble", line 9, in <module>
    import pystache
ModuleNotFoundError: No module named 'pystache'
```
Verifique que la biblioteca correspondiente esté disponible para Python 3, especialmente `pystache`.

## Modelo de funcionamiento

En el dispositivo ESP32, el espacio total de almacenamiento flash disponible (que no debe confundirse con el
almacenamiento disponible en alguna tarjeta microSD insertada) está dividido en varias particiones. En la
disposición estándar existen dos particiones dedicadas a la aplicación, una partición destinada a NVRAM, y
una partición dedicada a SPIFFS. De las dos particiones de aplicación, una de ellas contiene el código del
sketch en ejecución, y la otra se destina a recibir una posible actualización de código, seguido de un
intercambio de roles. Esto permite realizar un rollback si la nueva aplicación lo requiere, y también permite
que una actualización provea un firmware más grande que la memoria disponible o el espacio en SPIFFS, siempre
y cuando quepa dentro de la partición de actualización.

La partición NVRAM está destinada a almacenar opciones estructuradas accesibles en formato clave/valor, y permite
guardar opciones que deben persistir entre reinicios del dispositivo, como por ejemplo credenciales de WiFi,
o nombres/IPs de servidores remotos.

La partición SPIFFS contiene un sistema de archivos que el sketch puede leer y escribir de forma arbitraria.
En el YUBOX Framework, el sistema de archivos SPIFFS se utiliza para contener la página web de la interfaz de
configuración, su correspondiente Javascript, y las bibliotecas CSS y Javascript accesorias. Estas últimas
bibliotecas incluyen jQuery, Bootstrap 4 (javascript y CSS), y pueden incluir más bibliotecas por requerimiento
de la aplicación.

Un dispositivo que usa el YUBOX Framework expone un servidor web en sus interfases de red WiFi en el puerto 80.
Se aprovecha la capacidad del ESP32 de tener dos interfases WiFi activas (la interfaz STA y la interfaz AP) para
proveer permanentemente un acceso WiFi al dispositivo incluso si la conexión al WiFi del entorno se pierde o no
se ha configurado todavía. En la interfaz AP, el aparato expone por omisión una red WiFi de nombre
`YUBOX-XXXXXXXXXXXX` que se deriva de la MAC del WiFi del dispositivo, sin contraseña. Dentro de la red AP, el
dispositivo es accesible vía la IP 192.168.4.1 (por omisión).

El navegador que visita el servicio web (por cualquiera de las dos interfases) carga una página HTML construida
con Bootstrap 4, previa autenticación si el sketch así lo ha configurado:

![Autenticación admin](extras/yubox-framework-pedir-clave.png)

![Interfaz web con gráfico](extras/yubox-framework-interfaz-ejemplo.png)

La interfaz web es una aplicación de una sola página [(Single-Page-Application)](https://en.wikipedia.org/wiki/Single-page_application)
construida con jQuery y Bootstrap 4. Toda la comunicación para actualizar la página web debe de realizarse a través
de peticiones AJAX, canales Server-Sent-Event o websockets. El propósito de esta arquitectura es el de relegar el
dispositivo al rol de únicamente responder AJAX, sin perder tiempo en construir varias veces una página web. En el menú
de la parte superior se muestran los módulos disponibles para configuración. Por convención el módulo que corresponde
al propósito específico del sketch es el primero en mostrarse en el menú y el que se abre por omisión.

Al cambiar de cejilla en Bootstrap, se emiten eventos de cejilla visible o escondida, los cuales se manejan para
iniciar o detener monitoreos o funcionalidades que deben actualizar la página. Por ejemplo, el mostrar la
cejilla WiFi inicia un canal SSE hacia el servidor que inicia el escaneo de redes WiFi visibles. Este mismo
escaneo deja de realizarse al cerrar el canal, lo cual ocurre al elegir otra cejilla.

Internamente, el framework instala manejadores para las siguientes tareas:
- Se activa mDNS para asignar un nombre de host descubrible en la red WiFi. Si un sistema operativo soporta mDNS
  (Bonjour para MacOS o Avahi bajo Linux), el dispositivo responde al nombre de host `yubox-XXXXXXXXXXXX.local`
  construido a partir de la MAC del dispositivo. Adicionalmente se activa [DNS-SD](https://en.wikipedia.org/wiki/Zero-configuration_networking#DNS-SD) para exponer la presencia del servicio _http._tcp .
- Mantener una conexión WiFi activa siempre que sea posible. Es posible guardar las credenciales de múltiples sitos
  para que el dispositivo pueda ser movido entre ubicaciones con redes distintas sin tener que configurarlo otra vez.
  Si hay múltiples redes conocidas en un solo lugar, el framework elige la más potente primero.
- En caso de activar MQTT, la conexión al servidor MQTT se reintenta si se ha desconectado y se tiene de nuevo
  una conexión disponible.

Mediante el uso de AsyncTCP se consigue que los manejadores de red y de la interfaz web sean invocados desde fuera
del bucle principal del sketch. Entonces, el bucle principal sólo tiene que invocar regularmente el manejador de
NTP (en caso de requerir hora), y únicamente implementar el código que concierne a la aplicación.

## Estructura de directorios

Un proyecto Arduino que usa YUBOX Framework debe de ajustarse a la siguiente estructura de archivos y directorios:

- NombreProyecto/
  - NombreProyecto.ino
  - Fuente1.cpp
  - Fuente2.cpp
  - Makefile
  - modules.txt
  - data/
  - data-template/
    - libreria-adicional-1.js
    - libreria-adicional-2.js.gz
    - medidorproyecto/
      - index.htm
      - yuboxapp.js
      - module.ini
    - otromoduloproyecto/
      - index.htm
      - yuboxapp.js
      - module.ini

Para un ejemplo funcional de un proyecto de YUBOX Framework, examine el directorio `examples/yubox-framework-test` y refiérase a
la explicación a continuación.

En el esquema mostrado arriba, se notan los siguientes nombres de archivo:
- `NombreProyecto` es el nombre arbitrario del directorio de su proyecto.
- `NombreProyecto.ino` es el archivo fuente principal del proyecto. Por convención del Arduino IDE, este archivo debe llamarse igual que el
  nombre del directorio, con extensión `.ino`.
- `Fuente1.cpp` y `Fuente2.cpp` representan archivos adicionales de código fuente específicos de su proyecto.
- `Makefile` es el archivo leído por la invocación del programa `make`. Muchas de las operaciones que serán hechas en el proyecto involucran
  invocar al comando `make`, el cual leerá este archivo.
- `modules.txt` es un archivo de configuración que será leído por el comando `yubox-framework-assemble` para construir la interfaz HTML que
  será servida por el servidor web del YUBOX Framework.
- `NombreProyecto/data` es el directorio donde se arma el contenido del directorio SPIFFS del ESP32. El comando `yubox-framework-assemble`
  construye en este directorio el contenido que será servido por el servidor web del YUBOX Framework. Este directorio empieza vacío.
- `data-template` es el directorio donde se organiza el contenido de la parte de la interfaz del YUBOX Framework que es específica de su
  proyecto. En este directorio deben colocarse los módulos que definen configuraciones y visualizaciones de su proyecto, y bibliotecas
  Javascript adicionales que sean usadas por tales módulos.
- `libreria-adicional-1.js` y `libreria-adicional-2.js.gz` representan bibliotecas Javascript que serán incluidas en su proyecto. Para que
  la interfaz web realmente haga referencia a tales bibliotecas, estas deben ser declaradas en el archivo module.ini de al menos un módulo
  de su proyecto. La biblioteca ESPAsyncWebServer soporta servir una versión comprimida (con `gzip`) de un recurso si `archivo` no existe
  pero existe su correspondiente `archivo.gz`. Por lo tanto, a menos que exista una razón para lo contrario, se recomienda comprimir las
  bibliotecas externas que no son directamente parte de su desarrollo pero son dependencias de su proyecto.
- `data-template/medidorproyecto` y `data-template/otromoduloproyecto` representan módulos de la interfaz web a ser agregados para su proyecto.
- `data-template/medidorproyecto/index.htm` es un fragmento de HTML que contiene los widgets y elementos HTML necesarios para mostrar su
  interfaz web para el módulo específico del proyecto. El contenido de este HTML termina dentro de un `<div>` que se embebe en la lista de
  cejillas en la interfaz web.
- `data-template/medidorproyecto/yuboxapp.js` contiene código Javascript que será incluido como parte del archivo Javascript a ser cargado
  para la interfaz web entera. Dentro de este archivo debe declararse al menos una función Javascript dedicada a la preparación de los
  widgets del módulo (manejadores de eventos en su mayor parte). El nombre de esta función Javascript debe declararse en el archivo module.ini
  del directorio del módulo.
- `data-template/medidorproyecto/module.ini` es un archivo de configuración cuya presencia indica que el directorio `medidorproyecto` dentro
  de `data-template` es, efectivamente, un módulo. El contenido del archivo obedece el formato INI e indica varias variables a usar para
  la ejecución de `yubox-framework-assemble`. El detalle de este archivo se examina en

## Creación de un nuevo proyecto

### Archivos y directorios de proyecto

Para crear un proyecto nuevo que use YUBOX Framework, primero inicie un sketch ordinario en el Arduino IDE para el ESP32. A continuación, debe
crearse la siguiente estructura de archivos y directorios:
- `modules.txt`: este archivo contiene la lista de módulos que conformarán la interfaz web de su proyecto. El contenido de este archivo debe
  ser similar al siguiente ejemplo:
  ```
  +lector wifi ntpconfig mqtt webauth yuboxOTA
  ```
  El contenido es una sola línea de identificadores separados por espacio. Cada identificador es un módulo de interfaz web provisto por el
  YUBOX Framework o por su desarrollo del proyecto, y se corresponde con un nombre de directorio dentro del directorio `data-template`, sea
  del framework o de su proyecto. El orden en el cual los identificadores aparecen en el archivo determina el orden en el que se muestran las
  respectivas entradas de menú en la interfaz. Exactamente uno de los identificadores debe estar precedido de un signo más (+). Este identificador
  será el módulo que se mostrará por omisión al cargar la interfaz web.

  Los módulos provistos por el YUBOX Framework son los siguientes:
  - `wifi`: módulo que provee la funcionalidad de configuración de WiFi y escaneo de redes. Este módulo es casi siempre necesario.
  - `webauth`: módulo que permite administrar la contraseña de administración del dispositivo. A menos que se deje el equipo enteramente sin
    autenticación, este módulo es requerido.
  - `ntpconfig`: módulo que permite administrar la sincronización con hora de red vía NTP. Este módulo es necesario a menos que su proyecto sea
    enteramente independiente de la hora del día.
  - `yuboxOTA`: módulo que implementa la actualización de firmware a partir de una carga de archivo. También se ofrece la opción de rollback
    (si hay un firmware anterior) y un botón de simple reinicio del dispositivo.
  - `mqtt`: si está presente, este módulo permite administrar una conexión de un cliente MQTT hacia un servidor arbitrario cuyo acceso (incluyendo
    usuario y contraseña) se configura aquí. Este módulo puede omitirse si no se requiere una conexión MQTT o se usa otro mecanismo para enviar
    muestras.

- `Makefile`: este archivo es la entrada al comando `make` que inicia varias operaciones para la construcción del proyecto. El archivo más sencillo
  que sirve para el desarrollo es el siguiente:
  ```
  include $(YF)/Makefile.inc
  ```
  Este `Makefile` puede usarse con el siguiente comando:
  ```sh
  $ make YF=/ruta/a/yubox-framework/
  ```
  En esta invocación, `/ruta/a/yubox-framework/` es la ruta (relativa o absoluta) hacia su instalación de YUBOX Framework, generalmente en
  /home/SU_USUARIO/Arduino/libraries/yubox-framework/ .

  Alternativamente, puede incluirse la ruta relativa dentro del propio `Makefile` de la siguiente manera:
  ```
  include /ruta/a/yubox-framework/Makefile.inc
  ```
  De esta forma se evita tener que especificar la ruta, pero podrían introducirse dependencias en su estructura de directorios actual.
- `data-template`: el directorio que contiene los módulos personalizados para su proyecto, y cualquier biblioteca Javascript adicional requerida
  por los módulos Javascript de su proyecto.
  
  Es posible tener en el directorio `data-template` del proyecto, un módulo que se llama de forma igual que un módulo estándar del YUBOX Framework.
  En este caso, al ensamblar la interfaz web, se usará la versión del proyecto en lugar de la copia del YUBOX Framework. Del mismo modo, cualquiera
  de los archivos presentes en el directorio `data-template` del YUBOX Framework puede ser reemplazado por un archivo de nombre idéntico en el
  directorio `data-template` de su proyecto, y será usado en lugar del original. Este es un método recomendado para cambiar de colores reemplazando
  el archivo CSS, o la disposición de la interfaz en `index.htm.mustache`.


### Desarrollo cliente - JavaScript

En la parte de presentación de su proyecto, la casi totalidad de la programación estará dentro de uno o más directorios debajo de `data-template`.
Para que un directorio sea reconocido como un módulo de presentación en lugar de un archivo de datos a ser copiado directamente, el directorio
debe de tener un archivo llamado `module.ini`. A continuación se muestra un contenido típico del archivo:
```ini
[index.htm]
desc=Estado del clima
extra_jslibs=Chart-2.9.3b.min.js

[yuboxapp.js]
setupjs=setupLectorTab
```
El archivo `module.ini` sigue el formato INI (internamente leído a través de la biblioteca estándar `configparser` de Python 3) y está dividido en
secciones con claves y valores. La regla general es que cada sección tiene el nombre de un archivo a generar para el cual el YUBOX Framework
provee una plantilla Mustache. Por ejemplo, la sección llamada `[index.htm]` contiene claves a ser asignadas para el módulo cuando se procesa
la plantilla `data-template/index.htm.mustache`, sea la estándar del framework, o una plantilla personalizada del proyecto.

Las variables a asignar para cada archivo de plantilla son las siguientes:
- `[index.htm]`: variables para decorar el HTML correspondiente al módulo
  - `desc`: Etiqueta para el módulo, tal como debe de aparecer en la barra de menú superior. Valor requerido.
  - `extra_jslibs`: Lista de bibliotecas Javascript que son necesarias cargar antes de ejecutar la inicialización Javascript del módulo. Esta es
    una lista de nombres de archivo separada por espacios. Cada archivo memcionado debe de existir en el directorio `data-template` del proyecto,
    con la excepción de que si el archivo está comprimido con `gzip`, el archivo mencionado en la lista debe ser el nombre SIN el `.gz` al final.
    La presencia de esta lista es opcional.
- `[yuboxapp.js]`: variables para configurar el Javascript correspondiente al módulo
  - `setupjs`: nombre de una función Javascript que debe existir en el archivo `yuboxapp.js` del módulo. Esta función será invocada al cargar la
    página HTML, y debe encargarse de inicializar todos los eventos de los widgets del módulo, y de abrir inmediatamente o condicionalmente los
    canales de comunicación con el dispositivo. Por ejemplo, si se requiere que se abra un canal SSE al mostrar la cejilla del módulo, los
    manejadores de eventos serán instalados por esta función.
- `[yuboxapp.css]`: variables para configurar el CSS correspondiente al módulo
  - (ninguna definida en este punto por la plantilla)

Además del archivo `module.ini`, el módulo puede proveer un archivo que se corresponde a cada plantilla provista por el framework. Si el archivo
es provisto por el módulo, el contenido de este archivo se convierte en el "contenido" que el módulo provee para la interfaz:
- `index.htm` se carga y se introduce como el contenido de la cejilla elegida al navegar entre módulos en la interfaz.
- `yuboxapp.js` se carga y su contenido, concatenado con el de todos los demás módulos, contribuye al contenido del archivo `yuboxapp.js`
  cargado como parte de la interfaz web.
- `yuboxapp.css` (si existe) se carga y su contenido agrega a los estilos CSS disponibles para la interfaz web.

Refiérase al ejemplo `yubox-framework-test` para una organización típica. Los estilos y eventos disponibles en la interfaz HTML y el código
Javascript son los proporcionados por jQuery 3.5.1 y Bootstrap 4.5.0. Consulte la documentación de jQuery y Bootstrap 4 para esta referencia.
La plantilla estándar de YUBOX Framework adopta las siguientes convenciones para localizar los elementos del módulo llamado `ejemplo`:
- `div#yuboxMainTabContent > div.tab-pane#ejemplo` es el selector jQuery que identifica el panel que contiene el código HTML del módulo
  `ejemplo`, el cual se cargó originalmente del archivo `index.htm` del directorio del módulo.
- `ul#yuboxMainTab a#ejemplo-tab[data-toggle="tab"]` es el selector jQuery que identifica el widget de navegación que recibe los eventos
  de panel mostrado y ocultado para el módulo `ejemplo`. Refiérase a la documentación en https://getbootstrap.com/docs/4.4/components/navs/#events
  para los eventos `shown.bs.tab` y `hide.bs.tab` que deben ser manejados en caso necesario para tomar acción al mostrar u ocultar
  el módulo.

La plantilla estándar de YUBOX Framework define también las siguientes funciones para operaciones estándar de módulos:
- `yuboxAPI(s)` introduce una formato estándar para el punto de entrada de API de un módulo. La invocación de `yuboxAPI('ejemplo')+'/config.json'`
  se expande actualmente a `/yubox-api/ejemplo/config.json`. El código C++ de Arduino debe entonces instalar un manejador para esta ruta.
- `yuboxMostrarAlert(alertstyle, content, timeout)` y `yuboxMostrarAlertText(alertstyle, text, timeout)` muestran una caja de alerta de
  Bootstrap en la parte superior de la interfaz, justo debajo del menú de módulos. El valor de `alertstyle` es uno de los estilos de
  alerta estándar de Bootstrap: `primary`, `secondary`, `success`, `warning`, `danger`, `info`, `light`, `dark`. El parámetro `timeout`
  es opcional e indica el número de milisegundos luego del cual quitar la alerta. El parámetro `content` indica un HTML arbitrario a
  insertar, mientras que `text` indica un texto corriente.
- `yuboxDlgMostrarAlert(basesel, alertstyle, content, timeout)` y `yuboxDlgMostrarAlertText(basesel, alertstyle, text, timeout)` son
  similares a `yuboxMostrarAlert` y `yuboxMostrarAlertText`, pero muestran una caja de alerta en el interior de un diálogo modal
  identificado por `basesel`.
- `yuboxStdAjaxFailHandler(e, timeout)` y `yuboxStdAjaxFailHandlerDlg(basesel, e, timeout)` son especializaciones que muestran el contenido
  de un error o excepción dentro de `e`, en la parte superior de la interfaz, o dentro de un diálogo modal, respectivamente.

### Desarrollo server - Arduino C++

### Transferencia de sketch al ESP32

## Configuración básica usando interfaz web

### Ingreso a red softAP y configuración WiFi

### Credenciales y cambio de clave

### Configuración NTP

### Actualización de firmware

### Configuración MQTT (según proyecto)

