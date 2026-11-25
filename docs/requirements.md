# Wymagania RPi Sentinel

## Wymagania funkcjonalne

| ID | Wymaganie | Kryterium akceptacji |
| --- | --- | --- |
| F-01 | Własny minimalny obraz dla Raspberry Pi 3. | Obraz uruchamia się z microSD, udostępnia lokalną konsolę i automatycznie uruchamia usługę Sentinel. |
| F-02 | Pomiar temperatury CPU, zużycia RAM i czasu od rozruchu. | Odczyty zgadzają się z interfejsami Linuksa z uwzględnieniem jednostek i odstępu między odczytami. Brak danych oznacza stan zdegradowany. |
| F-03 | Konfigurowalny interwał oraz progi temperatury i RAM z histerezą. | Test potwierdza stany `NORMAL`, `WARNING` i `ERROR`, przejścia na granicach progów oraz odrzucenie błędnej konfiguracji. Uptime jest informacyjny. |
| F-04 | Dziennik zdarzeń i plik `/run/rpi-sentinel/status`. | Dziennik zapisuje pomiary, zmiany stanów i dostępności odczytów. Po pierwszej publikacji równoległy czytelnik widzi kompletny poprzedni lub nowy JSON. |
| F-05 | Odtworzenie działania procesu i systemu za pomocą osobnych watchdogów. | Testy na urządzeniu obejmują restart po awarii i zawieszeniu oraz limit kolejnych restartów. Osobny test potwierdza reset sprzętowy po ustaniu odświeżania i automatyczny start usługi po rozruchu. |

## Wymagania niefunkcjonalne

| ID | Wymaganie | Kryterium akceptacji |
| --- | --- | --- |
| NF-01 | Praca offline na dedykowanym koncie bez uprawnień roota. | Pomiary, alarmy, publikacja i nadzór działają przy odłączonej sieci. UID usługi jest różny od zera. |
| NF-02 | Ograniczenie zapisów i obraz konsolowy. | Stan i dziennik używają pamięci ulotnej. Manifest obrazu nie zawiera środowiska graficznego ani narzędzi kompilacji dla urządzenia. |
| NF-03 | Weryfikacja na hoście i urządzeniu. | CTest sprawdza parsery, walidację, histerezę i błędy odczytu. Test płytki obejmuje rozruch, odczyty, publikację stanu i oba mechanizmy recovery. Zapisujemy model płytki, testowany obraz i wyniki. |
| NF-04 | Odtwarzalne budowanie z udokumentowanych wejść. | Druga kompilacja w czystym katalogu z przypiętych źródeł i tą samą konfiguracją daje zgodny manifest pakietów oraz obraz, który się uruchamia. Porównanie bitowe obrazu SD to osobny test. |

## Zakres

MVP obejmuje jeden obraz AArch64, jedną usługę w C budowaną przez CMake,
lokalną konfigurację i diagnostykę, restart usługi z limitem, sprzętowy
watchdog, testy jednostkowe na hoście i testy akceptacyjne na Raspberry Pi.

Opcjonalne rozszerzenia obejmują trwałą historię zdarzeń, zdalną telemetrię,
aktualizacje A/B z wycofaniem, dodatkowe metryki i profil 32-bitowy.

MVP korzysta z konsoli lokalnej. Opcjonalny profil deweloperski umożliwia
SSH. Test na tym profilu potwierdza logowanie autoryzowanym kluczem
publicznym oraz wyłączenie uwierzytelniania hasłem.
