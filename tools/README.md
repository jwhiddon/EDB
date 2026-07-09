# EDB Tools

## edb_migrate.py

Converts legacy EDB v1 database files to the packed v2 on-disk format used by EDB 2.0.0.

### Usage

```bash
python tools/edb_migrate.py input.db output.db --arch avr
python tools/edb_migrate.py input.db output.db --arch esp32
python tools/edb_migrate.py input.db output.db --arch auto
python tools/edb_migrate.py input.db output.db --force
```

### Options

- `--arch avr|esp32|auto` — legacy header layout (`auto` tries known layouts)
- `--force` — overwrite an existing output file

### Standalone executable

```bash
pip install pyinstaller
pyinstaller --onefile tools/edb_migrate.py
```

The executable is written to `dist/edb_migrate.exe` on Windows or `dist/edb_migrate` on Linux/macOS.

### Tests

```bash
python -m unittest discover -s tools -p "test_*.py"
```
