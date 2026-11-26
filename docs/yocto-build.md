# Budowanie obrazu Yocto od świeżego klonu

Instrukcja dotyczy Raspberry Pi 3 Model B z `MACHINE=raspberrypi3-64`.
Budujemy `rpi-sentinel-image` na bazie Yocto Project 6.0.4 Wrynose,
BitBake 2.18.0, `DISTRO=poky` i `INIT_MANAGER=systemd`.
Wejścia procesu są przypięte. Zgodność dwóch obrazów bajt w bajt
wymaga osobnego porównania wykonanego w kontrolowanych warunkach.

## Host i wersje

Użyj natywnego Linuksa, np. Ubuntu 24.04 lub 26.04. Umieść katalog
roboczy Yocto na linuksowym systemie plików z zapasem miejsca i sprawdź
`df -h "$HOME"`. Pełna lista zależności zależy od wersji dystrybucji.
[Wymagania Yocto 6.0.4](https://docs.yoctoproject.org/6.0.4/ref-manual/system-requirements.html)

```bash
sudo apt update
sudo apt install build-essential chrpath cpio debianutils diffstat file gawk git \
    iputils-ping libacl1 libcrypt-dev locales python3 python3-git python3-jinja2 \
    python3-pexpect python3-pip python3-subunit socat texinfo unzip wget xz-utils zstd \
    bzip2 openssh-client udisks2
sudo locale-gen en_US.UTF-8
```

Pierwsza grupa pakietów przygotowuje hosta Yocto. `bzip2`,
`openssh-client` i `udisks2` służą do obsługi obrazu i karty.
Na hoście o innej ilości RAM dostosuj `BB_NUMBER_THREADS` i
`PARALLEL_MAKE` z `build-config/stage2.conf`.

Jedynym źródłem przypięć używanym przez poniższe polecenia jest
[`build-config/sources.lock`](../build-config/sources.lock).
Rewizję własnej warstwy określa commit bieżącego checkoutu.

| Repozytorium | Wersja lub seria | Rewizja Git |
| --- | --- | --- |
| BitBake | 2.18.0 | `51fd94a67fa17650a9ff623492005972bbc7d26f` |
| OpenEmbedded-Core | Wrynose 6.0.4, warstwa `meta` | `0f03c732a0a2d0506b41b1abe60b3f368b2300dc` |
| meta-yocto | Wrynose 6.0.4, warstwa `meta-poky` | `7c25b11156553145fa6b241a2d4f65d94438f804` |
| meta-raspberrypi | `wrynose`, Raspberry Pi BSP | `919588393f41b48cb6c5d832aa35e58a19b6762d` |

## Konfiguracja i build

Sklonuj repozytorium i ustaw checkout na wybrany commit lub tag:

```bash
read -r -p 'URL repozytorium projektu: ' repository_url
read -r -p 'Commit lub tag projektu: ' revision
git clone "$repository_url" rpi-sentinel &&
cd rpi-sentinel &&
git checkout --detach "$revision"
```

W katalogu głównym projektu uruchom cały blok Bash.
Domyślny profil `wifi-ssh`
umożliwia test `live`. Profil `base` daje obraz konsolowy.
Profil `recovery` dodatkowo instaluje uprawnienia dla prób awarii.

```bash
(
    set -eo pipefail
    project_dir="$PWD"
    yocto_dir="${YOCTO_DIR:-$HOME/yocto/rpi-sentinel-release}"
    profile="${SENTINEL_PROFILE:-wifi-ssh}"
    case "$profile" in
        base|wifi-ssh|recovery) ;;
        *) printf 'Nieznany profil: %s\n' "$profile" >&2; exit 2 ;;
    esac
    build_dir="$yocto_dir/build-release-$profile"
    artifact_dir="$project_dir/artifacts/release/$profile"
    mkdir -p "$yocto_dir/layers" "$artifact_dir"

    while read -r name url revision; do
        repo="$yocto_dir/layers/$name"
        if [ ! -d "$repo" ]; then
            git clone "$url" "$repo"
            git -C "$repo" checkout --detach "$revision"
        fi
        test "$(git -C "$repo" rev-parse HEAD)" = "$revision" ||
            { printf 'Nieprawidłowa rewizja: %s\n' "$repo" >&2; exit 1; }
        test -z "$(git -C "$repo" status --porcelain)" ||
            { printf 'Lokalne zmiany w warstwie: %s\n' "$repo" >&2; exit 1; }
        printf '%s %s\n' "$name" "$revision"
    done < <(awk -F ' *[|] *' '$1 !~ /^#/ && NF == 4 {print $1, $2, $3}' \
        "$project_dir/build-config/sources.lock")

    source "$yocto_dir/layers/openembedded-core/oe-init-build-env" "$build_dir"
    install -m 0644 "$project_dir/build-config/stage2.conf" conf/stage2.conf
    printf '%s\n' 'require conf/stage2.conf' > conf/sentinel-release.conf
    if [ "$profile" != base ]; then
        install -m 0644 "$project_dir/build-config/stage2-wifi-ssh.conf" conf/
        install -D -m 0644 "$project_dir/build-config/classes/stage2-wifi-ssh.bbclass" \
            classes/stage2-wifi-ssh.bbclass
        printf '%s\n' 'require conf/stage2-wifi-ssh.conf' >> conf/sentinel-release.conf
    fi
    if [ "$profile" = recovery ]; then
        printf '%s\n' 'IMAGE_INSTALL:append:pn-rpi-sentinel-image = " rpi-sentinel-test-access"' \
            >> conf/sentinel-release.conf
    fi
    cat >> conf/sentinel-release.conf <<'CONFIG'
DL_DIR = "${TOPDIR}/../downloads"
SSTATE_DIR = "${TOPDIR}/../sstate-cache"
BB_HASHSERVE_DB_DIR = "${TOPDIR}/../cache"
CONFIG
    if ! grep -Fxq 'require conf/sentinel-release.conf' conf/local.conf; then
        printf '\nrequire conf/sentinel-release.conf\n' >> conf/local.conf
    fi
    for layer in "$yocto_dir/layers/meta-yocto/meta-poky" \
            "$yocto_dir/layers/meta-raspberrypi" "$project_dir/meta-rpi-sentinel"; do
        if ! grep -Fq -- "$layer" conf/bblayers.conf; then
            bitbake-layers add-layer "$layer"
        fi
    done

    bitbake-layers show-layers | tee "$artifact_dir/layers.log"
    bitbake -p 2>&1 | tee "$artifact_dir/parse.log"
    if [ "${SENTINEL_PARSE_ONLY:-0}" = 1 ]; then
        exit 0
    fi
    bitbake rpi-sentinel-image 2>&1 | tee "$artifact_dir/build.log"
    deploy_dir="$(bitbake-getvar -q --value DEPLOY_DIR_IMAGE)"
    deploy_prefix="$deploy_dir/rpi-sentinel-image-raspberrypi3-64.rootfs"
    image="$(readlink -e "$deploy_prefix.wic.bz2")"
    manifest="$(readlink -e "$deploy_prefix.manifest")"
    cp -- "$image" "$manifest" "$artifact_dir/"
    printf 'Obraz: %s/%s\n' "$artifact_dir" "${image##*/}"
    cd "$artifact_dir"
    sha256sum -- "${image##*/}" | tee "${image##*/}.sha256"
)
```

Katalog `artifacts/release/<profil>/` zawiera plik `.wic.bz2`, manifest,
sumę i logi. `SENTINEL_PARSE_ONLY=1` kończy próbę po sprawdzeniu
metadanych, przed kompilacją. Aby zmienić profil w następnym przebiegu,
ustaw w bieżącej powłoce np. `export SENTINEL_PROFILE=recovery`.
Użyj tego profilu wyłącznie na urządzeniu do prób awarii, ponieważ
przyznaje kontu testowemu ograniczone uprawnienia administracyjne.

Sprawdzono powyższy blok w świeżych katalogach builda z istniejącymi,
zweryfikowanymi checkoutami upstream. Wszystkie trzy profile przeszły
parsowanie. Efektywne metadane potwierdziły OpenSSH w profilach
`wifi-ssh` i `recovery` oraz `rpi-sentinel-test-access` wyłącznie
w profilu `recovery`. Ta próba zakończyła się przed kompilacją obrazu.

## microSD i pierwsze połączenie

Wykonaj kopię danych z karty. Polecenie zapisu nadpisuje całą kartę.
Sprawdź ścieżki za pomocą `lsblk` i wpisz ścieżkę **całego urządzenia**,
np. `/dev/sdX`. Skrypt weryfikuje sumę i prosi o potwierdzenie.
Obecna wersja skryptu rozpoznaje sprawdzony czytnik USB identyfikujący
się jako `Storage Device`. Przy innym czytniku użyj narzędzia do zapisu
obrazów, które pokazuje urządzenie docelowe. Jeśli potrzebuje obrazu
nieskompresowanego, najpierw sprawdź sumę i rozpakuj plik przez `bzip2 -dc`.

```bash
lsblk -o PATH,SIZE,MODEL,TRAN,TYPE,FSTYPE,LABEL,MOUNTPOINTS
read -r -p 'Urządzenie całej karty: ' card
read -r -p 'Ścieżka obrazu .wic.bz2: ' image
read -r -p 'Suma SHA256 obrazu: ' image_sha
bash scripts/remove-deploy-card.sh "$card" "$image" "$image_sha"
```

Dla profilu `wifi-ssh` lub `recovery` przygotuj istniejącą parę
kluczy SSH i wskaż odmontowaną partycję karty oznaczoną `root`.
Skrypt poprosi o SSID i hasło przez terminal. Może także użyć
lokalnego pliku `.local/wifi-default` z trybem `0600`. Na kartę
trafia klucz **publiczny** i konfiguracja WPA2. Zachowaj prywatny
klucz i dane Wi-Fi w swoim środowisku. Obrazy do ewentualnej dystrybucji
pochodzą z katalogu artefaktów przed konfiguracją karty. Obecny profil
zapisuje kod kraju `PL`.

```bash
lsblk -o PATH,FSTYPE,LABEL,MOUNTPOINTS
read -r -p 'Odmontowana partycja root karty: ' root_partition
bash scripts/provision-wifi-card.sh "$root_partition" "$HOME/.ssh/id_ed25519.pub"
```

Włóż kartę do Pi i uruchom urządzenie. Adres odczytaj z listy dzierżaw
DHCP lub ustal według [instrukcji diagnostycznej](troubleshooting.md).
Zweryfikuj klucz hosta przy pierwszym połączeniu.

```bash
read -r -p 'Adres Raspberry Pi: ' pi
ssh -i "$HOME/.ssh/id_ed25519" "developer@$pi" 'uname -m && cat /proc/1/comm'
bash scripts/smoke-test.sh live "$pi"
```

Profil `base` można sprawdzić na lokalnej konsoli. Oczekiwany wynik
profilu SSH to `aarch64`, `systemd) i linie PASS z pomiarami.
