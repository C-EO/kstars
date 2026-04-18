# LLDB Formatters for KStars

This directory contains LLDB data formatters (pretty printers) for debugging KStars with LLDB.

## Files

- **all.py** - Main entry point that loads all formatter modules
- **qt6.py** - Qt6 type formatters (QString, QVector, etc.)
- **kde.py** - KDE type formatters (KTextEditor::Cursor, KDevelop::Path, etc.)
- **kstars.py** - KStars-specific type formatters
- **helpers.py** - Helper functions used by other formatters

## Usage

The formatters are automatically loaded when debugging with the "Debug KStars (LLDB)" configuration in VS Code. They are loaded via the `initCommands` in `.vscode/launch.json`:

```json
"initCommands": [
    "command script import ${workspaceFolder}/.lldb/formatters/all.py",
    ...
]
```

## KStars Custom Formatters

The `kstars.py` module provides formatters for astronomy-specific types:

### Supported Types

- **dms** - Displays angles in degrees with degree symbol
- **CachingDms** - Same as dms (inherits from it)
- **SkyPoint** - Displays celestial coordinates (RA and Dec)
- **SkyObject** - Shows object name and coordinates
- **GeoLocation** - Shows location name with latitude/longitude

### Example Output

Instead of seeing raw memory addresses and internal fields, you'll see:
- `dms`: `45.5°`
- `SkyPoint`: `RA: 83.75° Dec: 22.01°`
- `SkyObject`: `Betelgeuse (RA: 88.79° Dec: 7.41°)`
- `GeoLocation`: `Greenwich (Lat: 51.48°, Lon: 0.0°)`

## Extending

To add formatters for additional KStars types, edit `kstars.py`:

1. Create a summary provider function that takes `(valobj, internal_dict)` parameters
2. Register it in the `__lldb_init_module()` function using `type summary add`
3. Enable the category with `type category enable kstars`

For more information on LLDB data formatters, see:
https://lldb.llvm.org/use/variable.html#type-summary

## License

SPDX-License-Identifier: GPL-2.0-or-later
