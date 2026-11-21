# NA_Magicos – edición de Dussek

Cambios de este fork para el canal **@NA_Magicos**, sobre el código original de
[ccoors/dussek](https://github.com/ccoors/dussek) (código AGPL-3.0; instrumentos CC BY 4.0).

## Tema de video "Art déco dorado"

`midi2wav` / `dussek render` aceptan `--theme deco` junto a `--video`: fondo negro con resplandor cálido,
rayos de sol art déco, doble marco dorado con esquinas escalonadas, cuerdas de cobre a oro y perla,
teclas de marfil y ébano con filo dorado. `--theme classic` (por defecto) deja el visual original.

![Tema deco](tema-deco.png)

```
dussek render instruments/grand_b pieza.mid pieza.wav --video pieza.mp4 --theme deco
```

## Presets (`presets/`)

Copiar a `%APPDATA%\Dussek\presets\` (Windows):

- **NA Magicos - Nocturno calido**: Grand B, micrófonos de audiencia, martillo más suave, sustain 115 %.
- **NA Magicos - Lofi intimo**: Upright C, martillo muy suave, poca sala.

Ambos con una curva de velocidad para el Yamaha YPG-235 (teclas sin peso).

## Scripts

- `render-namagicos.ps1`: MIDI → WAV a −14 LUFS + video (horizontal, Short "Dos mundos" con el clip del
  personaje, o transparente), opcionalmente con pad (`-Pad`) y limitador (`-Limitar`). Usa el tema Deco
  si el `dussek.exe` lo trae; si no, el clásico.
- `pad_from_midi.py`: genera un pad sincronizado a partir de los acordes del MIDI y lo mezcla bajo el piano
  (`py -3 -m pip install numpy scipy mido soundfile`).
