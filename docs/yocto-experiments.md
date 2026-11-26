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
