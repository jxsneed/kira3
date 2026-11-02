KIRA v1.4 - efecto vocal VST3 (Vocal Bend + Auto-Tune + Smile) para Windows 10/11

QUE ES
Plugin VST3 (y Standalone) hecho con JUCE/CMake. DSP original:
  - Deteccion de tono YIN (monofonico, ~0.1 cents de error en tonos limpios).
  - Pitch shifter de 2 cabezales con ventanas sincronizadas al periodo
    (0.0 cents de error medido de -12 a +12 semitonos).
  - Autotune con cuantizacion a escala, histeresis de nota, velocidad de retune,
    referencia A4, rechazo de errores de octava y liberacion suave en consonantes
    y finales de nota.
  - SMILE por etapas: brillo y aire, halo de voces ascendentes, reverb.

INTERFAZ
  Estetica "Wired": cables de tension, pantalla CRT con scanlines y grano, glitch
  ocasional, un ojo animado que se abre cuando detecta voz y un monitor de afinacion
  en tiempo real (cian = nota detectada, rojo = nota corregida, ~5 s de historial).

CONTROLES
BEND   -12..+12 semitonos, continuo y automatizable.
TUNE   cantidad de correccion hacia la nota de la escala (0-100 %).
SPEED  velocidad de retune: ~300 ms (natural) a ~4 ms (efecto duro robotico).
VIBE   vibrato real (modulacion de afinacion) + deriva lenta.
SMILE  macro por etapas (0 = transparente):
         0-50 %   voz mas abierta: aire (+5 dB a 7 kHz), presencia (+2.5 dB a 3.2 kHz)
                  y excitador armonico en agudos. No toca los graves ni adelgaza la voz.
         12-100 % halo de voces ascendentes con eco (+quinta, +octava, +octava y quinta),
                  siempre afinadas con la voz corregida y repartidas en estereo.
         35-100 % floracion de reverb. El ojo del logo sonrie al subirlo.
MIX    seco/humedo (el seco se compensa en latencia).
A4     referencia de afinacion (430-450 Hz, defecto 440).
KEY / SCALE  tonica y escala: Chromatic, Major, Minor, Major Pent, Minor Pent, Harm Minor.

AJUSTES RAPIDOS
  Natural:       TUNE 40-60, SPEED 35-55, VIBE 15
  Efecto duro:   TUNE 100, SPEED 100, SCALE = la de tu cancion
  Bender:        TUNE 0 y mueve BEND (automatizalo en Ableton)
  Smile:         sube SMILE de 0 a 100: primero brillo, luego halo ascendente, luego reverb

LATENCIA
~37 ms (se informa al host; Ableton la compensa solo con PDC).

COMPILAR
Lo mas facil: sigue LEEME_GITHUB.txt (GitHub compila por ti y deja el zip en Releases).
Local: cmake -B build -S . -G "Visual Studio 17 2022" -A x64  y  cmake --build build --config Release

LIMITACIONES
  - Monofonico: pensado para una voz limpia y sola; con coros, ruido o audio
    polifonico habra artefactos.
  - Sin preservacion de formantes: BEND/correcciones grandes cambian el timbre
    (efecto "ardilla" al subir mucho).
  - No es un Auto-Tune comercial; es un motor propio. Nombres como Vocal Bender,
    Auto-Tune o Endless Smile pertenecen a sus respectivos autores: KIRA solo
    imita el tipo de efecto, no copia su codigo.
  - Probado numericamente con un arnes (sin JUCE real). La compilacion con JUCE 8.0.6
    debe verificarse en tu primera build.
