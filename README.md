# NovaDroid Emulator

**NovaDroid Emulator** — экспериментальный Android x86_64 launcher и менеджер виртуальных
Android-инстансов для Windows 11. Проект использует QEMU, аппаратную виртуализацию Windows
(WHPX/Hyper-V) и ADB для запуска и управления Android.

> Версия 1.0.0 MVP · Язык интерфейса: Русский / English · Код лаунчера: C++17 / Win32

## Ключевые возможности

- Android x86_64 в виртуальной машине (QEMU + WHPX, fallback TCG)
- Современный тёмный интерфейс в стиле игровых лаунчеров (#12151C / #6C63FF)
- Управление несколькими инстансами (создание, клонирование, удаление, бэкапы, экспорт/импорт)
- Установка APK кнопкой и Drag & Drop
- ADB-интеграция: подключение (только localhost), logcat-альтернативы, кнопки Назад/Домой/Недавние,
  громкость, скриншот, поворот
- Настройки CPU, RAM, диска, разрешения, DPI, FPS, режима GPU
- Диагностика виртуализации и компонентов (ТЗ 6.10)
- Логи лаунчера, QEMU, ADB и serial-вывода с автоочисткой (>14 дней)
- Обмен файлами с Android через общую папку (ADB push/pull)
- Ярлыки инстансов на рабочем столе (`NovaDroidLauncher.exe --launch instance-001`)

## Быстрый старт

1. **QEMU**: скопируйте `qemu-system-x86_64.exe` и `qemu-img.exe` в папку `app\qemu\`
   (например, из сборки QEMU для Windows: https://qemu.org или gyan.dev).
2. **ADB**: скопируйте содержимое Android SDK Platform Tools в `app\adb\`
   (https://developer.android.com/tools/releases/platform-tools).
3. **Android-образ**: скачайте ISO Android-x86 x86_64 (https://www.android-x86.org)
   и положите в `%LOCALAPPDATA%\NovaDroid\images\`.
4. **Включите виртуализацию**:
   - В BIOS/UEFI включите Intel VT-x или AMD SVM.
   - В компонентах Windows включите «Windows Hypervisor Platform»
     (`DISM /online /enable-feature /featurename:HypervisorPlatform`).
   - Перезагрузите ПК.
5. Запустите `app\NovaDroidLauncher.exe`. Диагностика подскажет, если что-то не включено.
6. Нажмите «Создать инстанс», выберите ISO, режим загрузки «ISO», затем «Запустить».
7. Установите Android-x86 на виртуальный диск прямо из гостевого установщика,
   после чего в настройках инстанса переключите режим загрузки на «Диск».

## Компиляция из исходников (Windows)

См. `docs/build-windows.md`. Кратко (MinGW-w64, GCC 14):

```
windres resources/app.rc -O coff -o resources/res.o
g++ -std=c++17 -O2 -municode -mwindows src\*.cpp resources\res.o ^
    -o NovaDroidLauncher.exe ^
    -lgdi32 -lshell32 -lshlwapi -lcomdlg32 -lole32 -ladvapi32 -ldwmapi ^
    -lmsimg32 -lpsapi -lcomctl32 -luuid -luxtheme -static -s
```

## Структура данных (ТЗ 8.1)

Данные по умолчанию: `%LOCALAPPDATA%\NovaDroid\`
`instances\<id>\config.json + disk.qcow2 + logs\ + shared\ + snapshots\`,
`images\`, `backups\`, `logs\`, `cache\`, `database\`, `apks\`, `screenshots\`.

## Этап 3 — Gaming Core (v0.3.0)

* **GPU-ускорение**: режимы Auto / Host / SwiftShader / Software, virtio-gpu-gl-pci
  (virgl) + `-display gtk,gl=on`, автоматический fallback и понятные сообщения
  (ТЗ 3.0 §6.1–6.4).
* **FPS**: счётчик с frame time (полупрозрачный оверлей в углу окна Android),
  ограничитель 30/45/60/90/120/без ограничения (применяется к гостю через
  refresh-rate settings после загрузки ADB).
* **Окно**: полноэкранный режим (F11 / Alt+Enter, Esc — выход), режим без рамки,
  «поверх всех окон», стабильное сворачивание/разворачивание без остановки VM.
* **Мышь и клавиатура**: захват/освобождение курсора (по умолчанию RCtrl,
  настраивается), чувствительность X/Y, инверсия Y.
* **Звук**: intel-hda + hda-duplex (dsound), громкость 0–100, mute,
  авто-mute при сворачивании окна.
* **Профили производительности**: Экономный / Сбалансированный / Игровой /
  Игровой 120 FPS (ТЗ 3.0 §11) — применяются к инстансу одним кликом.
* **Быстрый старт**: минимум проверок перед запуском, цель 10–40 с (SSD).
* **Crash recovery (ТЗ 3.0 §13)**: анализ аварийного выхода QEMU по коду,
  watchdog зависаний (~30 с без ответа окна), диалог восстановления:
  запустить снова / восстановить последний снапшот / логи / отчёт ZIP
  (крэш-репорты в `%LOCALAPPDATA%\NovaDroid\crashes`).
* **Диагностика GPU**: модель, драйвер, WHPX, VT-x, Vulkan, OpenGL ICD,
  virgl-возможности QEMU, автоматический выбор режима.

## Полный пакет и прямые ссылки (bootstrap)

При первом запуске лаунчер **обязан** скачать полный пакет (QEMU + ADB + образ
Android x86_64 + документация), проверить его и только после этого разрешить
запуск эмулятора. Порядок:

1. Проверяется маркер `.novadroid-pkg.json` и критические файлы
   (`qemu\qemu-system-x86_64.exe`, `adb\adb.exe`, `images\*.iso`).
2. Если пакет не установлен — открывается мастер загрузки. Источники берутся:
   * из `downloads.json` рядом с exe (приоритет):
     `{"urls":[{"url":"https://vikingfile.com/d/<hash>/<файл>.zip","sizeBytes":1041410801,"sha256":"..."}]}`
   * из встроенного списка прямых ссылок vikingfile;
   * либо «ZIP офлайн» — пользователь указывает уже скачанный ZIP вручную.
3. ZIP проверяется (SHA-256, если задан; точный размер; внутренний
   `package.json` с SHA-256 каждого файла), распаковывается `tar.exe`
   (старый exe исключается) и помечается как установленный.

Прямая ссылка на пакет 0.3 (страница):
https://vikingfile.com/f/BfegZGDp8b
Чтобы получить постоянную прямую ссылку формата
`https://vikingfile.com/d/<hash>/<имя>.zip` — откройте страницу в браузере
и нажмите Download (ссылка вида /d/ копируется из кнопки), затем пропишите
её в `downloads.json` рядом с exe.

## Статус

v0.3.0 Gaming Core: мультиинстансы, клоны, снапшоты, бэкапы, APK-библиотека,
раскладки, perf-центр, GPU-ускорение, FPS, звук, fullscreen, crash recovery,
bootstrap полного пакета.

## Лицензии

Код лаунчера — MIT (см. LICENSE). Используются сторонние компоненты с их собственными
лицензиями — см. THIRD_PARTY_LICENSES.txt. При распространении вместе с QEMU (GPLv2+)
выполняйте условия GPL: публикуйте исходные коды изменённых версий QEMU.
