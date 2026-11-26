# Budowanie i uruchamianie obrazu w etapie 2

Repozytorium pod `/mnt/windows1/Wspolne/Dokumenty/Projekty/Yocto/rpi-sentinel`
zawiera dokumentację, skrypty, przypięte wersje i konfigurację. Katalog
`$HOME/yocto/rpi-sentinel-wrynose` jest na ext4. Jego podkatalog
`layers/` przechowuje źródła upstream, a `build-stage2/` konfigurację
roboczą, logi i wyniki budowania. Poprzedni katalog `build/` zawiera
konfigurację QEMU.

## Sprawdzenie hosta

Host zgłosił Ubuntu 26.04.1 LTS, Linux 7.0.0-44-generic na x86-64,
Ryzen 7 260 z 8 rdzeniami i 16 wątkami, 14 GiB widocznej pamięci,
4 GiB swapu i około 415 GiB wolnego miejsca na ext4. Pakiety build hosta
i ustawienie regionalne `en_US.UTF-8` były dostępne. Użyte polecenia:

```bash
cat /etc/os-release
uname -srmo
lscpu
free -h
swapon --show
findmnt -T "$HOME" -no FSTYPE
df -h "$HOME" .
locale -a
dpkg-query -W git gcc gawk chrpath diffstat texinfo
git -C "$HOME/yocto/rpi-sentinel-wrynose/layers/meta-raspberrypi" rev-parse HEAD
```

## Obraz bazowy

Uruchom z repozytorium:

```bash
bash scripts/build-stage2.sh
```

Skrypt pobiera brakujące repozytoria i porównuje ich rewizje z
[sources.lock](../build-config/sources.lock). Tworzy `build-stage2`,
kopiuje konfigurację, dodaje warstwy Poky i Raspberry Pi, sprawdza
parsowanie metadanych i buduje `core-image-minimal`. Przerywa pracę po
błędzie. Przy kolejnym uruchomieniu aktualizuje kopię konfiguracji i
logi w `build-stage2/logs/`. Zmienna `YOCTO_DIR` pozwala wskazać inną
bezwzględną ścieżkę. Źródła muszą odpowiadać przypiętym wersjom i mieć
czyste katalogi robocze.

Katalog deploy to domyślnie
`$HOME/yocto/rpi-sentinel-wrynose/build-stage2/tmp/deploy/images/raspberrypi3-64/`.
Efektywną ścieżkę podaje `bitbake-getvar DEPLOY_DIR_IMAGE`. Skrypt kopiuje
gotowy obraz karty do `artifacts/` w projekcie, który jest pomijany przez Git.

### Wynik obrazu bazowego

24 listopada 2026 r. użytkownik wykonał build. Log wykazał 4963 udane
zadania i 36 ostrzeżeń. Część głównych adresów źródeł nie odpowiedziała,
po czym BitBake próbował mirrorów. Manifest potwierdził systemd 259.5.

- Obraz SD: `artifacts/core-image-minimal-raspberrypi3-64.rootfs-20261124103219.wic.bz2`
- SHA-256 obrazu skompresowanego: `dd7848b0e5441b5257cab5b0eeb3f927afce82773d3a6b65f3a64b4673df7062`
- Log: `build-stage2/logs/build.log` w katalogu na ext4.

Użytkownik wgrał obraz i potwierdził pojawienie się lokalnego ekranu
`login:` na Raspberry Pi 3.

### Pomiar równoległości

Uruchom z repozytorium `bash scripts/benchmark-stage2.sh`. Skrypt
uruchamia zadanie `compile` dla kernela, systemd, BusyBox,
wpa-supplicant, OpenSSH, curl i zstd kolejno z kombinacjami 8 i 12
zadań BitBake oraz `-j2` i `-j4`. Każda próba zaczyna w osobnym
katalogu roboczym na ext4 z własnym `tmp/` i `sstate-cache/`.
BitBake używa pobranych źródeł oraz kopiuje zgodne wyniki zależności
z lokalnego mirrora sstate obrazu bazowego. Próby działają bez sieci.
Katalogi robocze pozostają dostępne po pomiarze. Wynik dotyczy tego
zestawu receptur i nie mierzy czasu do uzyskania obrazu karty SD.

W `artifacts/benchmark-*/results.tsv` skrypt zapisuje kod zakończenia,
czas w sekundach, minimum `MemAvailable` z próbek co 5 sekund oraz
przyrost liczników odczytu i zapisu stron swapu dla hosta. Szczegóły są
w logu każdej próby, a ścieżka do katalogów roboczych w `workspace.txt`.
Porównuj czas prób z kodem zakończenia 0 razem z zużyciem pamięci. Każda
kombinacja jest mierzona raz, więc zbliżone wyniki mogą wymagać powtórki.

#### Wynik z 25 listopada 2026 r.

| Zadania BitBake | Prace kompilacji | Czas w sekundach | Minimum dostępnego RAM (GiB) | Zapisane strony swapu |
| --- | --- | ---: | ---: | ---: |
| 8 | `-j2` | 1093,91 | 8,05 | 172108 |
| 8 | `-j4` | 551,66 | 8,97 | 1 |
| 12 | `-j2` | 901,49 | 9,21 | 0 |
| 12 | `-j4` | 577,72 | 8,38 | 56889 |

Wszystkie próby zakończyły się kodem 0 i odtworzyły 480 z 482 wyników
sstate. Najkrótszy czas w tej rundzie osiągnęło `8 / -j4`.
Liczniki swapu obejmują cały host.
Każdą kombinację uruchomiono raz w stałej kolejności, więc różnica między
wariantami `-j4` może zależeć również od obciążenia tła i stanu hosta.
Surowe wyniki zapisano lokalnie w `artifacts/benchmark-20261125-093829/`.

Kolejną rundę można uruchomić z własnym katalogiem raportu i listą
par `zadania_bitbake:prace_kompilacji`:

```bash
bash scripts/benchmark-stage2.sh "artifacts/benchmark-j6-j8-$(date +%Y%m%d-%H%M%S)" \
    8:6 8:8 10:6 10:8 12:6 12:8
```

Wyniki drugiej rundy z 25 listopada 2026 r.:

| Zadania BitBake | Prace kompilacji | Czas w sekundach | Minimum dostępnego RAM (GiB) | Zapisane strony swapu |
| --- | --- | ---: | ---: | ---: |
| 8 | `-j6` | 488,26 | 7,32 | 23703 |
| 8 | `-j8` | 444,81 | 6,96 | 2398 |
| 10 | `-j6` | 476,85 | 6,76 | 36981 |
| 10 | `-j8` | 434,24 | 6,54 | 55732 |
| 12 | `-j6` | 495,27 | 6,05 | 56537 |
| 12 | `-j8` | 439,22 | 6,68 | 30154 |

Każda próba zakończyła się kodem 0 z 480 z 482 wyników z mirrora sstate.
Najkrótszy zmierzony czas dało `10 / -j8`. Zalecane ustawienie
do codziennej pracy to `8 / -j8`: w tej rundzie zajęło o 10,57 s
więcej i pozostawiło większy zapas pamięci. Liczniki swapu obejmują
cały host. Punktem odniesienia jest `8 / -j4` z poprzedniej rundy,
więc porównanie między rundami może obejmować różnicę obciążenia hosta.
Surowe wyniki są w `artifacts/benchmark-j6-j8-20261125-103622/`.

Wyniki trzeciej rundy z 25 listopada 2026 r.:

| Zadania BitBake | Prace kompilacji | Czas w sekundach | Minimum dostępnego RAM (GiB) | Zapisane strony swapu |
| --- | --- | ---: | ---: | ---: |
| 8 | `-j10` | 430,49 | 6,43 | 53073 |
| 8 | `-j12` | 409,41 | 6,38 | 17549 |
| 10 | `-j10` | 426,11 | 6,54 | 7518 |
| 10 | `-j12` | 421,52 | 6,23 | 9 |

Wszystkie próby zakończyły się kodem 0 i odtworzyły 480 z 482 wyników
z mirrora sstate. Najkrótszy czas osiągnęło `8 / -j12`. Wynik był
o 35,40 s krótszy od `8 / -j8` z poprzedniej rundy. To pojedyncze próby
kompilacji wybranych receptur. Różnice między rundami mogą obejmować
wpływ obciążenia hosta. Surowe wyniki są w
`artifacts/benchmark-j10-j12-20261125-112741/`.

Wyniki czwartej rundy z 25 listopada 2026 r. przy
`BB_NUMBER_THREADS = "8"`:

| Prace kompilacji | Czas w sekundach | Minimum dostępnego RAM (GiB) | Zapisane strony swapu |
| --- | ---: | ---: | ---: |
| `-j18` | 418,03 | 5,75 | 47457 |
| `-j16` | 417,76 | 5,29 | 71744 |
| `-j14` | 424,33 | 5,95 | 135518 |

Wszystkie trzy próby zakończyły się kodem 0 i odtworzyły 480 z 482
wyników z mirrora sstate. Najkrótszym zmierzonym wynikiem ze wszystkich
rund pozostaje `8 / -j12` (409,41 s). Te limity zapisano w
`build-config/stage2.conf` jako ustawienia domyślne projektu.
Próby przy wyższych limitach
miały mniejszy zaobserwowany zapas RAM. Każdą kombinację zmierzono raz,
a liczniki pamięci i swapu obejmują cały host. Surowe wyniki są w
`artifacts/benchmark-j18-j14-20261125-115936/`.

## Obraz deweloperski z Wi-Fi i SSH

### Skąd BitBake bierze konfigurację

| Plik w repozytorium | Rola |
| --- | --- |
| [`stage2.conf`](../build-config/stage2.conf) | Plik `.conf` ustawia wartości zmiennych BitBake. Tu wybieramy sprzęt, dystrybucję, system init i limity równoległości. |
| [`stage2-wifi-ssh.conf`](../build-config/stage2-wifi-ssh.conf) | Dodatkowy plik `.conf` dołączany z `local.conf` tylko w wariancie deweloperskim. Wybiera pakiety, włącza OpenSSH i przez klasę `extrausers` zakłada konto `developer`. |
| [`stage2-wifi-ssh.bbclass`](../build-config/classes/stage2-wifi-ssh.bbclass) | Klasa metadanych BitBake. `INHERIT` dołącza ją do profilu. Klasa rejestruje funkcję w `ROOTFS_POSTPROCESS_COMMAND`. Funkcja zapisuje jednostkę Wi-Fi, konfigurację DHCP i ustawienia OpenSSH w katalogu `IMAGE_ROOTFS`, czyli w składanym systemie plików urządzenia. |
| [`provision-wifi-card.sh`](../scripts/provision-wifi-card.sh) | Skrypt hosta uruchamiany po wgraniu obrazu. Wpisuje dane konkretnej sieci i klucz publiczny do systemu plików na karcie. |

Pliki konfiguracyjne i klasa są kopiowane przez skrypt z repozytorium do
katalogu roboczego. Edycje wykonuj w repozytorium, ponieważ kolejny build
nadpisze kopie robocze.

### Błąd zależności firmware

Pierwsza próba profilu zgłosiła `Nothing RPROVIDES` dla
`linux-firmware-rpidistro-bcm43430`. Linia poniżej wskazywała powód.
Receptura definiuje ten pakiet, ale BitBake pominął ją z powodu flagi
`synaptics-killswitch`. Profil wymagał pakietu w `IMAGE_INSTALL`, więc
odrzucenie receptury zatrzymało planowanie zadań obrazu. W profilu
zastąpiono te pakiety odpowiednikami `linux-firmware-bcm43430` i
`linux-firmware-bcm43455` z OE-Core. Weryfikacja na sprzęcie jest otwarta.

[Zachowanie LICENSE_FLAGS w Yocto 6.0.4](https://docs.yoctoproject.org/6.0.4/ref-manual/variables.html#term-LICENSE_FLAGS)

### Sterownik Wi-Fi wykryty, ale bez interfejsu

Na Raspberry Pi 3 Model B Rev 1.2 kernel wykrył BCM43430 na SDIO.
Dziennik potwierdził rozruch i nasłuch `sshd.socket`. Usługa Wi-Fi
oczekiwała na `wlan0`, ale sterownik przerwał inicjalizację przy
`brcmf_fwvid_request_module: mod=cyw: failed 256`. Na karcie był główny
moduł `brcmfmac`, a osobny moduł Cypress był dostępny jako pakiet kernela
w artefaktach Yocto. Profil dodaje teraz `kernel-module-brcmfmac-cyw`.
Kolejny rozruch potwierdził inicjalizację firmware i pojawienie się
`wlan0`. Pi nawiązało asocjację z punktem dostępu, ale uwierzytelnianie
przekroczyło limit czasu. Kernel nadal zgłaszał brak `regulatory.db`.
W przypiętej recepturze OE-Core plik i jego podpis należą do pakietu
`wireless-regdb-static`. Profil wybiera teraz ten pakiet. Weryfikacja
bazy przy następnym rozruchu i test logowania po Wi-Fi są otwarte.

### Budowanie i karta

Domyślną sieć przygotowujesz na laptopie przed włożeniem karty do Pi.
Skrypt czyta ją automatycznie z ignorowanej przez Git ścieżki
`.local/wifi-default` w projekcie. Na tym hoście katalog projektu na
FUSE nie zachowuje trybu `0600`, dlatego ta ścieżka jest dowiązaniem
do pliku w katalogu domowym na ext4. Wpisz SSID w pierwszym wierszu
i hasło w drugim. Polecenia uruchom z repozytorium.

```bash
(umask 077; mkdir -p "$HOME/.config/rpi-health-monitor"; touch "$HOME/.config/rpi-health-monitor/wifi-default")
chmod 600 "$HOME/.config/rpi-health-monitor/wifi-default"
mkdir -p .local
ln -sfn "$HOME/.config/rpi-health-monitor/wifi-default" .local/wifi-default
vim .local/wifi-default
```

Plik zawiera jawne hasło, dlatego skrypt wymaga trybu `0600`.
Bez zapisanego profilu skrypt pyta o SSID i hasło w terminalu laptopa.
Po zapisaniu konfiguracji na karcie Raspberry Pi używa jej przy starcie
i łączy się z Wi-Fi przed logowaniem przez SSH.

```bash
bash scripts/build-stage2.sh --wifi-ssh
```

Build używa `$HOME/yocto/rpi-sentinel-wrynose/build-stage2-wifi-ssh/`,
współdzieli pobrane pliki i cache obrazu bazowego oraz eksportuje obraz
do `artifacts/wifi-ssh/`. Skrypt wypisuje ścieżkę i SHA-256. Profil obsługuje
OpenSSH, firmware Broadcom i DHCP przez systemd-networkd. Wi-Fi startuje
na `wlan0`, gdy na karcie jest plik konfiguracji. Konto `developer`
loguje się kluczem publicznym i ma dostęp do dziennika systemowego.

Sprawdź nazwę karty przez `lsblk`. Nazwy `/dev/sda` i `/dev/sdb` mogą
się zmieniać między podłączeniami. Podaj urządzenie całej karty jako
pierwszy argument. Drugi argument to ścieżka obrazu, a trzeci to suma
SHA-256 z buildu. Po wpisaniu `NADPISZ` ze ścieżką urządzenia skrypt
odmontowuje partycje tej karty. Jeśli odmontowanie się nie powiedzie
lub urządzenie nadal jest używane, skrypt przerywa przed zapisem.

```bash
lsblk -e 7 -o PATH,SIZE,MODEL,TYPE,LABEL,MOUNTPOINTS
bash scripts/remove-deploy-card.sh /dev/sdX 'artifacts/wifi-ssh/OBRAZ.wic.bz2' 'SHA256_Z_KOMPILACJI'
lsblk -e 7 -o PATH,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINTS
```

Jeśli `.local/wifi-default` jest gotowy, podaj klucz publiczny jako
czwarty argument, aby od razu skonfigurować Wi-Fi i SSH. Po zapisie
skrypt sam znajdzie partycję `root` na wskazanym urządzeniu i zastosuje
zapisany profil bez dodatkowego pytania o sieć lub hasło.

```bash
bash scripts/remove-deploy-card.sh /dev/sdX 'artifacts/wifi-ssh/OBRAZ.wic.bz2' \
    'SHA256_Z_KOMPILACJI' "$HOME/.ssh/id_ed25519.pub"
```

Oddzielny krok poniżej przydaje się dla już zapisanej karty lub gdy
chcesz podać również sieć zapasową.

Wybierz partycję linuksową karty, zwykle drugą, i odmontuj ją, jeśli
została zamontowana. Przygotuj plik klucza publicznego. Osobną parę
kluczy możesz utworzyć raz przez
`ssh-keygen -t ed25519 -f "$HOME/.ssh/rpi-health-monitor"`.

```bash
bash scripts/provision-wifi-card.sh /dev/sdX2 "$HOME/.ssh/rpi-health-monitor.pub"
```

Zastąp `/dev/sdX2` sprawdzoną ścieżką partycji. Skrypt sprawdza marker
obrazu, pyta o sieć domyślną i pozwala opcjonalnie wpisać sieć zapasową.
`wpa_supplicant` preferuje przy wyborze dostępnej sieci tę z wyższym
priorytetem (20 dla domyślnej, 10 dla zapasowej). Jeśli Pi nie ma
dostępu do żadnej zapisanej sieci, wyłącz płytkę i uruchom skrypt
ponownie na laptopie z nowymi danymi. Ponowna konfiguracja zastępuje obie
pozycje. Kompilacja i ponowne wgrywanie obrazu są wtedy zbędne.

Skrypt zapisuje wyprowadzony klucz WPA2 w pliku na karcie z trybem
`0600`. Klucz ten nadal daje dostęp do sieci. Profil używa domeny
radiowej `PL` i wymaga WPA2-Personal albo trybu mieszanego
dopuszczającego WPA2. Sieć domyślną wybierasz przy przygotowaniu karty.

### Test na urządzeniu

Znajdź adres Pi na liście dzierżaw DHCP routera. Z tej samej sieci
lokalnej połącz się z urządzeniem:

```bash
ssh -i "$HOME/.ssh/rpi-health-monitor" developer@IP_PI
uname -a
cat /proc/1/comm
ip address show wlan0
journalctl -k -b --no-pager -n 30
free -h
df -h /
```

Po restarcie sprawdź logowanie przy odłączonym monitorze i Ethernet.
Sprawdź wybór sieci domyślnej, a następnie po wyłączeniu jej punktu
dostępu sprawdź połączenie przez sieć zapasową.

### Wynik testu Wi-Fi i SSH

Na Raspberry Pi 3 Model B Rev 1.2 użytkownik potwierdził SSH zarówno
przez hotspot WPA2, jak i przez domowy router. Na domowym Wi-Fi
`wlan0` otrzymał przez DHCP adres `192.168.1.10/24`. Kernel to
`6.18.33-v8` (AArch64), a PID 1 to `systemd`. Partycja `root` miała
220,6 MiB pojemności i 75,9 MiB dostępnego miejsca. Swap był wyłączony.

Oba połączenia przetestowano z parametrem
`brcmfmac.feature_disable=0x2000` dopisanym ręcznie do `cmdline.txt`.
W używanym kernelu maska wyłącza funkcję FWSUP sterownika, dzięki czemu
negocjację WPA2 obsługuje `wpa_supplicant`. Po zmianie log hotspotu
potwierdził odebranie ramek negocjacji, zakończenie wymiany kluczy i DHCP.

Profil deweloperski dodaje ten parametr przez
`CMDLINE:append:pn-rpi-cmdline`. Receptura BSP `rpi-cmdline` używa
`CMDLINE` do wygenerowania pliku na partycji rozruchowej. Zbuduj profil
ponownie i sprawdź wygenerowany plik:

```bash
bash scripts/build-stage2.sh --wifi-ssh
find "$HOME/yocto/rpi-sentinel-wrynose/build-stage2-wifi-ssh/tmp/deploy/images/raspberrypi3-64" \
    -name cmdline.txt -exec grep -H -F 'brcmfmac.feature_disable=0x2000' {} +
```

Świeży obraz `core-image-minimal-raspberrypi3-64.rootfs-20261124204507.wic.bz2`
został wgrany na microSD i uruchomiony. SSH przez domowy router działało
pod `192.168.1.10`, a `/proc/cmdline` potwierdził parametr wygenerowany
przez Yocto, bez ręcznej edycji partycji `boot`. Kernel zgłosił
`6.18.33-v8` na AArch64, PID 1 to `systemd`, a partycja root miała
88,8 MiB wolnego miejsca przy 220,6 MiB pojemności. System działał
bez swapu. Komunikaty kernela odczytano przez `journalctl -k -b`.
Dziennik potwierdził start firmware BCM43430 po próbie odczytu pliku
specyficznego dla płytki.

SHA-256 sprawdzonego obrazu:
`710fcd8cb536ce4d50e1c6c036a3415908a9b14ab6949355e7335e18ae84803f`.
Test dotyczył Wi-Fi i SSH. Fizycznego Ethernetu nie testowano.

### Diagnostyka bez sieci

Jeśli Pi nie ma osiągalnego adresu, sprawdź nazwę partycji `root` na
karcie przez `lsblk` i zamontuj ją na laptopie. Użyj aktualnej ścieżki
partycji w obu poleceniach. Przykład zakłada `/dev/sdb2`.

```bash
bash scripts/diagnose-wifi-card.sh enable /dev/sdb2
```

Skrypt włącza trwały dziennik z limitem 16 MiB. Zachowuje dotychczasowe
dowiązanie `/var/log`, ponieważ wskazuje na pamięć ulotną. Skrypt sam
odmontowuje partycję `root` oraz, jeśli jest zamontowana, partycję
`boot` z tej samej karty. Uruchom Pi na około dwie minuty, a potem włóż
kartę do laptopa i ponownie zamontuj partycję `root`.

Tryb `enable` dodaje też lokalne nadpisanie jednostki systemd z opcją
`wpa_supplicant -d`. To pozwala obserwować wymianę ramek EAPOL używanych
do negocjacji WPA2. Opcja ujawniająca klucze (`-K`) pozostaje wyłączona.

```bash
bash scripts/diagnose-wifi-card.sh read /dev/sdb2
```

Wynik obejmuje krótką listę wybranych zdarzeń Wi-Fi, EAPOL, DHCP, SSH i kernela
z ostatniego zapisanego rozruchu. Wgranie nowego obrazu usuwa tę
konfigurację dziennika.

## Etap 3 — aplikacja monitorująca na hoście

Projekt w `app/` wymaga kompilatora C, CMake 3.16 lub nowszego oraz
narzędzia budowania, np. Make. Na Ubuntu pakiet `cmake` dostarcza
również `ctest`. Jeśli brakuje narzędzi, zainstaluj je przed
uruchomieniem poniższych poleceń:

```bash
sudo apt install build-essential cmake libsystemd-dev pkg-config python3
```

Z katalogu głównego repozytorium:

```bash
build_dir=$(mktemp -d)
cmake -S libsentinel-metrics -B "$build_dir/metrics" -DCMAKE_BUILD_TYPE=Release &&
cmake --build "$build_dir/metrics" --parallel &&
cmake --install "$build_dir/metrics" --prefix "$build_dir/install" &&
cmake -S app -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$build_dir/install" &&
cmake --build "$build_dir" --parallel &&
(cd "$build_dir" && ctest --output-on-failure) &&
cmake --install "$build_dir" --prefix "$build_dir/install"
```

Biblioteka instaluje nagłówek `sentinel/metrics.h`, konfigurację pakietu
CMake i bibliotekę współdzieloną. `CMAKE_PREFIX_PATH` pomaga CMake
znaleźć zainstalowany pakiet podczas konfiguracji aplikacji. Do prób
hostowych używamy binarki bezpośrednio z katalogu budowania, gdzie
CMake zapisał ścieżkę do tymczasowo zainstalowanej biblioteki.

Żeby zobaczyć czytelny przebieg scenariusza testowego z katalogu
repozytorium, uruchom `sh app/tests/process.sh "$build_dir/rpi-sentinel"`.
Skrypt wypisuje sprawdzone zmiany stanu i wynik obsługi SIGTERM.

### Etap 4 — zakres CTest

| Test | Scenariusze |
| --- | --- |
| `sentinel-unit` | Granice parserów, brak plików, domyślne wartości pominiętych kluczy, progi, histereza, cały JSON oraz atomowy zapis. Limit `RLIMIT_FSIZE` wymusza błąd w trakcie zapisu i pozwala sprawdzić, czy poprzedni plik pozostał dostępny. |
| `sentinel-process` | Pełny cykl z testowymi plikami: `NORMAL → WARNING → ERROR → WARNING → NORMAL`, częściowa utrata pomiaru i poprawne wyjście. |
| `sentinel-shutdown` | Po opublikowaniu pierwszego stanu test wysyła `SIGTERM`. CTest ogranicza wykonanie do 6 s przy interwale pomiaru 10 s. |

Testy tworzą izolowane pliki wejściowe w katalogach tymczasowych.
Można je uruchomić na Linuksie bez Raspberry Pi i bez odczytu temperatury
hosta.

Instalacja umieszcza program w `bin/`, a przykład konfiguracji w
`share/rpi-sentinel/` pod wskazanym prefiksem. W zwykłym uruchomieniu
program czyta `/etc/rpi-sentinel.conf` i zapisuje JSON w
`/run/rpi-sentinel/status`. Do próby na hoście wskaż ścieżki zapisu
i konfiguracji w katalogu tymczasowym:

```bash
"$build_dir/rpi-sentinel" --config app/rpi-sentinel.conf \
    --status-file "$build_dir/status"
```

Program czyta rzeczywiste `/proc/uptime` i `/proc/meminfo`. Domyślną
strefą temperatury jest `cpu-thermal`. Na innym sprzęcie można podać
właściwy typ przez `--thermal-type`. Brak dopasowania oznacza
niedostępność temperatury i stan `ERROR`. Parametry `--proc-root`
i `--thermal-root` wskazują przygotowane źródła w teście procesowym.

- Plik konfiguracyjny obowiązuje od startu procesu. Zawiera pary
  `klucz=wartość` i komentarze od `#`. Nieobecne klucze zachowują
  wartości domyślne z przykładu. Interwał to 1–10 s. Progi CPU są w °C,
  progi RAM w całych procentach. Stan wchodzi na próg przy `>=`, a
  ustępuje po spadku poniżej progu pomniejszonego o histerezę.
- Wynik JSON używa milistopni Celsjusza, milisekund i KiB. Procent
  użytej pamięci jest obliczany z `MemTotal - MemAvailable` i
  zaokrąglany w dół. Niedostępna metryka ma pole `io_error` z kodem
  błędu. Poprawne metryki nadal są publikowane.
- Nieprawidłowa konfiguracja kończy proces przed pierwszą próbką.
  Błąd wejścia jest zgłaszany przy zmianie i powoduje ponowienie
  odczytu w kolejnym cyklu. Błąd publikacji pozostawia poprzedni plik
  i pozwala ponowić próbę. Pomiary trafiają na standardowe wyjście,
  a zmiany stanu i błędy na standardowe wyjście błędów.
- Publikacja tworzy plik przez `mkstemp()` obok pliku docelowego.
  Po zapisaniu i zamknięciu `rename()` atomowo podmienia nazwę.
  Otwarty wcześniej deskryptor nadal wskazuje poprzednią wersję.
  Stan ma znajdować się w ulotnym `/run`, więc zapis nie zapewnia
  trwałości po zaniku zasilania.
- Program używa ograniczonych buforów i struktur o stałej wielkości.
  Aplikacja nie alokuje pamięci bezpośrednio na stercie. Uchwyty plików
  i katalogów używane przez bibliotekę standardową są zamykane.
  `sigaction()` ustawia procedurę `SIGTERM`, która tylko zapisuje
  flagę. `pselect()` odblokowuje sygnał atomowo przy oczekiwaniu,
  a proces kończy cykl bez zapisywania stanu z procedury sygnału.

### CI aplikacji

Workflow `Kompilacja i testy aplikacji` uruchamia się po `push` i przy
aktualizacji pull requesta. Na runnerze Ubuntu 24.04 buduje i instaluje
bibliotekę do katalogu tymczasowego, a potem buduje aplikację jako
konsumenta zainstalowanej biblioteki. Jawnie włącza testy
przez `-DBUILD_TESTING=ON`. CTest wypisuje szczegóły nieudanych testów,
a opcja `--no-tests=error` kończy zadanie błędem również wtedy, gdy CTest
nie znajdzie żadnego testu.

Po wysłaniu zmian na GitHub otwórz kartę **Actions** i wybierz przebieg
dla danego commita. Oczekiwany wynik to cztery zaliczone testy, w tym
`sentinel-watchdog`, który odbiera powiadomienia aplikacji przez lokalne
gniazdo Unix. Ten przebieg dotyczy aplikacji hostowej. Procedura prób
obrazu Yocto i Raspberry Pi jest w [testach sprzętowych](testing.md).

## Etap 5 — integracja z Yocto

Warstwa `meta-rpi-sentinel` korzysta z OE-Core i deklaruje zgodność
z Wrynose. `BBPATH` umożliwia wyszukiwanie metadanych po ścieżce,
a `BBFILES` wskazuje pliki receptur w warstwie. Identyfikator kolekcji
łączy wzorzec plików, zależność od `core` i deklarację zgodności.

Receptura `rpi-sentinel_0.1.0.bb` pobiera katalog `app/` i tekst
licencji z tego samego checkoutu co warstwa. `S` wskazuje
`app/` w `UNPACKDIR`. Klasa `cmake` konfiguruje kompilację
skrośną. Przy instalacji `SENTINEL_CONFIG_INSTALL_DIR` ustawia
miejsce konfiguracji w systemie docelowym, a `do_install:append`
dopisuje instalację jednostki do drzewa `D`. `CONFFILES` oznacza
konfigurację użytkownika na potrzeby aktualizacji pakietu.

`SYSTEMD_SERVICE:${PN}` wiąże jednostkę z pakietem, a
`SYSTEMD_AUTO_ENABLE:${PN}` włącza jej automatyczny start.
`DynamicUser=yes` zapewnia konto przydzielane przez systemd,
a `RuntimeDirectory` tworzy katalog stanu pod `/run`.
`features_check` wymaga systemd w `DISTRO_FEATURES`. Receptura
obrazu rozszerza `core-image-minimal` o pakiet w `IMAGE_INSTALL`.

Po zatwierdzeniu przewidywań najpierw sprawdzamy parsowanie i
efektywną konfigurację BitBake, następnie pakiet aplikacji i QA,
a na końcu obraz. Sprawdzono CTest, instalację do tymczasowego drzewa
i składnię jednostki. W pierwszym buildzie BitBake utworzył pakiet RPM
z binarką AArch64, konfiguracją, jednostką systemd i regułą preset.
Log `do_package_qa` potwierdził wykonanie kontroli, a manifest
`rpi-sentinel-image` zawiera wersję 0.1.0 pakietu.

### Biblioteka metryk w Yocto

Receptura `libsentinel-metrics` kompiluje bibliotekę dla celu. Zależność
`DEPENDS` w recepturze monitora udostępnia nagłówki, bibliotekę i konfigurację
CMake w jego sysroot przed konfiguracją. Pakiet uruchomieniowy biblioteki
zawiera plik współdzielony z SONAME `libsentinel-metrics.so.1`. Pakiet
`-dev` dostarcza nagłówek, konfigurację CMake i niewersjonowany link
używany przy linkowaniu. Yocto odczytuje potrzebny SONAME z binarki
monitora i zapisuje zależność uruchomieniową w metadanych pakietu.
W bieżącym buildzie `oe-pkgdata-util` pokazuje logiczną nazwę
`libsentinel-metrics`, a manifest obrazu pokazuje nazwę pakietu
instalowanego na urządzeniu `libsentinel-metrics1`.

Obie receptury rozpakowują wspólny prywatny nagłówek parsera pod `S`.
Dzięki temu standardowe mapowanie ścieżek debugowania Yocto obejmuje
ten nagłówek. Przy nagłówku obok `S` kontrola `buildpaths` wykryła
ścieżkę do katalogu tymczasowego w pakiecie debug.

W zainicjalizowanym katalogu builda obrazu deweloperskiego uruchom:

```bash
bitbake libsentinel-metrics rpi-sentinel &&
oe-pkgdata-util list-pkg-files libsentinel-metrics-dev &&
oe-pkgdata-util read-value RDEPENDS rpi-sentinel
```

Po zbudowaniu i wgraniu obrazu Wi-Fi i SSH wykonaj
`bash scripts/smoke-test.sh live "$pi"`. Przebieg wgrywania opisuje
[procedura testów sprzętowych](testing.md).

**Wynik na Raspberry Pi 3.** Test `live` zakończył się wynikiem PASS.
Usługa była włączona przy starcie i aktywna, dziennik zawierał próbki,
a plik stanu został zaktualizowany. Próbka w stanie `NORMAL` pokazała
38,628°C, 11% użycia RAM i 224,340 s czasu pracy urządzenia.

### Wspólny sstate i baza równoważności

`build-stage5` współdzieli cache sstate z `build-stage2`.
Przy lokalnym serwerze równoważności hashy oba buildy powinny korzystać
z tej samej bazy, żeby BitBake mógł rozpoznawać równoważne wyniki zadań.
Na tym hoście należy wskazać istniejący katalog bazy buildu bazowego
w `build-stage5/conf/local.conf`:

```bitbake
BB_HASHSERVE_DB_DIR = "/home/dominik/yocto/rpi-sentinel-wrynose/build-stage2/cache"
```

Wartością zmiennej jest katalog. BitBake sam dopisuje nazwę
`hashserv.db`. Do sprawdzenia konfiguracji wystarczy `bitbake -p`.
Zmiana dotyczy ponownego użycia wyników przyszłych buildów.
