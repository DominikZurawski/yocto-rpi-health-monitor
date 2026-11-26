# Eksperymenty z BitBake

Konfiguracja startowa to Yocto 6.0.4 Wrynose, BitBake 2.18.0, `MACHINE=raspberrypi3-64` i projekt w rewizji `4541d7c`. Używamy osobnego katalogu `~/yocto/rpi-sentinel-wrynose/build-stage8`. Artefakty obserwacji zapisujemy w ignorowanym przez Git katalogu `artifacts/stage8/`.

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
