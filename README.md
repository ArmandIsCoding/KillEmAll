# ![Kill ’Em All — Costa industrial](docs/media/portada.png)

Un *bullet hell* de desplazamiento horizontal hecho en **C++17 y SFML 3.1**. Tenés tres vidas, un avión remachado y una costa industrial que sigue pasando bajo las alas. El cañón dispara solo. Vos elegís entre perseguir una formación para sumar puntos o dejarla pasar para colarte entre sus balas.

[Compilar y jugar en Windows](#compilar-en-visual-studio) · [Controles](#controles)

![El avión esquiva proyectiles fucsias sobre el puerto. Al llegar a potencia cinco, sus disparos dorados se abren en abanico y los verdes estallan en el aire.](docs/media/combate.gif)

## Los primeros 45 segundos

El arma mejora por **tiempo de vuelo**. Derribar enemigos suma puntos. La costa sigue de largo mientras te queden vidas.

| Tiempo en vuelo | Qué sale del cañón |
| --- | --- |
| Al despegar | Un disparo dorado hacia adelante. |
| 8 s | Salen tres proyectiles dorados por salva, cada uno con un ángulo que varía de un tiro al siguiente. |
| 18 s | El cañón encadena dos salvas por ráfaga. |
| 30 s | Se suman dos proyectiles verdes que zigzaguean y detonan al tocar un enemigo o tras casi un segundo de vuelo. La explosión también alcanza a los que vuelan cerca. |
| 45 s | Potencia máxima. Cinco proyectiles dorados por salva, ráfagas triples y proyectiles ondulantes. |

De vez en cuando cruza la pantalla un **potenciador turquesa con dos flechas**. Tocá uno con tu avión para multiplicar por **1,5** la cantidad de proyectiles que disparás por segundo. Las mejoras se acumulan hasta el fin de la partida. Con dos potenciadores, la cadencia es **2,25 veces** la habitual para ese nivel de potencia. Cuantos más recogés, más tarda en aparecer el siguiente.

Al despegar, los tiros salen hacia adelante. Algunos dorados se curvan en pleno vuelo y dejan una estela que se afina y se apaga detrás de ellos. Cuantos más proyectiles disparás por segundo, más frecuentes y pronunciados se vuelven esos arcos y más brillan sus estelas. También aumenta la variedad de ángulos de salida. Los verdes siguen su propio zigzag.

## Controles

| Tecla | Acción |
| --- | --- |
| **WASD** o **flechas** | Moverte en ocho direcciones. |
| **Shift** (mantener) | Volar más despacio y ver el punto vulnerable del avión. Guiá ese punto por los huecos entre las balas fucsias. |
| **P** | Pausar o reanudar. |
| **M** | Silenciar o activar la música y los efectos. |
| **Enter** o **R** | Volver a despegar después de perder las tres vidas. |
| **Esc** | Salir del juego. |

## Tráfico sobre el puerto

![De izquierda a derecha, tu avión, el explorador rojo de 100 puntos, el interceptor de 200 y el blindado de 500.](docs/media/hangar.png)

El **explorador** apunta a tu posición. El **interceptor** dispara en abanicos de tres. El **blindado** llega escoltado y alterna el ángulo de sus abanicos de siete proyectiles. El hueco por el que pasaste la primera vez cambia de lugar en la siguiente descarga. Cuando cae un blindado, la explosión grave sacude la cámara por un instante y un relámpago ilumina la costa.

## Compilar en Visual Studio

Necesitás **Git** y **Visual Studio 2026** con la carga *Desarrollo para el escritorio con C++*. Incluí MSVC para x64, el SDK de Windows y las herramientas de CMake para Windows. El generador de Visual Studio 2026 requiere **CMake 4.2 o posterior**.

Desde **Developer PowerShell for VS 2026**, cloná el proyecto y generá la solución. La primera configuración necesita conexión para descargar SFML 3.1.0 y sus dependencias.

```powershell
git clone https://github.com/ArmandIsCoding/KillEmAll.git
cd KillEmAll
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
Invoke-Item .\build\KillEmAll.slnx
```

En Visual Studio, presioná **F5**. Si preferís jugar directamente desde la terminal, estos comandos compilan y ejecutan la versión Release desde la raíz del proyecto.

```powershell
cmake --build build --config Release --target KillEmAll
.\build\Release\KillEmAll.exe
```

La compilación coloca la carpeta `assets` junto al ejecutable. Si movés el juego, llevá ambos. Para arrancar en silencio desde la raíz del proyecto, usá `.\build\Release\KillEmAll.exe --mute`.

## Dentro del juego

Para cambiar cuándo aparecen las formaciones o cuántos proyectiles salen por salva, empezá por el [sistema de combate](src/Combat.cpp). El [renderizador](src/BattleRenderer.cpp) dibuja ese combate con sprites, humo y esquirlas. El [jugador](src/Player.cpp) se encarga de las poses y del vuelo de precisión.

El proyecto se distribuye bajo la [licencia MIT](LICENSE). La tipografía **Press Start 2P** incluye su [licencia SIL Open Font License](assets/arcade/font/OFL.txt).
