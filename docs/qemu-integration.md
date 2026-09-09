# Интеграция QEMU (ТЗ 5.1, 6.x)

## Поиск исполняемого файла

Порядок: путь из настроек (`settings.json: qemuPath`) -> `app\qemu\qemu-system-x86_64.exe`
(рядом с лаунчером) -> поиск в PATH. `qemu-img.exe` ищется рядом с qemu-system.

## Формирование командной строки (backend.cpp: BuildQemuArgs)

```
qemu-system-x86_64
  -machine q35
  -accel whpx            # если WinHvPlatform.dll доступна, иначе -accel tcg
  -cpu host              # для whpx; для tcg -cpu max
  -smp <cpuCores>
  -m <ramMb>
  # режим ISO (live/установка):
  -drive file="<imagePath>",media=cdrom,if=ide  -boot d
  [-drive file="instances/<id>/disk.qcow2",format=qcow2,if=ide,index=1]
  # режим Диск:
  -drive file="instances/<id>/disk.qcow2",format=qcow2,if=ide,index=0  -boot c
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:<adbPort>-:5555
  -device virtio-net-pci,netdev=net0
  -device VGA,edid=on,xres=<resW>,yres=<resH>   # или virtio-vga
  [-audiodev dsound,id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0]
  -device qemu-xhci -device usb-tablet
  -rtc base=localtime
  -serial file:"instances/<id>/logs/serial.log"
  [-fullscreen]
  -name "NovaDroid - <name>"
```

## Создание диска

`qemu-img create -f qcow2 "disk.qcow2" <diskGb>G`. Если qemu-img недоступен —
создаётся разреженный RAW-файл (SetFilePointerEx + SetEndOfFile), формат меняется
на `raw` в config.json.

## WHPX: проверка и включение

Проверка (WhpxAvailable): LoadLibraryW("WinHvPlatform.dll") + GetProcAddress
(WHvCreateHandle / WHvGetCapability). Дополнительно диагностика запускает
`qemu-system-x86_64 -accel help` и проверяет, что сборка QEMU поддерживает whpx.

Включение на Windows 11:
1. BIOS/UEFI: Intel VT-x или AMD SVM.
2. `DISM /online /enable-feature /featurename:HypervisorPlatform` или
   «Компоненты Windows» -> Windows Hypervisor Platform.
3. Перезагрузка.

## Логи

- stdout/stderr QEMU -> `instances/<id>/logs/qemu-YYYYMMDD.log` и `qemu-err-*.log`
- serial -> `serial.log`
- события лаунчера -> те же файлы через ILog()

## Ограничения MVP

- FPS-лимит хранится в config.json, но QEMU не имеет прямого лимитера кадров.
- Режимы «OpenGL/Vulkan» отображены как VirtIO GPU / VGA (host-GL passthrough
  на Windows в QEMU отсутствует).
- Клонирование — полное копирование диска (linked clones/qemu-img rebase — в v2).
