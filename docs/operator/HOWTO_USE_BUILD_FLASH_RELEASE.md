# HOWTO: użycie, walidacja, kompilacja, release archive i flashowanie ESP8266

Dokument opisuje zalecany operatorski workflow dla repozytorium `esp8266-event-driven-framework`. Jest napisany dla prywatnego repo, w którym właściciel świadomie trzyma realne WiFi SSID/hasła/tokeny w allowlistowanych plikach private-lab, np. `bsp/<board>/board_secrets.local.h`.

Najważniejsza zasada: **release archive twórz przed SDK buildem albo po pełnym `git clean -fdx`**. SDK build generuje artefakty w katalogach `build/` i `adapters/.../build/`, których nie wolno mieszać z release-prearchive gate.

---

## 1. Szybki obraz poprawnego workflow

```text
1. Czyste repo
2. Evidence/privacy gates
3. Host tests + sanitizer + release-prearchive
4. ./tools/fw release-archive
5. Archive self-clean verification + SHA256
6. Dopiero potem SDK build / flash / monitor
```

Nie używaj ręcznego `git archive` jako normalnej ścieżki release. Nie używaj plain `./tools/fw sdk-build` jako głównego dowodu release/evidence. Do Wemos używaj markerowanego `sdk-build-one`.

---

## 2. Wymagania wstępne

Na komputerze developerskim potrzebujesz:

- Git,
- Docker,
- kompilator C hosta (`cc`, zwykle GCC lub Clang),
- Python 3,
- dostęp do portu USB/UART, np. `/dev/ttyUSB0`,
- dla flashowania ESP8266: uprawnienia do portu szeregowego.

Sprawdzenie podstaw:

```bash
git --version
docker --version
python3 --version
cc --version
```

Jeżeli port `/dev/ttyUSB0` wymaga uprawnień, zwykle pomaga dodanie użytkownika do grupy `dialout` i ponowne zalogowanie:

```bash
sudo usermod -aG dialout "$USER"
```

---

## 3. Ważna uwaga dla ASAN na Linuksie

Na niektórych dystrybucjach `vm.mmap_rnd_bits=32` powoduje zapętlenie lub spam:

```text
AddressSanitizer:DEADLYSIGNAL
```

W tym środowisku projekt przeszedł ASAN/UBSAN po ustawieniu:

```bash
sudo sysctl -w vm.mmap_rnd_bits=28
```

Żeby utrwalić to po restarcie:

```bash
echo 'vm.mmap_rnd_bits=28' | sudo tee /etc/sysctl.d/99-asan-mmap-rnd.conf
sudo sysctl --system
sudo cat /proc/sys/vm/mmap_rnd_bits
```

Oczekiwany wynik:

```text
28
```

Do codziennych sanitizerów używaj prostych opcji:

```bash
export ASAN_OPTIONS="abort_on_error=1:halt_on_error=1:detect_leaks=0:symbolize=1"
export UBSAN_OPTIONS="halt_on_error=1"
```

Nie używaj `print_stacktrace` w `ASAN_OPTIONS`, jeżeli Twoja wersja ASAN wypisuje `unrecognized flag`.

---

## 4. Czyste repo przed release

Przed release usuń wszystkie artefakty builda i archive z drzewa roboczego:

```bash
git clean -fdx
git status --short
```

Oczekiwany wynik `git status --short`: pusty output.

`git clean -fdx` usuwa nieśledzone pliki, m.in.:

```text
build/
adapters/esp8266_rtos_sdk/targets/*/build/
adapters/esp8266_rtos_sdk/targets/*/sdkconfig
*.tar.gz
*.sha256
__pycache__/
```

Nie usuwa śledzonych plików Git.

---

## 5. Tryb prywatnego repo i evidence gates

W prywatnym repo ustaw tryb:

```bash
export EV_REPO_MODE=PRIVATE_REPO
```

Sprawdź evidence redaction i politykę sekretów:

```bash
make evidence-redaction-check
make private-repo-secrets-policy
```

Oczekiwane wyniki:

```text
EV_EXISTING_EVIDENCE_REDACTION DRY_RUN files_changed=0 hash_repairs=0
private repo secrets policy passed mode=PRIVATE_REPO
```

Sekrety mogą pozostać w allowlistowanych private-lab źródłach. Nie mogą występować w plikach `redacted`, `normalized`, raportach, manifestach, patchach, ZIP-ach, markdownach ani outputach CI.

Jeżeli `evidence-redaction-check` zgłosi pliki do naprawy:

```bash
make repair-evidence-redaction
make evidence-redaction-check
make private-repo-secrets-policy
```

Potem sprawdź tylko nazwy plików, bez zwykłego `git diff`:

```bash
git status --short
git diff --name-only
```

Nie wypisuj zwykłego `git diff` dla historycznych evidence logs, bo diff może pokazać usuwane linie z prywatną wartością.

---

## 6. Host validation

Minimalna walidacja hostowa:

```bash
make host-test
make property-test
make host-strict-test
```

Pełna walidacja sanitizerowa:

```bash
make clean
export ASAN_OPTIONS="abort_on_error=1:halt_on_error=1:detect_leaks=0:symbolize=1"
export UBSAN_OPTIONS="halt_on_error=1"
make -j1 host-sanitize-test
unset ASAN_OPTIONS
unset UBSAN_OPTIONS
```

Oczekiwany wynik:

```text
host-sanitize-test passed (deterministic ASAN/UBSAN shards)
```

Używaj `-j1` dla sanitizerów i release-prearchive, gdy debugujesz problemy. Mniej równoległości oznacza czytelniejsze logi.

---

## 7. Release-prearchive gate

Przed utworzeniem archive uruchom:

```bash
make -j1 release-prearchive-gate
```

Ten gate powinien sprawdzić m.in.:

- evidence redaction,
- private repo secrets policy,
- release evidence contracts,
- routegen/static contracts,
- route-registry integration,
- patch hygiene,
- host tests,
- property tests,
- host strict,
- host sanitize,
- release archive workflow/content checks,
- clean tree gate.

Jeżeli ten gate failuje, **nie twórz archive**.

---

## 8. Poprawne tworzenie release archive

Nie rób:

```bash
git archive --format=tar.gz -o esp8266-event-driven-framework_$(date +%Y%m%d_%H%M%S).tar.gz HEAD
```

Normalna ścieżka release to:

```bash
./tools/fw release-archive
```

Po utworzeniu archive:

```bash
ARCHIVE="$(ls -t esp8266-event-driven-framework_*.tar.gz | head -n 1)"
echo "$ARCHIVE"
EV_RELEASE_ARCHIVE_PATH="$ARCHIVE" make release-archive-self-clean-gate
sha256sum "$ARCHIVE" > "$ARCHIVE.sha256"
```

Oczekiwany wynik:

```text
EV_RELEASE_ARCHIVE_SELF_CLEAN PASS archive=<archive>.tar.gz mode=PRIVATE_REPO
```

Zachowaj release artefakty poza repo:

```bash
mkdir -p ~/esp8266-release-artifacts
cp "$ARCHIVE" "$ARCHIVE.sha256" ~/esp8266-release-artifacts/
```

Po skopiowaniu możesz wyczyścić repo:

```bash
git clean -fdx
```

Pamiętaj: `git clean -fdx` usunie lokalny archive, jeżeli nadal leży w repo.

---

## 9. Wemos ESP-WROOM-02 18650 — build SDK

Dla Wemos ustaw zawsze jawny target:

```bash
mkdir -p build

export WEMOS_PROJECT=adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650
export FW_SDK_PROJECT_DIR="$WEMOS_PROJECT"
export EV_WEMOS_FLASH_VARIANT=4mb
export FW_MONITOR_BAUD=115200

echo "$FW_SDK_PROJECT_DIR"
```

Oczekiwany target:

```text
adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650
```

Sprawdź sekrety i wyczyść SDK target:

```bash
./tools/fw wifi-secrets-status
./tools/fw sdk-distclean
```

Buduj markerowaną ścieżką:

```bash
./tools/fw sdk-build-one wemos_esp_wroom_02_18650 2>&1 | tee build/sdk-current.log
```

Sprawdź markery:

```bash
grep -E 'EV_SDK_BUILD_(BEGIN|TARGET|STATUS|END)' build/sdk-current.log
grep 'EV_SDK_BUILD_TARGET=wemos_esp_wroom_02_18650' build/sdk-current.log
```

Oczekiwany wzór:

```text
EV_SDK_BUILD_TARGET=wemos_esp_wroom_02_18650
EV_SDK_BUILD_BEGIN
EV_SDK_BUILD_STATUS=PASS
EV_SDK_BUILD_END
EV_SDK_BUILD_TARGET=wemos_esp_wroom_02_18650
```

Następnie uruchom warning policy:

```bash
python3 tools/audit/sdk_warning_policy.py \
  --project-only \
  --latest-build-session \
  --session-kind build \
  --strict-build-session \
  --target wemos_esp_wroom_02_18650 \
  build/sdk-current.log
```

Oczekiwany wynik:

```text
sdk-project-warning-policy passed warnings=0 build_status=PASS session_mode=EV_SDK_BUILD_BLOCK target=wemos_esp_wroom_02_18650
```

Nie używaj plain `./tools/fw sdk-build` jako głównej walidacji release/evidence. `sdk-build-one` daje jednoznaczny blok `EV_SDK_BUILD_BEGIN/END` i target.

---

## 10. Flashowanie Wemos

Przed każdym flashowaniem ustaw target w tej samej sesji terminala:

```bash
export FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650
export FW_ESPPORT=/dev/ttyUSB0
export FW_MONITOR_BAUD=115200

echo "$FW_SDK_PROJECT_DIR"
```

Flash:

```bash
./tools/fw sdk-flash
```

Poprawny log musi zawierać:

```text
make: Entering directory '/work/adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650'
App "ev_wroom_02" version: <commit>
Hash of data verified.
```

Jeżeli zobaczysz:

```text
targets/esp8266_generic_dev
App "ev_esp8266_generic_dev"
```

flashujesz zły target. Wtedy przerwij, ustaw ponownie `FW_SDK_PROJECT_DIR` na Wemos i powtórz build/flash.

---

## 11. Monitor serial

Po flashu:

```bash
./tools/fw sdk-simple-monitor
```

Dla Wemos typowy baud:

```text
115200
```

Kontrolowane zakończenie monitora przez `Ctrl+C` jest normalne. Crash firmware wygląda inaczej i powinien być analizowany osobno.

---

## 12. Pełna zalecana sekwencja release + Wemos build/flash

Skopiuj jako bezpieczny happy path:

```bash
# 1. Czyste repo
rm -f esp8266-event-driven-framework_*.tar.gz esp8266-event-driven-framework_*.tar.gz.sha256
git clean -fdx
git status --short

# 2. Evidence / privacy
export EV_REPO_MODE=PRIVATE_REPO
make evidence-redaction-check
make private-repo-secrets-policy

# 3. Host / sanitizer / prearchive
make clean
export ASAN_OPTIONS="abort_on_error=1:halt_on_error=1:detect_leaks=0:symbolize=1"
export UBSAN_OPTIONS="halt_on_error=1"
make -j1 host-sanitize-test
unset ASAN_OPTIONS
unset UBSAN_OPTIONS

make -j1 release-prearchive-gate

# 4. Release archive
./tools/fw release-archive
ARCHIVE="$(ls -t esp8266-event-driven-framework_*.tar.gz | head -n 1)"
echo "$ARCHIVE"
EV_RELEASE_ARCHIVE_PATH="$ARCHIVE" make release-archive-self-clean-gate
sha256sum "$ARCHIVE" > "$ARCHIVE.sha256"

mkdir -p ~/esp8266-release-artifacts
cp "$ARCHIVE" "$ARCHIVE.sha256" ~/esp8266-release-artifacts/

# 5. SDK Wemos build
mkdir -p build
export WEMOS_PROJECT=adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650
export FW_SDK_PROJECT_DIR="$WEMOS_PROJECT"
export EV_WEMOS_FLASH_VARIANT=4mb
export FW_MONITOR_BAUD=115200

./tools/fw wifi-secrets-status
./tools/fw sdk-distclean
./tools/fw sdk-build-one wemos_esp_wroom_02_18650 2>&1 | tee build/sdk-current.log

python3 tools/audit/sdk_warning_policy.py \
  --project-only \
  --latest-build-session \
  --session-kind build \
  --strict-build-session \
  --target wemos_esp_wroom_02_18650 \
  build/sdk-current.log

# 6. Flash + monitor
export FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650
export FW_ESPPORT=/dev/ttyUSB0
export FW_MONITOR_BAUD=115200

./tools/fw sdk-flash
./tools/fw sdk-simple-monitor
```

---

## 13. Najczęstsze błędy i szybka diagnoza

### Błąd: `patch-hygiene-gate` łapie trailing whitespace w `adapters/.../build/...`

Przyczyna: uruchomiłeś release-prearchive po SDK buildzie. Wygenerowane SDK artefakty są w drzewie.

Naprawa:

```bash
git clean -fdx
make -j1 release-prearchive-gate
```

Release archive zawsze przed SDK buildem albo po czyszczeniu.

### Błąd: flashuje `esp8266_generic_dev`

Przyczyna: nie ustawiono `FW_SDK_PROJECT_DIR` w aktualnej sesji terminala.

Naprawa:

```bash
export FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650
export FW_ESPPORT=/dev/ttyUSB0
./tools/fw sdk-flash
```

### Błąd: `AddressSanitizer:DEADLYSIGNAL`

Sprawdź:

```bash
sudo cat /proc/sys/vm/mmap_rnd_bits
```

Jeżeli wynik to `32`:

```bash
sudo sysctl -w vm.mmap_rnd_bits=28
```

Potem:

```bash
make clean
make -j1 host-sanitize-test
```

### Błąd: `cat /proc/sys/vm/mmap_rnd_bits: Permission denied`

Użyj:

```bash
sudo cat /proc/sys/vm/mmap_rnd_bits
```

### Błąd: `EV_RELEASE_ARCHIVE_SELF_CLEAN FAIL`

Nie publikuj archive. Uruchom:

```bash
make evidence-redaction-check
make private-repo-secrets-policy
make release-prearchive-gate
./tools/fw release-archive
```

Jeżeli evidence check zgłasza zmiany, najpierw wykonaj lokalny repair i commit sanitized evidence.

### Błąd: `sdk-project-warning-policy` nie widzi build block

Użyłeś plain `sdk-build` albo log nie zawiera markerów. Użyj:

```bash
./tools/fw sdk-build-one wemos_esp_wroom_02_18650 2>&1 | tee build/sdk-current.log
```

---

## 14. HIL i testy sprzętowe

Bez fizycznego sprzętu, logów HIL i logic-analyzer capture status ma być:

```text
ENVIRONMENT_BLOCKED
```

Nie wolno traktować parser self-testu jako realnego hardware PASS.

Przykładowe targety:

```bash
make hil-atnel-i2c-gate
make hil-atnel-onewire-gate
make hil-logic-analyzer-readiness-gate
make wemos-runtime-soak-gate
make sdk-memory-stack-regression-gate
```

Jeżeli nie ustawisz wymaganych logów/capture przez zmienne środowiskowe, te gate’y mogą zakończyć się jako `ENVIRONMENT_BLOCKED`. To jest poprawne.

---

## 15. Co dalej po stabilnym release flow

Po potwierdzeniu:

```text
evidence-redaction-check PASS
private-repo-secrets-policy PASS
host-sanitize-test PASS
release-prearchive-gate PASS
release-archive-self-clean-gate PASS
sdk-build-one Wemos PASS
sdk_warning_policy warnings=0
sdk-flash Wemos PASS
```

następne sensowne kroki doskonalenia to:

1. realny I2C logic-analyzer HIL,
2. realny OneWire WiFi-ON HIL,
3. 30+ minut runtime soak,
4. SDK memory/stack regression z realnych artefaktów,
5. dopiero potem BME280.

---

## 16. Minimalny commit/publish flow

Po lokalnym commicie sanitized evidence i patchach:

```bash
git log --oneline -5
git status --short
```

Jeżeli branch jest ahead of origin:

```bash
git push origin dev
```

Nie commituj:

```text
build/
adapters/esp8266_rtos_sdk/targets/*/build/
adapters/esp8266_rtos_sdk/targets/*/sdkconfig
*.tar.gz
*.sha256
__pycache__/
```

Release artefakty trzymaj poza repo, np.:

```text
~/esp8266-release-artifacts/
```
