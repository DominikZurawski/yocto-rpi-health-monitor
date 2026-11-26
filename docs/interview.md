# Rozmowa rekrutacyjna

## Yocto i BitBake

1. **Po co aplikacji `DEPENDS` na bibliotekę, skoro Yocto wykrywa jej zależność uruchomieniową?**

   `DEPENDS` ustala zależność między recepturami podczas budowy i udostępnia
   pliki biblioteki w sysroot. `shlibdeps` później odczytuje potrzebny SONAME
   z ELF i dopisuje zależność między pakietami instalowanymi w obrazie.
2. **Jakie role pełnią Yocto, BitBake, OE-Core, Poky i BSP?**

   Yocto Project dostarcza narzędzia i praktyki budowania Linuksa.
   BitBake wykonuje graf zadań. OE-Core dostarcza podstawowe receptury
   i klasy. Poky jest konfiguracją referencyjną. BSP opisuje wsparcie
   konkretnej platformy, w tym kernel i elementy rozruchu.
3. **Co oznacza strzałka w grafie `bitbake -g`?**

   Prowadzi od zadania do jego wymagania. W naszym grafie przygotowanie
   sysroot monitora zależy od `systemd.do_populate_sysroot`, które w swoim
   łańcuchu wymaga instalacji i kompilacji systemd.
4. **Czym różni się znacznik `stamp` od sstate?**

   Znacznik pozwala ocenić, czy zadanie wykonano dla danej sygnatury
   w bieżącym buildzie. Sstate przechowuje wybrane wyniki, które BitBake
   może odtworzyć także w innym katalogu. Porównanie sygnatur pomaga
   ustalić, co zmieniło wejścia zadania.
5. **Jak sprawdzić, czy zadziałał `.bbappend` kernela?**

   `bitbake-layers show-appends` potwierdza dopasowanie receptury.
   `bitbake -e` pokazuje efektywne metadane. `kernel_configcheck`
   i końcowy `.config` sprawdzają efekt Kconfig, a odczyt z działającego
   kernela potwierdza poprawny obraz na urządzeniu.

## Embedded Linux

1. **Jak różnią się procfs i sysfs w tym projekcie?**

   Procfs udostępnia m.in. `meminfo` i `uptime`. Sysfs odzwierciedla
   obiekty i ich atrybuty wystawiane przez kernel. Temperatura i typ
   strefy pochodzą z `/sys/class/thermal`.
2. **Dlaczego używamy `MemAvailable` zamiast `MemFree`?**

   `MemAvailable` uwzględnia szacowaną pamięć możliwą do odzyskania.
   Daje użyteczniejszą podstawę do oceny zapasu RAM niż liczba całkowicie
   wolnych stron. Monitor liczy wykorzystanie z różnicy `MemTotal`
   i `MemAvailable`.
3. **Po co zegar monotoniczny w pętli pomiarowej?**

   Ręczna zmiana zegara lub korekta czasu sieciowego może przesunąć
   czas ścienny. `CLOCK_MONOTONIC` mierzy upływ czasu potrzebny do
   wyznaczania odstępów pomiarowych.
4. **Co daje atomowy `rename()` przy zapisie statusu?**

   Po zapisaniu pliku tymczasowego w tym samym katalogu `rename()`
   podmienia wpis katalogowy. Nowy czytelnik dostaje poprzedni albo
   cały nowy plik. Otwarty wcześniej deskryptor nadal wskazuje poprzednią
   wersję. Trwałość po zaniku zasilania wymaga odrębnych mechanizmów.
5. **Co się dzieje między `SIGTERM` a zakończeniem procesu?**

   Procedura sygnału ustawia flagę `sig_atomic_t`. `pselect()`
   atomowo odblokowuje sygnał w trakcie oczekiwania. Pętla sprawdza
   flagę i kończy się w normalnym przebiegu programu.

## Architektura projektu

1. **Gdzie znajduje się granica między biblioteką a aplikacją?**

   Biblioteka odczytuje i waliduje metryki, zwraca jednostki oraz kody
   błędów. Aplikacja zarządza progami, formatem statusu, logowaniem,
   planowaniem cyklu i powiadomieniami systemd.
2. **Jak działa histereza na przykładzie temperatury?**

   Przy progu `WARNING` 75°C i histerezie 2°C stan pozostaje
   `WARNING` przy 73°C. Do `NORMAL` wraca po zejściu poniżej 73°C,
   np. przy 72,999°C. To ogranicza przełączanie na granicy progu.
3. **Dlaczego heartbeat wychodzi z głównej pętli?**

   Potwierdza ukończenie cyklu: odczytu, logowania i obsługi publikacji.
   Oddzielny wątek mógłby wysyłać heartbeat pomimo blokady pętli.
   Obsłużony błąd metryki daje `ERROR` i pozwala pętli działać dalej.
4. **Dlaczego `Type=notify` i dynamiczny użytkownik?**

   `Type=notify` pozwala zgłosić gotowość po pierwszym cyklu.
   `DynamicUser` przydziela tożsamość usłudze, a `RuntimeDirectory`
   tworzy miejsce do zapisu statusu z odpowiednimi uprawnieniami.
5. **Czym różni się watchdog usługi od sprzętowego?**

   Watchdog usługi wykrywa brak powiadomień procesu, po czym
   systemd może zrestartować usługę. Watchdog sprzętowy po wygaśnięciu
   licznika resetuje urządzenie. Projekt implementuje recovery usługi.

## Debugowanie i testowanie

1. **Od czego zaczynasz, gdy Pi się uruchamia, a SSH nie działa?**

   Sprawdzam obecność `wlan0`, potem asocjację i DHCP w dzienniku,
   następnie nasłuch SSH i klucz. Każdy krok odróżnia inną warstwę
   problemu.
2. **Jak rozróżniasz awarię procesu od zawieszenia?**

   `SIGKILL` kończy proces i pozwala sprawdzić politykę restartu.
   `SIGSTOP` zatrzymuje postęp. Brak heartbeat wywołuje timeout,
   widoczny w journalu. Nowy `InvocationID` potwierdza kolejne
   uruchomienie w tym samym bootowaniu.
3. **Dlaczego testy na hoście używają fixture?**

   Stałe wejścia pozwalają sprawdzić jednostki, progi, brak plików
   i błędne dane z powtarzalnym wynikiem. Osobne próby na Pi potwierdzają
   rzeczywiste wejścia, start usługi i reakcję systemd.
4. **Jak sprawdzić, czy status nadal jest aktualizowany?**

   Odczytuję dwie poprawne próbki JSON i porównuję `sample_uptime_ms`.
   Rosnąca wartość potwierdza publikację nowszego odczytu. Stan usługi
   i wpisy w journalu pomagają znaleźć przyczynę zatrzymania.
5. **Co robisz, gdy systemd blokuje kolejne starty?**

   Czytam błędy i komunikat o zbyt częstych startach w journalu.
   Po usunięciu przyczyny wykonuję `reset-failed` i `start`, a następnie
   sprawdzam aktywność i wznowienie publikacji przez smoke test.
