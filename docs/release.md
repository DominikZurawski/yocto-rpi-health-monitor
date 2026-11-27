# Gotowość do publikacji

## Zatwierdzony zakres wydania

Proponowany tag to **`v1.0`** dla pierwszego wydania repozytorium projektu.
Wersje receptury monitora i biblioteki mają własny cykl życia. Zakres
obejmuje monitor, bibliotekę metryk, przypięty obraz Yocto dla Pi 3,
kontrolę CI, odzyskiwanie działania pojedynczej usługi i diagnostykę
Kconfig przez `/proc/config.gz`. Kandydatem do weryfikacji jest obraz
profilu `wifi-ssh`. Moduł jądra pozostaje w repozytorium jako opcjonalne
demo w profilu `module-demo`.

## Przegląd techniczny

| Priorytet | Ustalenie | Znaczenie dla publikacji |
| --- | --- | --- |
| Zweryfikowane | Fragment Kconfig potwierdzono na hoście i na Raspberry Pi. `/proc/config.gz` pokazał obie oczekiwane wartości, a test `live` przeszedł. | Powtórzyć tę próbę oraz test `live` na obrazie kandydującym. |
| Średni | Odczyt wejścia zastąpionego FIFO może blokować pętlę. SIGTERM jest odblokowywany przy oczekiwaniu na kolejny cykl. | Zwykłe wejścia to sysfs i procfs. Pod systemd przypadek zatrzymania pętli obejmuje watchdog. Uruchomienie z niestandardowym wejściem wymaga zaufanych plików. |
| Niski | Przy kilku strefach termicznych o pasującym typie biblioteka wybiera pierwszą napotkaną. | Przy przenoszeniu na inną płytkę sprawdź mapowanie stref lub wskaż typ przez `--thermal-type`. |
| Niski | W ręcznym uruchomieniu z wyjściem do zamkniętego potoku proces może zakończyć się przez `SIGPIPE` przed komunikatem o błędzie zapisu. | Dotyczy użycia z potokiem. Dziennik usługi jest obsługiwany przez systemd. |
| Niski | Zapis na kartę zawiera warunek dla konkretnego modelu czytnika USB. | Instrukcja opisuje warunek oraz alternatywną ścieżkę zapisu dla innych czytników. |

## Checklista wydania

- [x] Angielski README opisuje działające funkcje i odsyła do dokumentacji.
- [x] Rewizje upstream, profile obrazu, procedury instalacji i diagnostyki
  są udokumentowane.
- [x] [Tabela wyników](testing.md#podsumowanie-weryfikacji) wskazuje
  środowisko i zakres każdej próby.
- [x] Lokalne artefakty i profil Wi-Fi są ignorowane. Audyt historii Git
  obejmuje rozpoznawane formaty kluczy, tokenów i konfiguracji Wi-Fi.
- [x] Zatwierdzono zakres `v1.0`.
- [x] Zbudowano i sprawdzono sumę SHA256 obrazu kandydującego `wifi-ssh`.
- [x] Potwierdzono test sprzętowy obrazu kandydującego `wifi-ssh`.
- [ ] Uzgodnić zawartość commita publikacyjnego i potwierdzić CI dla
  dokładnego commita przeznaczonego do tagowania.
- [ ] Zatwierdzić datę wydania, tag `v1.0` i ewentualne załączniki.

Tag proponuje się na konkretny, zweryfikowany commit. Jeśli potrzebny
jest obraz do pobrania, opublikuj obraz sprzed konfiguracji karty i jego
sumę SHA256 jako załączniki GitHub Release. Sprawdź przy tym informacje
licencyjne dołączonych pakietów. [GitHub Releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)

## Raport gotowości

Build kandydata `wifi-ssh` zakończył się poprawnie (5132 zadania).
Dwa ostrzeżenia `do_fetch` dotyczyły pobierania certyfikatów CA i
metadanych konfiguracji jądra z podstawowych adresów. BitBake podjął
próby pobrania z adresów zapasowych i ukończył wszystkie zadania.
Manifest potwierdził obecność monitora, biblioteki metryk i OpenSSH.
Obraz `rpi-sentinel-image-raspberrypi3-64.rootfs-20261127093019.wic.bz2`
przeszedł kontrolę SHA256. Na Raspberry Pi 3 `/proc/config.gz` zwrócił
`CONFIG_IKCONFIG=y` i `CONFIG_IKCONFIG_PROC=y`. Test `live` potwierdził
aktywną usługę, próbki w dzienniku i aktualizację pliku stanu.

Repozytorium jest przygotowane do przeglądu publikacyjnego. Kompilacja
z ostrzeżeniami traktowanymi jako błędy i 4/4 testy CTest przeszły.
Wszystkie trzy profile poprawnie sparsowały się w świeżych katalogach
BitBake z istniejącymi checkoutami z `sources.lock`. Pełny build
w tym świeżym środowisku pozostaje osobną próbą.

Kontrola 86 osiągalnych wersji plików w historii Git oraz 52 plików
kandydujących do publikacji nie wykryła typowych formatów kluczy
prywatnych, tokenów GitHub i AWS ani zapisanych danych WPA2. Nie wykryto
też obrazu, pakietu lub pliku o rozmiarze co najmniej 1 MiB w skanowanym
zakresie. Audyt wzorców ogranicza się do rozpoznawanych formatów.

Wydanie `v1.0` czeka na weryfikację commita i zgodę na publikację.
Wszystkie 11 eksperymentów
BitBake opisano w [notatkach](yocto-experiments.md). Omówienie kodu opcji
Kconfig znajduje się na [liście prac odłożonych](build-guide.md#prace-odłożone).
