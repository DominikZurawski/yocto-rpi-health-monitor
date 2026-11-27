# Eksperymenty z BitBake

Konfiguracja startowa to Yocto 6.0.4 Wrynose, BitBake 2.18.0, `MACHINE=raspberrypi3-64` i projekt w rewizji `a4ba525`. Używamy osobnego katalogu `~/yocto/rpi-sentinel-wrynose/build-stage8`. Artefakty obserwacji zapisujemy w ignorowanym przez Git katalogu `artifacts/stage8/`.

## 1. Graf zależności receptury

**Hipoteza.** Zależność `DEPENDS += "systemd"` udostępni nagłówki i bibliotekę aplikacji przez zadania przygotowania sysroot.

**Prognoza przed próbą.** Oczekiwanym zadaniem dostarczającym zależność było `systemd.do_compile`.

**Polecenia.** Uruchom blok z katalogu repozytorium. Przy zakładaniu osobnego builda skopiowano sprawdzoną konfigurację etapu 6 z systemd oraz listą tych samych warstw. Przy kolejnych uruchomieniach używamy już zapisanej konfiguracji.

```bash
(
    set -eo pipefail
    project_dir="$PWD"
    yocto_dir="$HOME/yocto/rpi-sentinel-wrynose"
    reference_dir="$yocto_dir/build-stage6-wifi-ssh"
    build_dir="$yocto_dir/build-stage8"
    artifact_dir="$project_dir/artifacts/stage8/01-graph"
    new_build=0
    if [ ! -e "$build_dir/conf/local.conf" ]; then
        new_build=1
    fi
    source "$yocto_dir/layers/openembedded-core/oe-init-build-env" "$build_dir"
    if [ "$new_build" -eq 1 ]; then
        cp -- "$reference_dir/conf/local.conf" "$reference_dir/conf/bblayers.conf" \
            "$reference_dir/conf/stage2.conf" "$reference_dir/conf/stage2-wifi-ssh.conf" conf/
        install -D -m 0644 "$project_dir/build-config/classes/stage2-wifi-ssh.bbclass" \
            classes/stage2-wifi-ssh.bbclass
    fi

    bitbake -g rpi-sentinel
    mkdir -p -- "$artifact_dir"
    cp -- pn-buildlist task-depends.dot "$artifact_dir/"
    wc -l pn-buildlist task-depends.dot
    rg -x 'rpi-sentinel|systemd|cmake-native|pkgconf-native' pn-buildlist
    rg '"rpi-sentinel\.do_(prepare_recipe_sysroot|configure|compile)" ->|^"systemd\.do_(populate_sysroot|install)" -> "systemd\.do_(install|compile)"' task-depends.dot
)
```

**Wynik.** BitBake zapisał 150 pozycji w `pn-buildlist` i 8090 wierszy w `task-depends.dot`. Wśród pozycji były `rpi-sentinel`, `systemd`, `cmake-native` i `pkgconf-native`. Każda strzałka DOT prowadzi od zadania do jego wymagania. Zaobserwowana ścieżka wygląda tak:

```text
rpi-sentinel.do_compile
  -> rpi-sentinel.do_configure
  -> rpi-sentinel.do_prepare_recipe_sysroot
  -> systemd.do_populate_sysroot
  -> systemd.do_install
  -> systemd.do_compile
```

**Wniosek.** `systemd.do_compile` rzeczywiście należy do łańcucha jako zależność pośrednia. Bezpośrednia krawędź między recepturami łączy `rpi-sentinel.do_prepare_recipe_sysroot` z `systemd.do_populate_sysroot`. W przypiętej wersji OE-Core ustala to `do_prepare_recipe_sysroot[deptask] = "do_populate_sysroot"` w `staging.bbclass`. Po instalacji plików dostawca udostępnia wybrane pliki, w tym nagłówki i bibliotekę, do sysroot potrzebnego przy konfiguracji aplikacji. `bitbake -g` wyznacza zależności bez wykonywania zadań kompilacji. Graf sam nie rozstrzyga, które zadania wykona kolejny build i które wyniki przywróci cache.

## 2. Efektywne metadane receptury przez bitbake -e

**Hipoteza.** `bitbake -e` pokaże końcowe `DEPENDS` i historię operacji,
które złożyły tę wartość. Po wydzieleniu biblioteki metryk oczekujemy
`libsentinel-metrics` i `systemd` z receptury oraz `cmake-native`
i `pkgconf-native` z klas.

**Polecenia.** Uruchom w katalogu głównym projektu. Używamy tego samego
`build-stage8` co w eksperymencie 1, tym razem z bieżącą recepturą
aplikacji po wydzieleniu biblioteki.

```bash
(
    set -eo pipefail
    artifact_dir="$PWD/artifacts/stage8/02-env"
    yocto_dir="$HOME/yocto/rpi-sentinel-wrynose"
    mkdir -p "$artifact_dir"
    source "$yocto_dir/layers/openembedded-core/oe-init-build-env" "$yocto_dir/build-stage8"
    bitbake -e rpi-sentinel |
        awk '/^# \$DEPENDS([[:space:]]|$)/ {printing=1}
             printing {print}
             /^DEPENDS=/ {printing=0}
             /^(WORKDIR|UNPACKDIR|S|B|D)=/ {print}' |
        tee "$artifact_dir/metadata.txt"
)
```

**Wynik.** BitBake pokazał dziewięć operacji w historii `DEPENDS`.
Wiersz po rozwinięciu zawierał:

```text
DEPENDS="pkgconf-native cmake-native gcc-cross-aarch64 virtual/compilerlibs virtual/libc libsentinel-metrics systemd ninja-native systemd-systemctl-native"
```

| Wpis | Pochodzenie |
| --- | --- |
| `libsentinel-metrics`, `systemd` | Receptura `rpi-sentinel_0.1.0.bb`, linia z `DEPENDS +=`. |
| `cmake-native`, `ninja-native` | `cmake.bbclass`. CMake dodano przez `:prepend`, a Ninja dopisano przy wyborze generatora. |
| `pkgconf-native` | `pkgconfig.bbclass`. |
| `systemd-systemctl-native` | `systemd.bbclass`. |
| `gcc-cross-aarch64`, `virtual/compilerlibs`, `virtual/libc` | Zależności bazowe używane przez recepturę podczas kompilacji skrośnej. |

| Zmienna | Rzeczywista ścieżka względem `WORKDIR` | Rola |
| --- | --- | --- |
| `UNPACKDIR` | `sources/` | Katalog rozpakowania wejść. |
| `S` | `sources/app/` | Katalog źródeł wskazany przez recepturę. |
| `B` | `build/` | Katalog pracy CMake wskazany przez `cmake.bbclass`. |
| `D` | `image/` | Drzewo instalacyjne receptury, będące wejściem do pakowania. |

**Wniosek.** Hipoteza się potwierdziła. Wynik zawiera też narzędzia
i zależności bazowe dodane przez klasy obsługujące poszczególne etapy.
`-native` oznacza wariant narzędzia działający na hoście budowania.
`bitbake -e` odczytuje metadane receptury i tłumaczy pochodzenie
wartości. Ścieżki w wyniku określają, gdzie zadania będą pisały pliki,
jeżeli je uruchomimy. Eksperyment zakończył się bez kompilacji.

## 3. Zadania receptury i ich logi

### Przygotowanie źródeł

**Hipoteza.** Dla wejść `file://` BitBake znajdzie lokalne pliki,
rozłoży je według `SRC_URI`, a etap `do_patch` przejdzie przez
obsługę poprawek bez zastosowania żadnej do źródeł aplikacji.

Do próby użyto istniejących logów receptury `rpi-sentinel` z
`build-stage6-wifi-ssh`. W `log.do_fetch` wpis `Searching for common`
jest w wierszu 35. W `log.do_unpack` wiersz 64 pokazuje wejście
`common` i katalog docelowy `sources/app/`. Plik znaleziony po
operacji to `sources/app/common/parse.h`.

```bash
work="$HOME/yocto/rpi-sentinel-wrynose/build-stage6-wifi-ssh/tmp/work/cortexa53-nocrypto-poky-linux/rpi-sentinel/0.1.0"
grep -n 'Unpacking .*common to ' "$work/temp/log.do_unpack"
ls -l "$work/sources/app/common/parse.h"
```

`file://common;subdir=app` oznacza, że całe lokalne wejście `common/`
jest kopiowane do podkatalogu `app` pod `UNPACKDIR`. Pierwsze wiersze
logu pokazują ścieżki wyszukiwania, a wiersz z `Unpacking ... to ...`
pokazuje miejsce docelowe. W `log.do_patch` BitBake wykonał
`patch_do_patch`, a log nie zawiera wpisu o zastosowaniu poprawki.
Wpisy o usuwaniu lub dodawaniu bibliotek dotyczą przygotowania sysroot
receptury. Hipoteza dotycząca przygotowania źródeł się potwierdziła.

### Konfiguracja CMake

**Hipoteza i prognoza.** `do_configure` wygeneruje instrukcje
budowania. W recepturze wskazuje na to dziedziczenie klasy `cmake`,
a `EXTRA_OECMAKE` przekazuje opcje konfiguracyjne.

```bash
grep -nE 'Configuring done|Generating done|Build files have been written' \
    "$work/temp/log.do_configure"
```

**Wynik.** Wiersz 29 potwierdził zapis plików budowania w `B`, czyli
w katalogu `build/`. Jest w nim `build.ninja`. Prognoza była poprawna.
Ten log pokazuje wygenerowanie instrukcji na podstawie
`app/CMakeLists.txt`. Ich wykonanie należy do `do_compile`.

### Kompilacja i linkowanie

**Prognoza.** Pierwsza miała powstać binarka `rpi-sentinel`, ponieważ
cel jest wymieniony przed plikiem źródłowym w `add_executable`.

```bash
grep -noE -- 'cmake --build| -o (CMakeFiles/rpi-sentinel.dir/src/main\.c\.o|rpi-sentinel)( |$)' \
    "$work/temp/log.do_compile"
```

**Wynik.** Wiersz 2 pokazał wywołanie `cmake --build`, wiersz 10
utworzenie `main.c.o`, a wiersz 11 zapis binarki `rpi-sentinel`.
`add_executable` podaje najpierw nazwę celu CMake. Kompilator
tworzy plik obiektowy z `main.c`, a linker używa go wraz z bibliotekami
do utworzenia binarki AArch64.

### Instalacja do drzewa receptury

**Prognoza.** Pliki trafią do katalogu roboczego receptury. Wskazówką
była rozdzielona struktura katalogów pod `WORKDIR`.

```bash
grep -nE 'Installing: .*rpi-sentinel' "$work/temp/log.do_install"
```

**Wynik.** Wiersze 9 i 10 potwierdziły zapis plików do
`image/usr/bin/rpi-sentinel` oraz `image/etc/rpi-sentinel.conf`.
O miejscu instalacji decyduje `D`, w tym buildzie
`${WORKDIR}/image`. `UNPACKDIR` wskazuje etap wejściowy,
`${WORKDIR}/sources`. To osobne katalogi z osobnymi rolami.

### Podział plików na pakiety

**Prognoza.** Binarka, konfiguracja i jednostka systemd trafią do
osobnych pakietów.

```bash
find "$work/packages-split" \( -type f -o -type l \) -printf '%P\n' |
    grep -E '/(usr/bin/rpi-sentinel|etc/rpi-sentinel\.conf|rpi-sentinel\.service|98-rpi-sentinel\.preset)$' |
    sort
```

**Wynik.** Każda z czterech ścieżek zaczynała się od `rpi-sentinel/`.
To nazwa jednego pakietu. Dalsza część ścieżki wskazuje miejsce pliku
po zainstalowaniu na urządzeniu.

Domyślne `FILES:${PN}` z OE-Core obejmuje binarki z `bindir` i pliki
z `sysconfdir`. W recepturze `SYSTEMD_SERVICE:${PN}` wskazuje jednostkę,
którą klasa `systemd` wiąże z tym samym pakietem. `CONFFILES:${PN}`
oznacza plik konfiguracji na potrzeby aktualizacji pakietu.

**Wniosek z eksperymentu 3.** Śledziliśmy przejście od wejść `SRC_URI`
przez źródła `S`, kompilację w `B`, instalację w `D` i podział na
pakiety w `packages-split`. `do_package` przygotowuje zawartość
pakietów. Archiwum RPM zapisuje osobne zadanie `do_package_write_rpm`.

## 4. Pierwszy i kolejny build receptury

**Hipoteza.** Pierwsze `bitbake rpi-sentinel` w `build-stage8` może
odtworzyć wyniki z wcześniejszych buildów. Powtórka bez zmian może użyć
znaczników zadań i wyników już obecnych w tym katalogu.

**Prognoza dla pierwszego builda.** BitBake odtworzy wyniki z cache,
ponieważ zależne źródła i konfiguracja pozostają takie same, a cache
jest nadal dostępny.

**Pierwszy build.** W zainicjalizowanym `build-stage8` uruchomiono
`bitbake rpi-sentinel`. Log jest w `artifacts/stage8/04-repeat-build/first.log`.
BitBake zgłosił 808 wymaganych wpisów sstate, 808 trafień lokalnych,
brak brakujących wpisów i `Current 0`. W podsumowaniu 1934 z 1935
zadań nie wymagało ponownego wykonania. Pozostało zbiorcze zadanie
`do_build` oznaczone `noexec`, czyli bez własnych poleceń.
Prognoza się potwierdziła. Aplikacja została odzyskana z cache bez
ponownej kompilacji.

**Powtórka bez zmian.** Ten sam cel wykonano w tym samym katalogu,
a wynik zapisano w `artifacts/stage8/04-repeat-build/second.log`.
BitBake zgłosił `Wanted 530 Local 530 Missed 0 Current 278`.
Wszystkie 1935 zadań nie wymagały ponownego wykonania. W pierwszym
logu było 278 wpisów `Running setscene task`, a w drugim nie było
takich wpisów.

**Wniosek.** Przy pierwszym uruchomieniu BitBake odtworzył potrzebne
wyniki z cache. W powtórce korzystał z rezultatów i znaczników
istniejących już w tym katalogu. `Local` oznacza dostępny wpis
lokalnego cache. Do ustalenia, czy rzeczywiście nastąpiło odtwarzanie,
sprawdziliśmy log wykonania zadań `setscene`.

## 5. Zawartość wpisu sstate-cache

**Hipoteza i prognoza.** Nazwa wpisu sstate identyfikuje recepturę
i konkretne zadanie. Archiwum mieści wyniki tego zadania.

**Wynik identyfikacji.** W `SSTATE_DIR` znaleziono wpis zaczynający się
od `sstate:rpi-sentinel:` i kończący się
`_package_write_rpm.tar.zst`. Pierwsza część wskazuje recepturę,
a końcówka zadanie zapisujące pakiety RPM. Klasa `sstate.bbclass`
składa nazwę z danych receptury, `BB_UNIHASH` i nazwy zadania.

**Prognoza zawartości.** W archiwum znajdą się gotowe pakiety RPM.

```bash
cache="$HOME/yocto/rpi-sentinel-wrynose/build-stage2/sstate-cache"
entry="$(find "$cache" -type f \
    -name 'sstate:rpi-sentinel:*_package_write_rpm.tar.zst' -print -quit)"
tar --zstd -tf "$entry"
```

**Wynik.** W `deploy-rpms/cortexa53_nocrypto/` były cztery pliki RPM
z pakietami głównym, `-dbg`, `-dev` i `-src`. Hipoteza się potwierdziła.
Opcja `-t` programu `tar` wypisała zawartość bez zapisu plików na dysk.

**Wniosek.** `.tar.zst` określa sposób spakowania danych. O zawartości
decyduje zadanie. Dla `package_write_rpm` zachowanym wynikiem są
gotowe RPM, które BitBake może odtworzyć przez `setscene`.

## 6. Odczyt sygnatury przez bitbake-dumpsig

**Hipoteza i prognoza.** `basehash` i `taskhash` będą różne.
Pierwszy opisuje własne wejścia i kod zadania. Drugi uwzględnia także
sygnatury jego zależności.

Dla pliku `.siginfo` obok archiwum z eksperymentu 5 wykonano:

```bash
"$HOME/yocto/rpi-sentinel-wrynose/layers/bitbake/bin/bitbake-dumpsig" "$entry.siginfo" |
    grep -E '^(basehash:|Computed task hash|Hash for dependent task rpi-sentinel:do_package is)'
```

**Wynik.** Dla `package_write_rpm` odczytano `basehash`
`b1f0405383d5…` i `taskhash` `52d65a45b1a1…`.
Podgląd zawierał też hash zależnego `rpi-sentinel:do_package`
`8937c979106c…`. Prognoza się potwierdziła.

**Wniosek.** `bitbake-dumpsig` pozwala zobaczyć wejścia używane do
obliczania sygnatury i zależności zadań. Zmiana sygnatury zadania
zależnego może zmienić `taskhash` przy takim samym `basehash`.

## 7. Porównanie sygnatur przez bitbake-diffsigs

**Hipoteza i prognoza.** Dwie sygnatury tego samego zadania mają
różne hashe. `bitbake-diffsigs` wskaże też, które zmienne lub zadania
zależne się zmieniły.

`bitbake-diffsigs` w tej wersji uruchamia serwer BitBake także przy
podaniu dwóch plików wprost. Używa konfiguracji builda do odszukania
sygnatur zależnych zadań. Pierwsza próba z katalogu repozytorium
zakończyła się komunikatem o braku `BBPATH` i `conf/bblayers.conf`.
Poniższy blok inicjalizuje środowisko i przechodzi do `build-stage8`.
Uruchom go z katalogu głównego projektu.

```bash
(
    set -eo pipefail
    artifact_dir="$PWD/artifacts/stage8/07-diffsigs"
    yocto_dir="$HOME/yocto/rpi-sentinel-wrynose"
    cache="$yocto_dir/build-stage2/sstate-cache"
    first="$(find "$cache" -type f -name 'sstate:rpi-sentinel:*:52d65a45*_package_write_rpm.tar.zst.siginfo' -print -quit)"
    second="$(find "$cache" -type f -name 'sstate:rpi-sentinel:*:3b577747*_package_write_rpm.tar.zst.siginfo' -print -quit)"
    test -r "$first" && test -r "$second" || exit 1
    mkdir -p "$artifact_dir"
    source "$yocto_dir/layers/openembedded-core/oe-init-build-env" "$yocto_dir/build-stage8"
    bitbake-diffsigs "$first" "$second" > "$artifact_dir/comparison.txt"
    sed -n '1,8p' "$artifact_dir/comparison.txt"
)
```

**Wynik.** Po inicjalizacji środowiska porównanie doszło przez zależności
do `rpi-sentinel:do_fetch`. Narzędzie wskazało zmienione sumy kontrolne
`app/CMakeLists.txt`, `app/src/main.c` i `rpi-sentinel.service`.
Raport pokazał także zmiany hashy `do_unpack`, `do_patch` i
`do_deploy_source_date_epoch`.

**Wniosek.** Prognoza się potwierdziła. `bitbake-diffsigs` pozwala
przejść od zmienionej sygnatury zadania zależnego do zmienionych wejść.
Zmiana hasha `do_patch` w tym łańcuchu wynika ze zmiany sygnatury
wcześniejszego zadania. Raport nie jest dowodem na zmianę treści patcha
ani listą wykonanych zadań. Ponieważ w tym porównaniu zmieniło się kilka
plików, w kolejnej próbie zmienimy tylko jeden.

## 8. Zmiana jednego pliku źródłowego

**Hipoteza i prognoza.** Zmiana `app/src/main.c` zmieni sygnaturę
`do_fetch`, mimo że wpis `file://app` w `SRC_URI` pozostanie taki sam.

Próbę przygotowano w osobnej kopii utworzonej przez `git archive HEAD`.
`cmp` potwierdził zgodność pliku wyjściowego. W osobnym katalogu
builda skopiowano konfigurację `build-stage8` i skierowano wpis
własnej warstwy w `bblayers.conf` na tę kopię.

Przed zmianą i po niej wykonano `bitbake -S none -c fetch rpi-sentinel`.
W kopii zmieniono tylko tekst pomocy z `Użycie:` na
`Użycie programu:` w `app/src/main.c`. `bitbake-diffsigs`
porównał oba pliki `.sigdata` dla `do_fetch`.

**Wynik porównania.** Pojawiła się nowa sygnatura. Raport wskazał zmianę
sumy `./src/main.c` z `7419d924285485506131b1d773ee24cf` na
`54d43e91450ea74d302e77dd151cafcf`. Prognoza się potwierdziła.
BitBake uwzględnił zawartość lokalnego wejścia, które wskazuje
`file://app`. Generowanie sygnatur przez `-S none` pozwoliło
zaobserwować tę zmianę bez kompilacji.

**Build po zmianie.** Odczytano zachowany `log.do_compile` i znacznik
`do_compile` z katalogu kopii. Log pokazuje wywołanie `cmake --build`,
a znacznik i binarka powstały około 21:02, po zmianie pliku około 20:34.
W binarce AArch64 znaleziono tekst `Użycie programu: %s`, co potwierdza,
że zawiera zmienioną wersję kodu.

**Powtórka.** W logu z 21:55 wszystkie 1935 zadań uznano za aktualne.
`Wanted 0` i `Current 808` wskazują, że w tej powtórce BitBake nie
potrzebował odzyskiwać wyników z sstate. Prognoza ponownej kompilacji
potwierdziła się w zachowanym logu wcześniejszego przebiegu.

```bash
work="$source_test/build/tmp/work/cortexa53-nocrypto-poky-linux/rpi-sentinel/0.1.0"
grep -aFq 'Użycie programu: %s' "$work/build/rpi-sentinel" &&
printf 'Binarka zawiera zmieniony tekst pomocy.\n'
```

**Wniosek.** Zmiana lokalnego pliku wpłynęła na sygnaturę wejść.
Log zadania i nowy tekst w binarce potwierdziły kompilację, a
późniejsze podsumowanie potwierdziło ponowne użycie aktualnych
wyników w tym samym katalogu.

## 9. Zawartość i zależności wygenerowanych RPM

**Hipoteza.** Główny RPM monitora zawiera binarkę, konfigurację
i jednostkę systemd. Biblioteka metryk ma osobny pakiet, a RPM
monitora deklaruje ją jako zależność.

**Prognoza dla zawartości.** Główny RPM pomieści także bibliotekę
`libsentinel-metrics.so.1`.

**Wynik inwentaryzacji.** `rpm -qpl` dla
`rpi-sentinel-0.1.0-r0.cortexa53_nocrypto.rpm` pokazał binarkę,
konfigurację, jednostkę systemd i preset. Na liście plików nie było
biblioteki metryk. Kolejna próba sprawdza pole wymagań pakietu.

**Prognoza dla wymagań.** Biblioteka pojawi się na liście zależności RPM.

```bash
"$rpm_tool" --dbpath "$rpm_query_dir" -qpR "$package" |
    grep -F 'libsentinel-metrics' | sort -u
```

**Wynik.** Metadane RPM zawierały wymagania
`libsentinel-metrics.so.1()(64bit)` i `libsentinel-metrics1 >= 1.0.0`.
Pierwsza prognoza dotycząca plików w pakiecie była błędna.
Druga prognoza dotycząca wymagań była poprawna.

**Wniosek.** Pakiet monitora wymaga biblioteki dostarczanej przez
osobny RPM. Receptura definiuje `DEPENDS` na czas kompilacji.
Po zlinkowaniu aplikacji Yocto wykrywa wymagany SONAME z ELF
i dopisuje zależność między pakietami na czas instalacji obrazu.

## 10. Generowanie i użycie SDK

**Hipoteza i prognoza.** SDK działa na laptopie x86-64 i generuje kod
dla Raspberry Pi 3 w architekturze AArch64. Przewidywanie dotyczy
architektury wynikowego ELF.

**Podgląd.** Polecenie `bitbake -n -c populate_sdk rpi-sentinel-image`
w `build-stage6-wifi-ssh` pokazało `SDKMACHINE=x86_64`,
`MACHINE=raspberrypi3-64` i plan zawierający `do_populate_sdk`.
BitBake zgłosił 1580 brakujących wpisów sstate. Podgląd potwierdza
konfigurację i zakres pracy. Weryfikacja instalatora i architektury
pliku ELF wymaga wykonania kolejnych kroków.

**Pierwsze wykonanie.** Kompilacja `nativesdk-llvm` i `clang-native`
zakończyła się poprawnie. Generowanie SDK przerwał błąd
`nativesdk-python3:do_package`. W `temp/log.do_package.9894` operacja
usuwania katalogu `sstate-build-package` zakończyła się sygnałem
`SIGABRT`. W `pseudo/pseudo.log` wpis `path mismatch [5 links]`
wskazał ten sam inode pliku `bytearrayobject.c` pod dwiema ścieżkami
w katalogu roboczym receptury (`package/` i `sstate-build-package/`).

**Próba naprawy.** W tym samym katalogu budowania uruchom
`bitbake -c clean nativesdk-python3`, a następnie
`bitbake -c package nativesdk-python3`. Pozwala to odtworzyć lokalny
stan tej receptury z zachowaniem współdzielonego sstate. Próba pakowania
zakończyła się powodzeniem. BitBake podsumował 1055 zadań, z których
1047 nie wymagało ponownego wykonania.

**Ponowne generowanie SDK.** `bitbake -c populate_sdk rpi-sentinel-image`
zakończyło się poprawnie. BitBake podsumował 7696 zadań, z których 7077
nie wymagało ponownego wykonania. W `tmp/deploy/sdk/` powstał instalator
`poky-glibc-x86_64-rpi-sentinel-image-cortexa53-nocrypto-raspberrypi3-64-toolchain-6.0.4.sh`.

**Weryfikacja kompilacji skrośnej.** Instalator zakończył się poprawnie.
Po wczytaniu `environment-setup-cortexa53-nocrypto-poky-linux` kompilator
wskazany przez `CC` zbudował minimalny program w C. `readelf -h probe`
zwrócił `Class: ELF64` i `Machine: AArch64`. Wynik potwierdza prognozę
dotyczącą architektury binarki. Zakres próby obejmował kompilację,
linkowanie i odczyt nagłówka ELF.

## 11. Zawartość i rozmiar obrazu

**Hipoteza.** `bzip2` zmniejszy rozmiar obrazu `.wic`. Pliki konfiguracji
są częścią systemów plików w obrazie i podlegają tej samej kompresji.

**Pomiar.** Dla obrazu `rpi-sentinel-image-raspberrypi3-64.rootfs-20261126154208.wic.bz2`
z `artifacts/stage9/module-demo/` polecenie `stat` zwróciło 64 905 440
bajtów. `bzip2 -dc | wc -c` zmierzyło 393 424 896 bajtów po rozpakowaniu.
Kompresja zmniejszyła plik o około 83,5%. Podczas wgrywania obrazu
na kartę zapisywany jest rozpakowany strumień `.wic`.

**Tablica partycji.** `fdisk -l` odczytał startową partycję FAT32 LBA
o rozmiarze 130 MiB (sektory 8192–274431) oraz partycję typu Linux
o rozmiarze około 239,2 MiB (sektory 278528–768407). Początkowy obszar
obrazu i przerwa między partycjami zajmują łącznie 6 MiB. Rozmiar
partycji określa jej pojemność, a typ `Linux` w tablicy partycji
określa identyfikator typu partycji.

**Pakiety w obrazie.** Manifest tego samego obrazu zawiera trzy osobne
wpisy dla sprawdzanych komponentów:

| Pakiet | Architektura pakietu | Wersja |
| --- | --- | --- |
| `rpi-sentinel` | `cortexa53_nocrypto` | `0.1.0` |
| `libsentinel-metrics1` | `cortexa53_nocrypto` | `1.0.0` |
| `kernel-module-rpi-sentinel-demo-6.18.33-v8` | `raspberrypi3_64` | `1.0` |

Receptura obrazu dodaje `rpi-sentinel` przez `IMAGE_INSTALL`. Pakiet
biblioteki trafia do obrazu jako zależność uruchomieniowa aplikacji,
ustalana na podstawie `libsentinel-metrics.so.1`. Pakiet modułu
pochodzi z osobnej receptury `rpi-sentinel-demo`, korzystającej z klasy
`module`. Manifest pokazuje zainstalowane pakiety, a `IMAGE_INSTALL`
jest jednym z wejść do ustalenia ich listy.
