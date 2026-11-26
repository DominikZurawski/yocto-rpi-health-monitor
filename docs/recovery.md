# Odzyskiwanie działania monitora

Systemd nadzoruje proces `rpi-sentinel`. Aplikacja wysyła `READY=1` po pierwszym zakończonym cyklu, a `WATCHDOG=1` po każdym zakończonym cyklu, jeśli watchdog usługi jest włączony. Powiadomienia pochodzą z wątku wykonującego pomiary, logowanie i obsługę zapisu stanu. Interwał pomiarów wynosi od 1 do 10 s. Aplikacja odrzuca interwał przekraczający połowę limitu otrzymanego od systemd. Domyślna konfiguracja jednostki daje limit 30 s.

Obsłużony błąd pomiaru lub zapisu stanu trafia do dziennika. Błąd odczytu metryki może dać stan `ERROR` z działającym watchdogiem. Watchdog mierzy postęp pętli. Ocena progu i dostępności metryk wynika z danych programu. `TimeoutStartSec=30s` obejmuje okres przed `READY=1`.

Receptura Yocto deklaruje `systemd` w `DEPENDS` na czas kompilacji. Klasa `pkgconfig` kieruje CMake do pliku `libsystemd.pc` w sysroot dla celu. Wymaganą bibliotekę w pakiecie docelowym rozpoznaje mechanizm zależności bibliotek współdzielonych. Test CTest `sentinel-watchdog` odbiera datagramy z binarki hostowej i sprawdza gotowość, kolejne sygnały heartbeat, zachowanie po utracie metryki oraz odrzucenie niezgodnego interwału.

## Przygotowanie prób na Pi

Przed budową ustaw `export SENTINEL_PROFILE=recovery` i skorzystaj z
[instrukcji budowania i wgrywania](yocto-build.md). Ten profil zawiera
pakiet `rpi-sentinel-test-access`, który umożliwia wysłanie `SIGKILL`
i `SIGSTOP` do głównego procesu tej jednej jednostki oraz wykonanie
`reset-failed`. Próby celowo przerywają pomiary. Wykonaj je na urządzeniu
testowym.

Na laptopie ustaw adres Pi i uruchom test bazowy.

```bash
read -r -p 'Adres Raspberry Pi: ' pi
bash scripts/smoke-test.sh live "$pi"
```

Zdefiniuj pomocnicze funkcje w tej samej powłoce. `wait_for_monitor` kończy się powodzeniem dopiero po odczycie nowego identyfikatora aktywnej jednostki.

```bash
pi_ssh() {
    ssh -i "$HOME/.ssh/id_ed25519" -o BatchMode=yes -o ConnectTimeout=10 "developer@$pi" "$@"
}
monitor_invocation() {
    pi_ssh 'systemctl is-active --quiet rpi-sentinel.service &&
        systemctl show -p InvocationID --value rpi-sentinel.service'
}
wait_for_monitor() {
    local previous="$1" limit="$2" current deadline=$((SECONDS + $2))
    while (( SECONDS < deadline )); do
        if current="$(monitor_invocation)" && [[ -n "$current" && "$current" != "$previous" ]]; then
            printf 'Nowy InvocationID: %s\n' "$current"
            return 0
        fi
        sleep 1
    done
    printf 'Nie wykryto nowego aktywnego wywołania w ciągu %s s.\n' "$limit" >&2
    return 1
}
```

## 1. Awaria procesu przez SIGKILL

**Cel i warunki.** Działa jednostka na obrazie testowym. Sprawdzamy restart po natychmiastowym zakończeniu procesu. `reset-failed` czyści licznik uruchomień przed próbą.

```bash
pi_ssh 'sudo -n /usr/bin/systemctl reset-failed rpi-sentinel.service' &&
before="$(monitor_invocation)" && test -n "$before" &&
pi_ssh 'sudo -n /usr/bin/systemctl kill --kill-whom=main --signal=SIGKILL rpi-sentinel.service' &&
wait_for_monitor "$before" 25 &&
bash scripts/smoke-test.sh live "$pi"
pi_ssh 'journalctl -b -u rpi-sentinel.service --no-pager -n 25'
```

**PASS / FAIL.** PASS, gdy `InvocationID` się zmieni, `live` potwierdzi nowe próbki, a dziennik pokaże zakończenie przez `KILL` i zaplanowany restart. Brak oczekiwanych dowodów daje FAIL.

## 2. Zatrzymanie heartbeat przez SIGSTOP

**Cel i warunki.** Działa jednostka na obrazie testowym. `SIGSTOP` wstrzymuje proces, w tym jego główną pętlę. Systemd nadal działa i sprawdza czas od ostatniego heartbeat.

```bash
pi_ssh 'sudo -n /usr/bin/systemctl reset-failed rpi-sentinel.service' &&
before="$(monitor_invocation)" && test -n "$before" &&
pi_ssh 'sudo -n /usr/bin/systemctl kill --kill-whom=main --signal=SIGSTOP rpi-sentinel.service' &&
wait_for_monitor "$before" 150 &&
bash scripts/smoke-test.sh live "$pi"
pi_ssh 'journalctl -b -u rpi-sentinel.service --no-pager -n 25'
```

**PASS / FAIL.** PASS, gdy dziennik wskaże awarię `watchdog`, pojawi się nowy `InvocationID`, a `live` potwierdzi wznowienie pomiarów. Limit czekania uwzględnia czas na zakończenie poprzedniego procesu i ponowne uruchomienie. Brak oczekiwanych dowodów daje FAIL.

## 3. Wielokrotne awarie i limit startów

**Cel i warunki.** Działa jednostka na obrazie testowym. Test zeruje licznik i wykonuje jeden start. Kolejne trzy automatyczne starty zużyją resztę limitu. Czwarty `SIGKILL` powinien zostawić usługę w stanie `failed`.

```bash
(
    set -e
    since="$(pi_ssh 'date +%s')"
    [[ "$since" =~ ^[0-9]+$ ]]
    pi_ssh 'sudo -n /usr/bin/systemctl reset-failed rpi-sentinel.service &&
        sudo -n /usr/bin/systemctl restart rpi-sentinel.service'
    for attempt in 1 2 3 4; do
        before="$(monitor_invocation)"
        test -n "$before"
        pi_ssh 'sudo -n /usr/bin/systemctl kill --kill-whom=main --signal=SIGKILL rpi-sentinel.service'
        if [ "$attempt" -lt 4 ]; then
            wait_for_monitor "$before" 15
        fi
    done
    for attempt in {1..15}; do
        if pi_ssh "systemctl is-failed --quiet rpi-sentinel.service &&
            journalctl -b -u rpi-sentinel.service --since=@$since --no-pager |
            grep -F 'Start request repeated too quickly'"; then
            printf 'PASS — limit startów zablokował kolejne uruchomienie.\n'
            exit 0
        fi
        sleep 1
    done
    printf 'FAIL — brak potwierdzenia blokady startu w stanie failed.\n' >&2
    exit 1
)
pi_ssh 'journalctl -b -u rpi-sentinel.service --no-pager -n 35'
```

Po próbie przywróć usługę poniższym poleceniem także wtedy, gdy test zakończył się błędem.

```bash
pi_ssh 'sudo -n /usr/bin/systemctl reset-failed rpi-sentinel.service &&
    sudo -n /usr/bin/systemctl start rpi-sentinel.service' &&
bash scripts/smoke-test.sh live "$pi"
```

**PASS / FAIL.** PASS, gdy po pierwszych trzech awariach zmienia się `InvocationID`, po czwartej jednostka jest `failed` i dziennik zawiera `Start request repeated too quickly`, a ręczny start przywraca pomiary. Pole `Result` po `SIGKILL` może nadal pokazywać `signal`. Inny przebieg daje FAIL.

## Ograniczenia

- Po czterech startach w oknie 120 s limit blokuje kolejne uruchomienie. Upływ okna pozwala uruchomić usługę ręcznie. Systemd sam nie planuje kolejnej próby po wyczerpaniu limitu.
- Trwały błąd konfiguracji, brak dostępu do pliku lub awaria czujnika wymagają usunięcia przyczyny.
- Programowy watchdog usługi odbiera heartbeat procesu przez systemd. Po przekroczeniu limitu polityka `Restart=on-failure` przywraca samą usługę. Mechanizm wymaga działającego jądra i systemd.
- Sprzętowy watchdog Raspberry Pi korzysta z licznika sprzętowego, który system odświeża przez sterownik. Wygaśnięcie licznika powoduje reset całego urządzenia. W etapie 7 użyliśmy wyłącznie watchdoga usługi.

## Wyniki z Raspberry Pi

Z dokładnością do sekund widocznych w dzienniku restart po `SIGKILL` trwał około 2 s od odnotowania awarii do ponownego startu. Po zatrzymaniu heartbeat watchdog miał limit 30 s, a po odnotowaniu timeoutu usługa wystartowała po około 2 s. W próbie limitu potwierdzono skuteczne ręczne przywrócenie bez pomiaru jego czasu.

| Próba | Wynik | Dowód |
| --- | --- | --- |
| SIGKILL | PASS | O 17:59:03 systemd odnotował `status=9/KILL`, a o 17:59:05 zaplanował restart i uruchomił usługę. Nowy `InvocationID` to `55f8b4931c6b42e79ea10368e3c44437`. `live` potwierdził wznowienie pomiarów. |
| SIGSTOP i watchdog | PASS | O 17:56:45 systemd zapisał `Watchdog timeout (limit 30s)` oraz wynik `watchdog`. O 17:56:47 jednostka wystartowała ponownie. `InvocationID` zmienił się z `64a644622471498c83bd960c957c4668` na `fdc9241b4456414cacb9d1efd2f0ca60`, a `live` potwierdził wznowienie pomiarów. |
| Limit startów i ręczne przywrócenie | PASS | Po pierwszych trzech awariach pojawiły się trzy nowe `InvocationID`. Po czwartej, o 18:00:22, systemd zapisał `Start request repeated too quickly` i odmówił startu jednostki. Pole `Result` pozostało `signal`. Po `reset-failed` i `start` test `live` potwierdził aktywną usługę i aktualizację pliku stanu. Próbka w stanie `NORMAL` zawierała 46,160°C, 11% użycia RAM i 551,750 s czasu pracy urządzenia. |
