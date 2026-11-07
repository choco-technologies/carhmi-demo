# Literki

Panel **Gry** jest pod ikoną pada w lewym menu CarHMI. Zawiera jedną grę:
**Literki**. Domyślnie losuje wszystkie cztery rodzaje zadań, bez kar za pomyłki.

1. Obrazek i trzy litery: rozpoznanie pierwszej litery ze słuchu.
2. Litera i trzy obrazki: wskazanie słowa rozpoczynającego się od tej litery.
3. Obrazek i przemieszane literki: układanie słowa po kolei, z przyciskiem Cofnij.
4. Wyraz i trzy obrazki: samodzielne czytanie. Lektorka mówi tylko instrukcję,
   nigdy nie odczytuje wyrazu, który dziecko ma przeczytać.

Jest **27 liter × 3 słowa = 81 obrazków**, m.in. **mama, tata, dom**.
Zgodnie z ustaleniem pomijamy Ą, Ę, Ń oraz mało użyteczne na początku wyrazu
Ó i Y. Wszystkie polskie znaki wewnątrz słów pozostają poprawne, np. `ŁÓDKA`,
`WĄŻ`, `SŁOŃ`, `ŻÓŁW`, `ŹDŹBŁO`. Pojedyncze kafelki to litery, więc np.
`SZ` i `CZ` składają się z dwóch kafelków.

W każdej paczce czterech zadań pojawia się każdy typ, w losowej kolejności.
Słowa są tasowane bez powtórzeń do wykorzystania całego zestawu. Trzy obrazki
w zadaniach wyboru mają różne pierwsze litery, więc odpowiedź jest jednoznaczna.
Powtarzające się litery (np. MAMA, TATA) mają osobne kafelki.

Poprawna odpowiedź daje gwiazdkę, krótki sygnał i brawa. Potem automatycznie
pojawia się kolejne zadanie. Podwójne i spóźnione kliknięcia nie dają dodatkowych
punktów. Przy błędzie można spróbować ponownie bez utraty ułożonych literek.
Wejście do panelu Gry wstrzymuje muzykę i wycisza efekty samochodu; wyjście
przywraca wcześniejsze odtwarzanie i syrenę. Przycisk głośności działa nadal.

Przyciski reagują już przy dotknięciu: w grze zmieniają tło i obramowanie,
przycisk Zagraj ciemnieje, a pozostałe przyciski CarHMI rozjaśniają tło i
przygasają. Reakcję rysuje sam widok, przed obsługą kliknięcia przez aplikację.
Akcja wykonuje się dopiero po puszczeniu palca wewnątrz przycisku; zsunięcie
palca poza przycisk anuluje kliknięcie i przywraca jego wygląd.
Pod wczytywaną ilustracją pojawia się „Ładuję…”, przykrywane po odczytaniu obrazka.

## Nagrania ElevenLabs

Gotowa lista: **[elevenlabs.csv](elevenlabs.csv)**; prosta wersja do kopiowania:
**[teksty-do-nagran.txt](teksty-do-nagran.txt)**. Są **193 pliki**:

- 81 pytań `first_<słowo>.mp3`, np. `first_arbuz.mp3`:
  „Jaką literką zaczyna się słowo: arbuz?”
- 81 poleceń `spell_<słowo>.mp3`, np. `spell_mama.mp3`:
  „Ułóż z literek słowo: mama.”
- 27 poleceń `letter_00.mp3` … `letter_26.mp3`:
  „Wybierz obrazek na literę A.” itd., w kolejności liter w `words.json`.
- `welcome.mp3`, `read.mp3`, `again.mp3`, `bravo.mp3`.

Nagraj wszystkie teksty tą samą polską kobiecą barwą, ciepło i wyraźnie,
z niewielką pauzą przed nazwą przedmiotu. Nie dodawaj w pytaniu nazwy poprawnej
litery. Litery wypowiadaj po polsku; w `read.mp3` nie dodawaj konkretnego wyrazu.
Zapisz MP3 ze standardowym próbkowaniem (np. 44100 Hz), najlepiej mono, bez
muzyki w tle i długiej ciszy. Nazwy plików z CSV są bez polskich znaków.

Umieść nagrania w `assets/games/literki/audio/` i uruchom zwykły build:

```sh
cmake -S . -B build
cmake --build build --parallel 8
```

Nagrania trafią do obu pakietów ekranowych pod `literki/audio/`. Można też
wgrać same MP3 na płytkę do katalogu
`/mnt/<karta>/views/carhmi_view/literki/audio/` przez `ymodem_send.py` z dmod-boot.
Nie umieszczaj ich w `music/`: to instrukcje gry, a nie utwory odtwarzacza.

**Stan bez dostarczonych MP3:** gra i generowane brawa działają na płytce;
lektor na STM32 wymaga nagrań. Podgląd w przeglądarce korzysta z plików MP3,
a gdy ich brak — z dostępnego systemowego polskiego głosu. ElevenLabs pozwoli
uzyskać spójną barwę na urządzeniu niezależnie od komputera.

## Dane i ilustracje

Źródło słownictwa: `assets/games/literki/words.json`.
`scripts/generate_literki.py` generuje z niego tabelę C, dane podglądu i listę
nagrań. Zmiana słów nie wymaga ręcznego synchronizowania tych plików.
Ilustracje znajdują się w `assets/games/literki/images/`; oryginalne atlasy
w `atlases/`. Wygenerowano je narzędziem wbudowanym `image_gen` i wyodrębniono
pola atlasów. Zestaw promptów jest w `art-prompts.json`.

Widok nadal powstaje z `assets/car_hmi.html`. Konwerter lokalny `dmvs_html`
wymaga zwiększonego limitu klikalnych elementów (256 zamiast 64); klawiatura gry
przekraczała stary limit. Lokalna poprawka jest w
`/data/codex/dmvs_html/src/script.c`. Zaktualizowano narzędzie w `build/todmvs/`.
Przy przygotowaniu świeżego środowiska trzeba użyć tej wersji konwertera
(`CARHMI_TOOLS_DIR`) lub nowszej z taką pojemnością. Efekty naciśnięcia
wymagają też lokalnej poprawki łączenia `:active` z wyglądem zmienianym przez
zestawy klas; pojemność stanów strony wynosi 512. Zmiana narzędzi konwertera
unieważnia teraz cache konwersji strony.

## Testy

```sh
cmake -S tests/literki -B build/letters-host
cmake --build build/letters-host
ctest --test-dir build/letters-host --output-on-failure
```

`test_literki_input` sprawdza rzeczywisty renderer, dotyk, kanał widoku i kontroler
C. Zastępuje tylko fizyczne końcówki audio, by móc sprawdzić pytania, nagrody,
przerywanie dźwięku i przywracanie muzyki bez odtwarzania na komputerze.
Porównuje też piksele przycisków przed dotknięciem i podczas przytrzymania,
sprawdza anulowanie dotyku oraz podpis ładowania obrazu.
Podgląd i testy obejmują 480×272 oraz 320×240.


Weryfikacja tej zmiany (2026-11-07):

- 9720 rund testu logiki, w tym polskie znaki i wielokrotne litery.
- Chromium: rzeczywiste kliknięcia wszystkich czterech trybów, cofanie,
  powtórzenie instrukcji, pomyłki i opuszczenie gry; oba rozmiary ekranu.
- Natywny renderer i kontroler: oba rozmiary ekranu, włącznie ze sprawdzaniem,
  że narysowane kafelki odpowiadają aktualnej rundzie.
- Regresja nawigacji i tankowania w obu rozmiarach ekranu.
- STM32F746G-DISCO: `PASS Literki real input 480x272: 0 failure(s)`.
- Prawdziwy mikser/głośnik STM32: `PASS reward speaker: 39690 PCM frames, finished=1`.

Podglądy z natywnego renderera są w `docs/games/previews/`; logi lokalne w
`build/games-validation/`. Poprzedni widok i moduły na karcie zachowano w
`/mnt/DMODOS/carhmi-games-backup-20261107/`. Aplikację i widok wgrano na kartę,
bez przeprogramowywania firmware płytki.

Test przeglądarkowy (Playwright zainstalowany w środowisku):

```sh
python3 -m http.server 8765 --directory assets
NODE_PATH=/path/to/node_modules node tests/literki/browser.cjs
```

`test_literki_sound` jest dodatkowym testem sprzętowym uruchamianym ręcznie;
celowo odtwarza brawa, więc nie jest częścią automatycznego ctest.

## Naprawa pustych obrazków na STM32 (2026-11-07)

Pliki ilustracji były na karcie, ale wspólny loader obrazków w `libdmview`
był wątkiem procesu, który jako pierwszy zmienił źródło obrazka. Parametr
`NULL` w `dmosi_thread_create` oznacza bieżący proces. Zamknięcie tego procesu
usuwało loader, pozostawiając w bibliotece niepusty, nieaktualny uchwyt.
Kolejne źródła obrazków pozostawały w stanie oczekiwania; działały tylko obrazy
odczytane wcześniej synchronicznie.

Poprawka w `/data/codex/dmview/src/image.c` tworzy dla wspólnego loadera
osobny proces bez rodzica i usuwa go przy zamykaniu biblioteki. Poprawiony
`libdmview.dmf` zainstalowano zarówno w `/ext/bin`, jak i w
`/mnt/DMODOS/bin`, po czym zrestartowano płytkę. Poprzednie moduły są w
`/ext/image-loader-backup-20261107/` oraz
`/mnt/DMODOS/image-loader-backup-20261107/`.

Dodany `test_literki_images` weryfikuje dokładne bajty każdego pliku na karcie
(FNV-1a i rozmiar) oraz faktycznie rysuje obraz na buforze RGB565. Puste pole
nie przechodzi testu. Zestaw kontrolny zawiera wszystkie **81 × 4 = 324**
warianty obrazu dla ekranu 480×272. Sonda najpierw rysuje puste źródło, więc
wszystkie obrazy korzystają z loadera w tle. Przed poprawką test odtworzył
usterkę na pierwszym obrazie. Po poprawce przeszedł dla całego zestawu,
także po zatrzymaniu i ponownym uruchomieniu CarHMI. Najdłuższy zmierzony
odczyt podczas kontroli po restarcie wyniósł 1710 ms; limit oczekiwania sondy
wynosi 10 s, aby uwzględnić równoległy dostęp aplikacji do karty.

`test_literki_input` sprawdza teraz także piksele wewnątrz każdego pola
ilustracji we wszystkich czterech typach zadania. Sam poprawny parametr
`game.object` nie wystarcza do zaliczenia testu. Kontrola wszystkich czterech
trybów przeszła na STM32 dwa razy w osobnych procesach; loader pozostał
aktywny po ich zakończeniu. Oba rozmiary ekranu przeszły też test na hoście.

Powtórzenie audytu (narzędzia `todmv` przygotowane przez zwykły build):

```sh
cmake --build build --target test_literki_images test_literki_input --parallel 8
DMOD_DMF_DIR="$PWD/build/todmv" dmod_loader build/todmv/todmv.dmf --args \
  -o build/tests/image_probe.dmv tests/literki/image_probe.dmvs
python3 /data/codex/dmod-boot/scripts/ymodem_send.py --dest /tmp \
  build/dmf/test_literki_images.dmf build/tests/image_probe.dmv
```

Następnie w konsoli płytki, przed i po ponownym uruchomieniu CarHMI:

```sh
/tmp/test_literki_images.dmf /tmp/image_probe.dmv /mnt/DMODOS/views/carhmi_view
```

Oczekiwany wynik: `PASS image audit: 324/324 rendered, 0 failures`.
Logi odtworzenia i naprawy zapisano w `build/games-validation/image-audit-*.log`.
