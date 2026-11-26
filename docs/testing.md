# Testy sprzętowe RPi Sentinel

Testy wykonuje się na Raspberry Pi 3 z obrazem `rpi-sentinel-image` zbudowanym dla `raspberrypi3-64`. Polecenia w tym dokumencie uruchamiaj na laptopie.

## 1. Zawartość obrazu i pierwszy rozruch

**Cel.** Sprawdzić zawartość własnego obrazu, uruchomić go z microSD i potwierdzić dostęp po Wi-Fi i SSH.

**Warunki początkowe.** Build `build-stage5` zawiera warstwę `meta-rpi-sentinel`. Masz kopię danych z karty, zapisany lokalny profil Wi-Fi `.local/wifi-default` i odpowiadającą sobie parę kluczy SSH. Polecenie zapisu karty nadpisuje całe wybrane urządzenie.

**Polecenia.** W katalogu repozytorium uruchom poniższy blok. Osobny katalog `build-stage6-wifi-ssh` pozwala porównać obraz testowy z obrazem z etapu 5. `bitbake -p` sprawdza, czy BitBake może wczytać konfigurację i receptury, przed uruchomieniem builda.

```bash
(
    set -eo pipefail
    project_dir="$PWD"
    yocto_dir="$HOME/yocto/rpi-sentinel-wrynose"
    stage5_dir="$yocto_dir/build-stage5"
    build_dir="$yocto_dir/build-stage6-wifi-ssh"
    artifact_dir="$project_dir/artifacts/stage6-wifi-ssh"

    source "$yocto_dir/layers/openembedded-core/oe-init-build-env" "$build_dir"
    cp -- "$stage5_dir/conf/bblayers.conf" conf/bblayers.conf
    cp -- "$project_dir/build-config/stage2.conf" conf/stage2.conf
    cp -- "$project_dir/build-config/stage2-wifi-ssh.conf" conf/
    install -D -m 0644 "$project_dir/build-config/classes/stage2-wifi-ssh.bbclass" \
        classes/stage2-wifi-ssh.bbclass
    cp -- "$stage5_dir/conf/local.conf" conf/local.conf
    cat >> conf/local.conf <<'CONFIG'

require conf/stage2-wifi-ssh.conf
IMAGE_INSTALL:append:pn-rpi-sentinel-image = " rpi-sentinel-test-access"
CONFIG

    bitbake -p
    bitbake rpi-sentinel-image

    deploy_dir="$(bitbake-getvar --value DEPLOY_DIR_IMAGE)"
    machine="$(bitbake-getvar --value MACHINE)"
    image="$(readlink -e -- "$deploy_dir/rpi-sentinel-image-$machine.rootfs.wic.bz2")"
    manifest="$(readlink -e -- "$deploy_dir/rpi-sentinel-image-$machine.rootfs.manifest")"
    for package in rpi-sentinel systemd openssh-sshd rpi-sentinel-test-access sudo; do
        grep -E "^$package[[:space:]]" "$manifest"
    done

    mkdir -p -- "$artifact_dir"
    cp -- "$image" "$manifest" "$artifact_dir/"
    printf 'Obraz do wgrania\n%s/%s\n' "$artifact_dir" "${image##*/}"
    sha256sum -- "$artifact_dir/${image##*/}"
)
```

Pakiet `rpi-sentinel-test-access` dodaje regułę w `/etc/sudoers.d/rpi-sentinel-test`. Tylko ten build dołącza pakiet do obrazu. Receptura wybiera ścieżkę do `systemctl` przez `base_bindir`, a `RDEPENDS` zapewnia obecność `sudo` na urządzeniu. Konto `developer` potrzebuje klucza do SSH i może przez `sudo -n` wykonywać polecenia z listy testowej. Testy awarii monitora opisuje [procedura recovery](recovery.md).

Po buildzie znajdź kartę poleceniem `lsblk`. Podaj ścieżkę całej karty, ścieżkę obrazu i sumę wyświetloną powyżej. Skrypt poprosi o potwierdzenie urządzenia. Po zapisie użyje lokalnego profilu Wi-Fi i wgra klucz publiczny na kartę. Przeskanuj sieć: `sudo nmap -sn 192.168.1.0/24`

```bash
lsblk -o PATH,SIZE,MODEL,TRAN,TYPE,MOUNTPOINTS
read -r -p 'Urządzenie całej karty microSD: ' card
read -r -p 'Ścieżka obrazu .wic.bz2: ' image
read -r -p 'Suma SHA256 obrazu: ' image_sha
bash scripts/remove-deploy-card.sh "$card" "$image" "$image_sha" "$HOME/.ssh/id_ed25519.pub"
```

Przełóż kartę do Pi i włącz zasilanie. Użyj adresu przydzielonego przez domowy router. W poprzednim teście urządzenie miało `192.168.1.10`. Po zmianie obrazu sprawdź odcisk nowego klucza hosta. Możesz go porównać z odciskiem wyliczonym poleceniem `ssh-keygen -lf` dla odpowiadającego pliku `/etc/ssh/ssh_host_*_key.pub` na partycji root karty po pierwszym starcie. Jeśli SSH zgłasza zmianę klucza, po zweryfikowaniu odcisku usuń poprzedni wpis poleceniem `ssh-keygen -R "$pi"` i ponów logowanie.

```bash
read -r -p 'Adres Raspberry Pi: ' pi
ssh -i "$HOME/.ssh/id_ed25519" "developer@$pi" \
    'uname -m && cat /proc/1/comm && cat /proc/sys/kernel/random/boot_id'
```

**Oczekiwany wynik.** Manifest zawiera wszystkie sprawdzane pakiety. SSH z kluczem zwraca `aarch64`, `systemd` i identyfikator rozruchu.

**PASS / FAIL.** PASS po sprawdzeniu manifestu i udanym rozruchu tego obrazu. Brak pakietu, brak rozruchu lub nieudane połączenie SSH daje FAIL.

## 2. Instalacja i stan usługi systemd

**Cel.** Potwierdzić obecność aplikacji, jej konfiguracji i jednostki oraz autostart usługi.

**Warunki początkowe.** Pi działa na obrazie testowym. Zweryfikowano klucz hosta, a zmienna `pi` zawiera aktualny adres. Jeżeli klucz prywatny jest zabezpieczony hasłem, załaduj go do agenta SSH poleceniem `ssh-add "$HOME/.ssh/id_ed25519"`. Skrypt używa trybu bez monitów.

**Polecenie.** Ten sam tryb wykonuje również testy 3 i 4. Uruchom go raz i zachowaj cały krótki wynik.

```bash
bash scripts/smoke-test.sh live "$pi"
```

**Oczekiwany wynik.** Konto `developer` widzi wykonywalną binarkę, czytelny `/etc/rpi-sentinel.conf` i jednostkę `rpi-sentinel.service`. Systemd zgłasza `enabled` i `active`.

**PASS / FAIL.** PASS po linii z potwierdzeniem binarki, konfiguracji i jednostki. Brak któregokolwiek pliku lub inny stan usługi daje FAIL.

## 3. Dziennik bieżącego rozruchu

**Cel.** Sprawdzić, czy log pomiaru trafia do dziennika jednostki.

**Warunki początkowe.** Usługa jest aktywna, a konto SSH ma dostęp do journalu przez grupę `systemd-journal`.

**Polecenia.**

```bash
bash scripts/smoke-test.sh live "$pi"
ssh -i "$HOME/.ssh/id_ed25519" "developer@$pi" \
    'journalctl -b -u rpi-sentinel.service --no-pager -n 5'
```

**Oczekiwany wynik.** W dzienniku bieżącego rozruchu pojawia się co najmniej jedna próbka JSON z usługi.

**PASS / FAIL.** PASS po linii potwierdzającej próbki w dzienniku. Błąd odczytu lub brak próbki daje FAIL.

## 4. Rzeczywiste pomiary i aktualizacja pliku stanu

**Cel.** Sprawdzić odczyt temperatury CPU, RAM i czasu pracy z systemu Pi oraz publikowanie kolejnej próbki.

**Warunki początkowe.** Usługa działa z konfiguracją z obrazu. Laptop ma `python3` do sprawdzenia JSON.

**Polecenie.**

```bash
bash scripts/smoke-test.sh live "$pi"
```

**Oczekiwany wynik.** Dwa odczyty `/run/rpi-sentinel/status` zawierają dostępne i poprawne metryki. `sample_uptime_ms` rośnie w drugiej próbce.

**PASS / FAIL.** PASS po linii potwierdzającej nowszą próbkę, z kodem wyjścia 0. Niepoprawny JSON, niedostępna metryka lub brak aktualizacji w ciągu 12 sekund daje FAIL.

## 5. Przekroczenie progu ostrzegawczego

**Cel.** Sprawdzić, czy binarka z obrazu przechodzi z `NORMAL` do `WARNING` po przekroczeniu 75°C.

**Warunki początkowe.** Pi działa, a użytkownik `developer` może utworzyć katalog tymczasowy. Skrypt uruchamia drugą instancję na kontrolowanych danych za pomocą `--proc-root` i `--thermal-root`.

**Polecenie.** Wykonuje również test 6.

```bash
bash scripts/smoke-test.sh alarm "$pi"
```

**Oczekiwany wynik.** Stan przy 60°C to `NORMAL`, a przy 76°C to `WARNING`. Dziennik instancji testowej zawiera przejście `NORMAL -> WARNING`.

**PASS / FAIL.** PASS, gdy obie próbki i wpis o przejściu są obecne. Limit oczekiwania na próbkę lub niewłaściwy stan daje FAIL.

## 6. Powrót do NORMAL z histerezą

**Cel.** Sprawdzić powrót do `NORMAL` po spadku temperatury poniżej granicy wynikającej z histerezy.

**Warunki początkowe.** W tej samej instancji testowej poprzedni stan to `WARNING`, próg wynosi 75°C, a histereza 2°C.

**Polecenie.**

```bash
bash scripts/smoke-test.sh alarm "$pi"
```

**Oczekiwany wynik.** Próbka 72,999°C daje `NORMAL`. Dziennik zawiera `WARNING -> NORMAL`. Instancja testowa kończy się po SIGTERM z kodem 0.

**PASS / FAIL.** PASS po potwierdzeniu próbki, przejścia i zakończeniu skryptu z kodem 0. Brak któregokolwiek potwierdzenia daje FAIL.

## 7. Zmiana progu w konfiguracji

**Cel.** Potwierdzić, że obniżenie progu przy stałej temperaturze zmienia wynik oceny.

**Warunki początkowe.** Działa SSH. Test może działać również na poprzednim obrazie. Osobna instancja korzysta z testowej konfiguracji i próbki 70°C.

**Polecenie.**

```bash
bash scripts/smoke-test.sh threshold "$pi"
```

**Oczekiwany wynik.** Pierwsze uruchomienie z progiem 75°C daje `NORMAL`. Drugie uruchomienie z progiem 65°C daje `WARNING` przy tej samej temperaturze. Skrypt ponownie uruchamia instancję, aby odczytała zmieniony plik konfiguracji.

**PASS / FAIL.** PASS po uzyskaniu obu stanów i zakończeniu skryptu z kodem 0. Błędny stan, brak próbki lub nieudane zakończenie instancji daje FAIL.

## 8. Zatrzymanie i restart usługi

**Cel.** Sprawdzić zatrzymanie rzeczywistej jednostki, ponowne uruchomienie oraz działanie polecenia `restart`.

**Warunki początkowe.** Pi działa na nowym obrazie z pakietem `rpi-sentinel-test-access`. Usługa jest aktywna. Ten test na krótko zatrzymuje pomiary.

**Polecenia.**

```bash
ssh -i "$HOME/.ssh/id_ed25519" "developer@$pi" sh -s <<'REMOTE'
set -eu
unit=rpi-sentinel.service
initial="$(systemctl show -p InvocationID --value "$unit")"
test -n "$initial"

# W razie niepowodzenia po zatrzymaniu spróbuj wznowić monitor.
trap 'sudo -n /usr/bin/systemctl start "$unit"' EXIT
sudo -n /usr/bin/systemctl stop "$unit"
test "$(systemctl show -p ActiveState --value "$unit")" = inactive
test "$(systemctl show -p Result --value "$unit")" = success
printf 'PASS — jednostka została poprawnie zatrzymana.\n'

sudo -n /usr/bin/systemctl start "$unit"
systemctl is-active --quiet "$unit"
started="$(systemctl show -p InvocationID --value "$unit")"
test -n "$started"
test "$started" != "$initial"
printf 'PASS — jednostka ponownie uruchomiona.\n'

sudo -n /usr/bin/systemctl restart "$unit"
systemctl is-active --quiet "$unit"
restarted="$(systemctl show -p InvocationID --value "$unit")"
test -n "$restarted"
test "$restarted" != "$started"
printf 'PASS — restart jednostki utworzył nowe wywołanie.\n'
trap - EXIT
REMOTE
```

**Oczekiwany wynik.** Po `stop` stan to `inactive` i wynik to `success`. Po `start` oraz `restart` stan to `active` i pojawia się nowy identyfikator wywołania.

**PASS / FAIL.** PASS po trzech potwierdzeniach i wyjściu z kodem 0. Błąd polecenia lub niewłaściwy stan daje FAIL.

## 9. Restart całego Raspberry Pi

**Cel.** Potwierdzić nowy rozruch, ponowne zestawienie Wi-Fi i SSH oraz automatyczny start monitora.

**Warunki początkowe.** Pi działa na nowym obrazie z pakietem `rpi-sentinel-test-access`. Usługa jest aktywna. Zapisz pracę na Pi przed restartem.

**Polecenia.** Najpierw zapisz identyfikator rozruchu na laptopie.

```bash
boot_before="$(ssh -i "$HOME/.ssh/id_ed25519" "developer@$pi" \
    cat /proc/sys/kernel/random/boot_id)" && printf '%s\n' "$boot_before"
```

Następnie wykonaj restart przez SSH. Połączenie może zostać przerwane przez restart. Dowodem rozruchu jest nowy identyfikator w następnym kroku.

```bash
ssh -i "$HOME/.ssh/id_ed25519" "developer@$pi" \
    'sudo -n /usr/bin/systemctl reboot'
```

Po ponownym połączeniu z siecią wróć do tej samej powłoki na laptopie. Zaktualizuj zmienną `pi`, jeśli router zmienił adres.

```bash
boot_after="$(ssh -i "$HOME/.ssh/id_ed25519" "developer@$pi" \
    cat /proc/sys/kernel/random/boot_id)" &&
printf 'Przed: %s\nPo:    %s\n' "$boot_before" "$boot_after" &&
test -n "$boot_before" && test -n "$boot_after" &&
test "$boot_before" != "$boot_after" &&
bash scripts/smoke-test.sh live "$pi"
```

**Oczekiwany wynik.** Identyfikatory są różne, SSH działa, a test `live` ponownie potwierdza stan usługi i nowe próbki.

**PASS / FAIL.** PASS po zmianie identyfikatora i zakończeniu bloku z kodem 0. Jeżeli faktycznie wykonano restart, ten sam identyfikator, brak SSH lub nieudany test `live` daje FAIL.

## Wyniki z Raspberry Pi

| Test | Wynik | Dowód |
| --- | --- | --- |
| 1. Obraz i rozruch | PASS | Manifest obrazu z 25.11.2026 zawiera `rpi-sentinel`, `systemd` i `openssh-sshd`. Po rozruchu SSH zwróciło `aarch64`, `systemd` i identyfikator rozruchu. |
| 2. Instalacja i usługa | PASS | `live` potwierdził binarkę i konfigurację. `systemctl status` pokazał `Loaded: loaded (...; enabled; preset: enabled)` oraz `Active: active (running)`. |
| 3. Dziennik | PASS | `journalctl -b -u rpi-sentinel.service` pokazał próbki JSON z bieżącego rozruchu. |
| 4. Pomiary i plik stanu | PASS | `live` potwierdził nowszą próbkę. Przykładowy odczyt w stanie `NORMAL` to 39,704°C, 10% użycia RAM i 344,780 s czasu pracy. |
| 5. Przekroczenie progu | PASS | `alarm` odnotował `NORMAL` przy 60°C, `WARNING` przy 76°C i wpis o zmianie stanu. |
| 6. Powrót do NORMAL | PASS | `alarm` odnotował `NORMAL` przy 72,999°C oraz wpis `WARNING -> NORMAL` po poprawnym zakończeniu instancji testowej. |
| 7. Zmiana progu w konfiguracji | PASS | Na Pi próbka 70°C dała `NORMAL` przy progu 75°C i `WARNING` po obniżeniu progu do 65°C i ponownym uruchomieniu testowej instancji. |
| 8. Zatrzymanie i restart usługi | PASS | Po `stop` jednostka była `inactive` z wynikiem `success`. Po `start` i `restart` jednostka wróciła do `active` i za każdym razem otrzymała nowy `InvocationID`. |
| 9. Restart Pi | PASS | Po restarcie przez SSH `boot_id` zmienił się z `8556721c-0823-4e03-a82b-eb9257883056` na `f5482a6a-1ba3-47dc-9286-6fa6b1b16b1d`. Ponowny `live` potwierdził autostart, próbki w journalu i aktualizację stanu. Próbka w stanie `NORMAL` zawierała 46,160°C, 12% użycia RAM i 17,550 s czasu pracy. |
