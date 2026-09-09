# Интеграция ADB (ТЗ 5.7, 6.5, 6.8)

## Подключение

- Сериал инстанса: `127.0.0.1:<adbPort>` (порт задаётся при создании,
  база 5555 из настроек; каждый инстанс получает свой порт).
- `adb connect 127.0.0.1:<port>` (3 попытки), затем опрос `get-state`
  каждые 2 с до 120 с — поток готовности в StartInstance.
- После подключения читаются `ro.build.version.release` и `ro.product.cpu.abi`
  -> поле androidVersion в config.json («Android 9 (x86_64)»).

## Команды, используемые лаунчером

| Функция | Команда |
|---|---|
| Установка APK | `adb -s <serial> install -r -g "<apk>"` |
| Передача файла в Android | `adb -s <serial> push "<file>" /sdcard/Download/` |
| Забрать загрузки | `adb -s <serial> pull /sdcard/Download/ "<shared>"` |
| Скриншот (бинарно) | `adb -s <serial> exec-out screencap -p > file.png` |
| Назад / Домой / Недавние | `input keyevent 4 / 3 / 187` |
| Громкость + / - | `input keyevent 24 / 25` |
| Поворот экрана | `input keyevent 82` |
| Перезагрузка гостя | `adb shell reboot` |
| Выключение гостя | `adb shell reboot -p` |
| Свойства | `getprop <prop>` |

## Безопасность (ТЗ 9)

- hostfwd привязан к 127.0.0.1 — ADB недоступен из сети.
- ADB не включается в режим TCP для внешних интерфейсов.
- Установка APK: проверяется только расширение `.apk`; предупреждение о
  недоверенных APK выводится в уведомлении при ошибке (MVP).

## Требования к гостю

Внутри Android-x86 adbd должен слушать порт 5555 (в Android-x86 обычно включено
по умолчанию в отладочных сборках). Если ADB не подключается:
1. Убедиться, что QEMU запущен и ОС загрузилась.
2. Проверить `adb devices` вручную.
3. Проверить hostfwd-порт в config.json.
4. См. troubleshooting.md.
