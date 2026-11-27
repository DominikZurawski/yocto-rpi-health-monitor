# Opcja B — moduł kernela out-of-tree

Źródła modułu znajdują się w naszej warstwie Yocto, poza drzewem źródeł
Linuksa. Kbuild kompiluje plik `.ko` dla konfiguracji i wersji kernela
używanego na Pi. Podczas `modprobe` moduł zaczyna działać w przestrzeni
kernela. Błąd w tym kodzie może wpłynąć na całe urządzenie, dlatego
testujemy na oddzielnym obrazie i używamy ręcznego ładowania.

## Pliki do przejrzenia

| Plik | Rola |
| --- | --- |
| [rpi_sentinel_demo.c](../meta-rpi-sentinel/recipes-kernel/rpi-sentinel-demo/files/rpi_sentinel_demo.c) | Funkcja inicjalizacji tworzy katalog i atrybut sysfs. Funkcja odczytu podaje milisekundy z zegara monotonicznego, a funkcja wyjścia usuwa atrybut przed zwolnieniem obiektu. |
| [Makefile](../meta-rpi-sentinel/recipes-kernel/rpi-sentinel-demo/files/Makefile) | `obj-m` określa moduł. `KERNEL_SRC` wskazuje drzewo kernela, a `M` wskazuje zewnętrzne źródła. |
| [Receptura modułu](../meta-rpi-sentinel/recipes-kernel/rpi-sentinel-demo/rpi-sentinel-demo_1.0.bb) | `inherit module` przekazuje do Kbuild narzędzia kompilacji skrośnej i artefakty docelowego kernela oraz pakuje wynik. |
| [Reguła sudo obrazu testowego](../meta-rpi-sentinel/recipes-core/rpi-sentinel-demo-test-access/files/rpi-sentinel-demo.sudoers) | Konto `developer` może załadować i usunąć tylko `rpi_sentinel_demo`. |

## Budowa i sprawdzenie pakietu

W zainicjalizowanym środowisku Yocto uruchom:

```bash
bitbake rpi-sentinel-demo rpi-sentinel-demo-test-access &&
oe-pkgdata-util read-value RDEPENDS rpi-sentinel-demo
```

Oczekiwany wynik to pakiet `kernel-module-rpi-sentinel-demo-*`.
`modinfo -F vermagic ŚCIEŻKA_DO_PLIKU_KO` pozwala odczytać wersję i
cechy kernela, dla którego powstał moduł. `vermagic` pomaga przy
diagnostyce zgodności. Moduł budujemy z artefaktami tego samego kernela,
który trafia do obrazu.

**Wynik budowania.** Obie receptury przeszły QA. Pakiet
`kernel-module-rpi-sentinel-demo-6.18.33-v8` zawiera
`rpi_sentinel_demo.ko.xz`, a `modinfo` potwierdził `6.18.33-v8`,
`modversions` i `aarch64`. Kontrola `visudo` zaakceptowała regułę
dla dwóch dokładnych poleceń `modprobe`. Logi znajdują się w
`artifacts/stage9/module-demo/`.

Aby przygotować oddzielny obraz dla prób z laptopa, ustaw
`export SENTINEL_PROFILE=module-demo` przed wykonaniem bloku
z [instrukcji budowania](yocto-build.md). Pakiet dostępowy dociąga
moduł i `kmod`. Profil włącza SSH. Moduł ładuje się dopiero na żądanie.
Zachowaj sprawdzony obraz jako drogę powrotu.

**Wynik obrazu.** Gotowy plik
`rpi-sentinel-image-raspberrypi3-64.rootfs-20261126154208.wic.bz2`,
manifest i suma SHA256 są w `artifacts/stage9/module-demo/`.
Manifest potwierdził moduł, `kmod`, SSH i oddzielny pakiet dostępu.

## Próba na Raspberry Pi

Wgraj obraz testowy, skonfiguruj Wi-Fi i klucz publiczny, po czym
sprawdź pierwsze logowanie zgodnie z [instrukcją microSD](yocto-build.md#microsd-i-pierwsze-połączenie).
Na laptopie, z katalogu repozytorium, wykonaj:

```bash
read -r -p 'Adres Raspberry Pi: ' pi
ssh -i "$HOME/.ssh/id_ed25519" "developer@$pi" sh -s <<'REMOTE'
set -eu
module=rpi_sentinel_demo
attribute=/sys/kernel/rpi_sentinel_demo/loaded_ms

test ! -e "$attribute"
sudo -n /usr/sbin/modprobe "$module"
trap 'sudo -n /usr/sbin/modprobe -r "$module"' EXIT
test -r "$attribute"
test "$(stat -c %a "$attribute")" = 444
first="$(cat "$attribute")"
sleep 2
second="$(cat "$attribute")"
test "$second" -gt "$first"
printf 'PASS — licznik modułu wzrósł: %s -> %s ms.\n' "$first" "$second"

sudo -n /usr/sbin/modprobe -r "$module"
trap - EXIT
test ! -e "$attribute"
printf 'PASS — atrybut zniknął po usunięciu modułu.\n'

sudo -n /usr/sbin/modprobe "$module"
trap 'sudo -n /usr/sbin/modprobe -r "$module"' EXIT
reloaded="$(cat "$attribute")"
test "$reloaded" -lt "$second"
sudo -n /usr/sbin/modprobe -r "$module"
trap - EXIT
test ! -e "$attribute"
printf 'PASS — po ponownym załadowaniu licznik zaczął od nowa: %s ms.\n' "$reloaded"
REMOTE
```

PASS wymaga nieobecności atrybutu po rozruchu, trybu `0444`, wzrostu
odczytu, zniknięcia atrybutu po usunięciu modułu i nowego licznika po
ponownym załadowaniu. Błąd polecenia lub brak któregokolwiek z tych
efektów daje FAIL. Przy błędzie ładowania sprawdź `journalctl -k -b`
oraz zgodność `vermagic` modułu z uruchomionym kernelem.

**Wynik na Raspberry Pi 3 — PASS.** Przed ładowaniem atrybut był
nieobecny, a po załadowaniu miał tryb `0444`. Dwa odczyty po odstępie
2 s dały `12 ms` i `2021 ms`. Po usunięciu modułu ścieżka zniknęła.
Po ponownym załadowaniu pierwszy odczyt wyniósł `7 ms`. Skrypt
ponownie usunął moduł i potwierdził brak atrybutu.

Osobna próba ładowania i usuwania modułu potwierdziła
oba wpisy w dzienniku jądra odczytanym przez `journalctl -k -b`:

```text
Nov 27 09:28:29 raspberrypi3-64 kernel: rpi_sentinel_demo: Załadowano moduł diagnostyczny
Nov 27 09:28:29 raspberrypi3-64 kernel: rpi_sentinel_demo: Usunięto moduł diagnostyczny
```

## Pytania kontrolne

1. Które katalogi wskazują w poleceniu Kbuild opcje `-C` i `M`?
2. Dlaczego zgodność numeru wersji kernela to tylko część zgodności
   modułu z uruchomionym kernelem?
3. Dlaczego przy usuwaniu modułu zdejmujemy atrybut sysfs przed
   zwolnieniem obiektu?

Ćwiczenie. Po udanej próbie porównaj
`/usr/sbin/modinfo -F vermagic rpi_sentinel_demo` z `uname -r` na Pi.
Wskaż w wyniku zarówno wersję, jak i cechy konfiguracji kernela.
