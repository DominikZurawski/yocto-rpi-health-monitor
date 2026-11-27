# Diagnostyka RPi Sentinel

## Pi nie pojawia się w sieci

1. Sprawdź, czy używasz obrazu `wifi-ssh` lub `recovery` i czy wykonano
   konfigurację partycji root po zapisie karty.
2. Sprawdź dzierżawy DHCP i obecność laptopa w tej samej sieci. Jeśli router
   nie pokazuje dzierżaw, włącz Pi i porównaj listę urządzeń własnej podsieci
   przed i po starcie. Model B testowano z siecią 2,4 GHz.
3. Jeżeli Pi nadal jest niewidoczne, użyj lokalnej konsoli albo
   [diagnostyki z karty](#diagnostyka-z-karty-bez-sieci). Dziennik z karty
   jest dostępny po wcześniejszym włączeniu trwałego journalu.

Na urządzeniu zacznij od `ip address show dev wlan0` i dziennika:

```sh
journalctl -b -u stage2-wifi@wlan0.service -u systemd-networkd.service --no-pager |
    grep -Ei 'CONNECTED|DHCP|auth|timed out' | tail -n 20
journalctl -k -b --no-pager | grep -Ei 'brcmfmac|firmware|regulatory' | tail -n 20
```

Brak `wlan0` kieruje do sterownika i firmware. Zdarzenie `CONNECTED`
z następującym wpisem DHCP potwierdza asocjację i przydział adresu.
Przekroczenie czasu uwierzytelniania wymaga sprawdzenia trybu WPA,
pasma i konfiguracji sieci na karcie.

## Diagnostyka z karty bez sieci

Wyłącz Pi i włóż kartę do laptopa. W katalogu repozytorium ustal aktualną
ścieżkę partycji ext4 z etykietą `root`. Zamontuj ją, jeśli system jeszcze
tego nie zrobił, i włącz zbieranie dziennika:

```bash
lsblk -o PATH,FSTYPE,LABEL,MOUNTPOINTS
read -r -p 'Partycja root karty: ' root_partition
if ! findmnt -rn -S "$root_partition" >/dev/null; then
    udisksctl mount -b "$root_partition"
fi
bash scripts/diagnose-wifi-card.sh enable "$root_partition"
```

Skrypt ustawia limit dziennika na 16 MiB i włącza dodatkowe komunikaty
negocjacji WPA2. Odmontowuje partycję `root` oraz partycję `boot`, jeśli
jest zamontowana. Przełóż kartę do Pi, pozwól mu pracować około dwóch
minut, po czym wyłącz urządzenie i przełóż kartę z powrotem do laptopa.
Sprawdź ścieżkę ponownie, ponieważ nazwa urządzenia może się zmienić.

```bash
lsblk -o PATH,FSTYPE,LABEL,MOUNTPOINTS
read -r -p 'Partycja root karty: ' root_partition
if ! findmnt -rn -S "$root_partition" >/dev/null; then
    udisksctl mount -b "$root_partition"
fi
bash scripts/diagnose-wifi-card.sh read "$root_partition"
```

Tryb `read` pokazuje wybrane wpisy Wi-Fi, DHCP, SSH i jądra z ostatniego
zapisanego rozruchu. Wgranie nowego obrazu usuwa ustawienia trwałego
dziennika z karty.

## SSH odrzuca połączenie

- `Connection refused` oznacza, że trzeba sprawdzić adres urządzenia i
  nasłuch `sshd.socket`.
- `Permission denied (publickey)` uzasadnia sprawdzenie konta `developer`,
  wybranego klucza prywatnego oraz przypisanego mu klucza publicznego na karcie.
- Po wgraniu obrazu klucz hosta może się zmienić. Porównaj nowy odcisk
  z `ssh-keygen -lf` dla właściwego pliku `ssh_host_*_key.pub` odczytanego
  z partycji root karty lub lokalnej konsoli. Po potwierdzeniu usuń stary
  wpis przez `ssh-keygen -R "$pi"` i połącz się ponownie.

## Usługa nie działa lub status stoi w miejscu

```sh
systemctl status --no-pager rpi-sentinel.service
systemctl cat rpi-sentinel.service
journalctl -b -u rpi-sentinel.service --no-pager -n 30
ls -ld /run/rpi-sentinel
ls -l /usr/bin/rpi-sentinel /etc/rpi-sentinel.conf /run/rpi-sentinel/status
```

| Objaw | Co sprawdzić |
| --- | --- |
| `203/EXEC` | Ścieżkę `ExecStart`, prawo wykonywania, architekturę binarki i dostępność interpretera ELF. |
| Błąd konfiguracji | Pierwszy błąd w journalu, jednostki i zakres wartości z przykładowego pliku. |
| `available: false` | Kod `io_error`, typ strefy termicznej i dostępność plików `/proc`. |
| Brak aktualizacji JSON | Uprawnienia katalogu utworzonego przez `RuntimeDirectory` dla `DynamicUser` oraz błędy publikacji w journalu. |
| `Watchdog timeout` | Przyczynę blokady w głównej pętli i moment ostatniego ukończonego cyklu. |
| `Start request repeated too quickly` | Przyczynę wcześniejszych awarii. Administrator po jej usunięciu wykonuje `systemctl reset-failed rpi-sentinel.service` i `systemctl start rpi-sentinel.service`. |

## Błąd budowania Yocto

- Sprawdź czystość i rewizje repozytoriów z `sources.lock`, a potem wynik
  `bitbake-layers show-layers` oraz `bitbake -p`.
- Otwórz wskazany w błędzie log `log.do_*` w `temp/` katalogu roboczego
  receptury. Przy `do_rootfs` sprawdź komunikat menedżera pakietów przed
  końcowym podsumowaniem BitBake.
- Przy rozbieżności konfiguracji kernela porównaj fragment z końcowym
  `.config` i przejrzyj wynik `kernel_configcheck`.
- Przy nieudanym uruchomieniu dynamicznie linkowanej binarki sprawdź
  `readelf -d` na hoście i obecność pakietu z wymaganym SONAME w manifeście.
