# Сборка NovaDroid Launcher под Windows

## Вариант A. MinGW-w64 (GCC) — рекомендованный для воспроизведения

### Требования
- Windows 10/11 x64
- MinGW-w64 GCC 13+ (например, из MSYS2: `pacman -S mingw-w64-ucrt-x86_64-gcc`
  или из сборки niXman / WinLibs)

### Сборка
```bat
cd NovaDroid
windres resources\app.rc -O coff -o resources\res.o

g++ -std=c++17 -O2 -municode -mwindows ^
    src\main.cpp src\ui.cpp src\pages.cpp src\dialogs.cpp ^
    src\core.cpp src\backend.cpp src\util.cpp src\json.cpp src\strings.cpp ^
    resources\res.o ^
    -o build\NovaDroidLauncher.exe ^
    -lgdi32 -lshell32 -lshlwapi -lcomdlg32 -lole32 -ladvapi32 -ldwmapi ^
    -lmsimg32 -lpsapi -lcomctl32 -luuid -luxtheme ^
    -static -static-libgcc -static-libstdc++ -s
```

Флаги:
- `-municode` — входная точка `wWinMain`, полная поддержка Unicode (русский язык);
- `-mwindows` — GUI-подсистема (без консоли);
- `-static*` — статическая линковка рантайма, exe зависит только от системных DLL.

### Кросс-компиляция из Linux
Пакеты: `binutils-mingw-w64-x86-64 mingw-w64-common mingw-w64-x86-64-dev
gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix`

```bash
x86_64-w64-mingw32-windres resources/app.rc -O coff -o resources/res.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -municode -mwindows src/*.cpp \
    resources/res.o -o build/NovaDroidLauncher.exe \
    -lgdi32 -lshell32 -lshlwapi -lcomdlg32 -lole32 -ladvapi32 -ldwmapi \
    -lmsimg32 -lpsapi -lcomctl32 -luuid -luxtheme -static -s
```

## Вариант B. Microsoft Visual Studio (альтернатива)

1. Создайте пустой проект «Desktop Application (C++)».
2. Добавьте все файлы из `src\`.
3. Свойства → Linker → System → SubSystem: **Windows**;
   Linker → Input: `gdi32.lib;shell32.lib;shlwapi.lib;comdlg32.lib;ole32.lib;
   advapi32.lib;dwmapi.lib;msimg32.lib;psapi.lib;comctl32.lib;uxtheme.lib`.
4. C/C++ → Language → C++17; Character Set: **Unicode**.
5. Добавьте `resources\app.manifest` (Manifest Tool → Input and Output) и
   `resources\icon.ico` как ресурс.

## Проверка сборки

- `objdump -p NovaDroidLauncher.exe | grep "DLL Name"` — только системные DLL.
- Тест JSON-парсера (кроссплатформенный): скомпилируйте `scripts/test_json.cpp`
  вместе с `src/json.cpp` любым компилятором C++17 и запустите.
