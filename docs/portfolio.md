# Demo i materiały do portfolio

## Scenariusz 90 sekund

| Czas | Co pokazać | Co powiedzieć |
| --- | --- | --- |
| 0–15 s | Pi 3 i diagram w README | „To monitor w C, uruchamiany jako usługa systemd we własnym obrazie Yocto dla Raspberry Pi 3.” |
| 15–35 s | `scripts/smoke-test.sh live "$pi"` i ostatni JSON | „Te dane pochodzą z sysfs i procfs. Czytelnik dostaje pełną próbkę dzięki atomowej podmianie pliku.” |
| 35–55 s | Wykonany wcześniej log z `SIGSTOP`, `Watchdog timeout` i nowym `InvocationID` | „Po wygaśnięciu heartbeat systemd uruchomił monitor ponownie. Limit startów zatrzymuje pętlę awarii.” |
| 55–75 s | `sources.lock`, receptura aplikacji i biblioteki | „Buduję na przypiętych rewizjach. Biblioteka współdzielona ma własną recepturę, a Yocto wykrywa jej zależność uruchomieniową.” |
| 75–90 s | Zielony GitHub Actions i tabela prób | „CTest weryfikuje aplikację na hoście. Rozruch, Wi-Fi i recovery potwierdziłem na Raspberry Pi.” |

Do pokazu przygotuj działające SSH, widok GitHub Actions i krótki
fragment udokumentowanego logu. Usuń z nagrania lokalne ścieżki,
identyfikatory sieci i dane uwierzytelniające.

## CV

**RPi Sentinel — Embedded Linux health monitor | C, Yocto, BitBake, systemd**

- Built a custom AArch64 Yocto image for Raspberry Pi 3 with pinned build
  inputs and separate recipes for a C daemon and its shared metrics library.
- Implemented Linux telemetry, atomic JSON status updates, and systemd
  watchdog recovery with restart rate limiting.
- Verified boot, live measurements, and failure recovery on Raspberry Pi 3.
  Automated four host-side CTest scenarios with GitHub Actions.

## LinkedIn

**RPi Sentinel · Embedded Linux and Yocto project**

I built a Raspberry Pi 3 health monitor from the C application through
Yocto packaging and on-device validation. The daemon reads Linux
temperature, memory, and uptime metrics, exports an atomic JSON status
file, and uses systemd watchdog notifications for service recovery.
I pinned the build inputs, packaged the metrics library independently,
and verified trigger and recovery paths on hardware. GitHub Actions
runs the host build and tests, and the repository documents how to build,
deploy, and troubleshoot the image.
