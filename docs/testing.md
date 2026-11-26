# Testy sprzętowe RPi Sentinel

Testy wykonuje się na Raspberry Pi 3 z obrazem `rpi-sentinel-image` zbudowanym dla `raspberrypi3-64`. Polecenia w tym dokumencie uruchamiaj na laptopie.

## Podsumowanie weryfikacji

| Obszar | Środowisko i rzeczywisty wynik | Dowód |
| --- | --- | --- |
| Kompilacja aplikacji i biblioteki | Host Ubuntu, `-Wall -Wextra -Werror`, 4/4 CTest PASS | Końcowy przegląd lokalny, czas CTest 6,21 s. |
| CI aplikacji z biblioteką | GitHub Actions, PASS potwierdzony po publikacji zmian biblioteki | Workflow `application-ci.yml` uruchamia cztery testy CTest. |
| Build i QA pakietów | Yocto 6.0.4, obie receptury przeszły `do_package_qa` | Manifest obrazu z biblioteką zawierał `libsentinel-metrics1`. Metadane aplikacji zawierały zależność od biblioteki. |
| Konfiguracja builda publikacyjnego | Świeże katalogi BitBake, 3/3 profile poprawnie sparsowane | Z istniejących, zweryfikowanych checkoutów upstream. Sprawdzono efektywne metadane obrazu dla `base`, `wifi-ssh` i `recovery`. |
| Rozruch, Wi-Fi, SSH i monitor | Raspberry Pi 3 Model B Rev 1.2, PASS | [Wyniki poszczególnych prób](#wyniki-z-raspberry-pi). |
| Awaria, watchdog i limit startów | Raspberry Pi 3 Model B Rev 1.2, 3/3 PASS | [Wyniki recovery](recovery.md#wyniki-z-raspberry-pi). |
| Monitor po wydzieleniu biblioteki | Raspberry Pi 3 Model B Rev 1.2, `live` PASS | [Próbka na obrazie z biblioteką](build-guide.md#biblioteka-metryk-w-yocto). |
| Kernel z wbudowanym IKCONFIG | Host, kontrola konfiguracji, ekstrakcja i build obrazu PASS. Próba tego obrazu na Pi odłożona. | [Zmiana kernela i sposób powtórzenia](build-guide.md#etap-9--konfiguracja-kernela). |

Próby dotyczyły obrazów z kolejnych etapów. Dla wydania powtórz
uzgodniony smoke test na obrazie z commita przeznaczonego do tagowania.

## 1. Zawartość obrazu i pierwszy rozruch

**Cel.** Sprawdzić zawartość własnego obrazu, uruchomić go z microSD i potwierdzić dostęp po Wi-Fi i SSH.

**Warunki początkowe.** Użyj obrazu z profilem `wifi-ssh` do podstawowych
prób lub profilu `recovery` do prób wymagających uprawnień testowych.
Przygotuj kartę microSD oraz parę kluczy SSH.

**Polecenia.** Zbuduj i zweryfikuj obraz, zapisz kartę oraz skonfiguruj
Wi-Fi zgodnie z [instrukcją od świeżego klonu](yocto-build.md).
Po rozruchu ustaw adres Pi i wykonaj:

```bash
read -r -p 'Adres Raspberry Pi: ' pi
ssh -i "$HOME/.ssh/id_ed25519" "developer@$pi" \
    'uname -m && cat /proc/1/comm && cat /proc/sys/kernel/random/boot_id'
```

**Oczekiwany wynik.** Manifest zawiera `rpi-sentinel`, `systemd` i `openssh-sshd`. Profil `recovery` zawiera również `rpi-sentinel-test-access`. SSH z kluczem zwraca `aarch64`, `systemd` i identyfikator rozruchu.

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
