# Архитектура NovaDroid (ТЗ 4)

```
Windows 11 x64
|
+-- NovaDroid Launcher (NovaDroidLauncher.exe, C++17 / Win32 + GDI)
|   +-- ui.cpp      каркас окна, тёмная тема, sidebar, Home/Instances
|   +-- pages.cpp   APK / Файлы / Раскладка / Производительность /
|   |               Диагностика / Логи / Настройки / О программе
|   +-- dialogs.cpp диалоги создания и настройки инстанса
|   +-- core.cpp    настройки (settings.json), менеджер инстансов (config.json),
|   |               логирование, статусы (ТЗ 6.2)
|   +-- backend.cpp сборка аргументов QEMU, старт/стоп VM, ADB-обёртка,
|   |               диагностика (ТЗ 6.10)
|   +-- util.cpp    процессы с перехватом stdout/stderr, файловые операции,
|   |               ярлыки, буфер обмена
|   +-- json.cpp    минимальный JSON-парсер/сериализатор (без зависимостей)
|
+-- QEMU (qemu-system-x86_64.exe, внешний компонент)
|   +-- -accel whpx  (Windows Hypervisor Platform) / fallback -accel tcg
|   +-- q35, virtio-net (NAT + hostfwd tcp:127.0.0.1:<порт> -> :5555)
|   +-- VGA/virtio-vga, intel-hda, qemu-xhci + usb-tablet
|   +-- serial -> instances/<id>/logs/serial.log
|
+-- Android Guest (Android-x86 x86_64, ISO или установленный диск)
    +-- adbd (порт 5555 внутри гостя, проброшен на localhost)
    +-- управляется через adb: install / push / pull / input keyevent / screencap
```

## Ключевые решения

1. **QEMU не встраивается и не модифицируется.** Лаунчер запускает внешний
   `qemu-system-x86_64.exe` (поиск: путь из настроек -> `app\qemu\` -> PATH).
   Это снимает обязательства по модификации GPL-кода.
2. **ADB localhost-only** (ТЗ 9): `hostfwd=tcp:127.0.0.1:<port>-:5555` — порт
   слушается только на loopback-интерфейсе.
3. **Монитор процессов:** лаунчер держит HANDLE процесса QEMU; отдельный поток
   ждёт завершения и через `WM_APP_QEXIT` обновляет статус (ТЗ 6.2).
   Остановка: `adb shell reboot -p` -> ожидание 20 с -> TerminateProcess.
4. **Хранение:** один каталог на инстанс, `config.json` по схеме ТЗ 8.2
   (плюс imagePath/bootMode/diskFormat). Резервная копия config.json пишется
   как config.json.bak перед каждой записью (ТЗ 11.2).
5. **UI:** полностью собственная отрисовка (GDI, двойная буферизация),
   перманентно тёмная тема из ТЗ 7.1, Per-Monitor DPI v2 из манифеста.
6. **Диагностика (ТЗ 6.10):** версия Windows, RAM, свободное место,
   VT-x/AMD-V (IsProcessorFeaturePresent), WHPX (LoadLibrary WinHvPlatform.dll
   + наличие экспортов), служба Hyper-V, наличие QEMU/ADB, поддержка WHPX
   сборкой QEMU (`qemu-system-x86_64 -accel help`), Android-образ, GPU.
