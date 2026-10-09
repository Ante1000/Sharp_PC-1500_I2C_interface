# Pomoc: program testowy `pc1500_i2c_arduino_test-v1.0` (PC-1500 ↔ Arduino UNO po I2C)

Program w BASIC-u `Basic/pc1500_i2c_arduino_test-v1.0.txt` sprawdza połączenie I2C między
Sharpem PC-1500(A) a zestawem **Arduino UNO + LCD Keypad Shield** ze szkicem
`arduino/PC1500_I2C_Slave/PC1500_I2C_Slave.ino`.

**Masterem jest PC-1500**, bo procedura I2C v1.0 umie być tylko masterem. **Arduino jest
urządzeniem podrzędnym (slave) pod adresem &42.** PC-1500 wysyła polecenia: napis na LCD,
odczyt przetwornika ADC i przycisków shielda, test echa, pomiar prędkości. Arduino je wykonuje
i odsyła odpowiedzi.

English description: [docs/arduino_tester.md](docs/arduino_tester.md).

## Stan testów

| Element | Stan |
|---|---|
| szkic `PC1500_I2C_Slave` | kompiluje się bez ostrzeżeń: 8,9 KB flash, 722 B RAM |
| szkic `LCD_Keypad_Test` (test samego shielda) | sprawdzony na sprzęcie, działa |
| pomiar prędkości (menu 6) na prawdziwym PC-1500 | **działa: SCL 9,397 kHz, 1011 B/s** (symulator przewidywał 9,871 kHz i 1062 B/s, czyli prawdziwy PC-1500 jest o około 5% wolniejszy) |

## Potrzebne części

- Arduino UNO (5 V) z klonem shielda „LCD Keypad Shield” (DFRobot): wyświetlacz 16×2 i 6 przycisków
- dioda Schottky'ego: BAT43, BAT85 albo 1N5817. Nie 1N4148, bo daje za duży spadek napięcia
- rezystor 470 Ω
- opcjonalnie 2 rezystory 4,7 kΩ
- wtyk do 60-pinowego złącza PC-1500: listwa kołkowa 2 × 30 w rastrze 1,27 mm

## Podłączenie

| PC-1500 (złącze 60-pin) | Element pośredni | Shield / UNO |
|---|---|---|
| PC7, pin 10 | dioda Schottky'ego, **katodą (paskiem) do PC7** | **A5** (SCL) |
| PB2, pin 27 | rezystor 470 Ω | **A4** (SDA) |
| GND, piny 52–55 | – | GND |

```
PC-1500 (60-pin)                          Arduino UNO / shield
                     BAT43 / BAT85
PC7  pin 10  ──────────|<|──────────────  A5  (SCL)
             (pasek/katoda do PC7)
PB2  pin 27  ─────────[ 470 ]───────────  A4  (SDA)

GND  pin 55  ───────────────────────────  GND

opcjonalnie:  A5 ──[ 4,7k ]── 5V      A4 ──[ 4,7k ]── 5V
```

- **Rezystory podciągające.** `Wire.begin()` w szkicu sam włącza wewnętrzne rezystory
  podciągające ATmegi na A4 i A5 (20–50 kΩ). Przy krótkich przewodach (do 20–30 cm) i naszym
  zegarze 9–10 kHz to wystarcza, więc na początek wystarczą 3 przewody. Zewnętrzne 4,7 kΩ z A4
  do 5 V i z A5 do 5 V dołóż, gdy test ECHO pokaże błędy, przy dłuższych przewodach albo gdy na
  magistrali są inne moduły. Większość modułów, np. DS3231, ma już rezystory 4,7 kΩ na płytce.
- **Skąd wziąć 5 V i GND.** A4 i A5 są na górnym złączu shielda. Jeśli przy złączach A1–A5 są
  rzędy V/G (5 V i masa), weź je stamtąd. Jeśli nie, użyj złącza ICSP na UNO: pin 2 to 5 V,
  pin 6 to GND.
- **Dioda musi zostać, mimo że obie strony pracują na 5 V.** UNO po każdym bajcie przytrzymuje
  SCL w stanie niskim, dopóki nie obsłuży przerwania. Bez diody PC7 wymuszałby w tym czasie stan
  wysoki i dwa wyjścia walczyłyby ze sobą.
  - Z diodą PC7 może linię tylko ściągnąć w dół, jak prawdziwe wyjście I2C z otwartym drenem.
    Stan wysoki daje rezystor podciągający.
  - Prąd płynie więc tylko od rezystora podciągającego do PC7 (około 1 mA), nigdy z PC7 do
    Arduino. Tak ma być.
- **Na SDA nie ma diody,** bo PB2 musi też odczytywać tę linię (bity ACK i dane z Arduino).
  Rezystor 470 Ω ogranicza tylko prąd, gdyby obie strony kiedyś nadawały jednocześnie.
- **PC-1500 musi być wyjęty z CE-150,** bo CE-150 steruje linią PB2.
- **D0 i D1 zostaw wolne,** bo to USB i monitor portu szeregowego.
- **Wolne piny Arduino:** D2 i D3 można sterować poleceniami z PC-1500, a A1–A3 mierzyć
  przetwornikiem ADC. A0 to drabinka rezystorów przycisków shielda.

## Uruchomienie

1. **Wgraj szkic** `PC1500_I2C_Slave.ino` do UNO (Arduino IDE, płytka „Arduino Uno”). Na LCD
   pojawi się „I2C slave 0x42”. Jeśli ekran jest pusty, ustaw kontrast niebieskim
   potencjometrem na shieldzie.
2. **Zainstaluj I2C v1.0 na PC-1500:**
   - w trybie PRO wpisz `NEW0`, potem `NEW &4500` (PC-1500A; inne wartości są w tabeli w
     [README](README.md#memory));
   - wpisz instalator `Basic/pc1500_i2c_installer-v1.0.txt` i uruchom go `RUN`;
   - na pytanie o port SDA naciśnij ENTER (PB2).
3. **Uruchom skaner instalatora** (`RUN 500`). Powinien znaleźć urządzenie pod **&42**.
4. **W trybie PRO wpisz `NEW`**, żeby usunąć instalator. Kod I2C zostaje w pamięci.
5. **Wpisz program testowy** `Basic/pc1500_i2c_arduino_test-v1.0.txt` i uruchom go `RUN`.
   Program sam sprawdzi:
   - czy kod I2C jest w pamięci (pierwsze bajty &FD &BE). Jeśli nie, pokaże „BRAK I2C V1.0”;
   - czy Arduino odpowiada (test obecności). Jeśli nie, pokaże „I2C BLAD E=1 (1=BRAK)”.

Program testowy zajmuje kilka KB, więc mieści się w PC-1500A. Na PC-1500 z 2 KB RAM potrzebny
jest moduł pamięci (np. CE-155, wtedy `NEW &3D00`).

## Menu na PC-1500

Po starcie program pokazuje menu, a potem pyta `WYBOR 1-8 (9 MENU)?`:

```
0KON 1ID 2TEKST 3ADC 4KLAW
5ECHO 6PREDK 7SWIATLO 8CLS
```

`0` kończy program, `9` pokazuje menu jeszcze raz. Komunikat zatrzymany na ekranie (WAIT)
zamyka się klawiszem ENTER.

| Nr | Test | Pytania | Co robi i co pokazuje |
|---|---|---|---|
| 1 | ID | – | Odczytuje „PC1500 I2C v1.0”. To najprostszy test zapisu i odczytu. |
| 2 | TEKST | `WIERSZ LCD (0/1)?`, `TEKST?` | Wysyła napis (do 16 znaków) do wiersza 0 lub 1 wyświetlacza Arduino i pokazuje `WYSLANO n ZNAKOW`. |
| 3 | ADC | `KANAL A0-A3 (0-3)?` | Ciągły pomiar wybranego kanału: wartość ADC (0–1023) i napięcie w woltach. Kanał 0 to drabinka przycisków, więc wciskając przyciski shielda zobaczysz zmiany. Wyjście: dowolny klawisz PC-1500. |
| 4 | KLAW | – | Przyciski shielda widoczne na PC-1500: `AKT:` aktualnie wciśnięty, `OST:` ostatnio wciśnięty. Wyjście: dowolny klawisz PC-1500. |
| 5 | ECHO | `ILE PROB ECHO?` (ENTER = 20) | N prób z losowymi danymi o długości 1–29 bajtów. Arduino odsyła dane, PC-1500 porównuje je i liczy błędy: `ECHO j/N  BLEDY b`, na końcu podsumowanie. |
| 6 | PREDK | – | Pomiar prędkości: polecenie `F`, potem zapis 31 bajtów (`X` + 30 bajtów). Arduino mierzy czas od pierwszego zbocza SCL do warunku STOP. Wynik na LCD (przez 4 s) i na PC-1500, np. `SCL 9.397 KHZ, 1011 B/S` oraz `32 B Z ADRESEM W 30.648 MS`. Pomiar obejmuje samą transmisję, bez narzutu BASIC-a. |
| 7 | SWIATLO | `SWIATLO 1=WL 0=WYL?` | Włącza i wyłącza podświetlenie LCD. |
| 8 | CLS | – | Czyści LCD Arduino. |

**Na Arduino:**
- Przycisk **SELECT** przełącza widok LCD między tekstem z PC-1500 a statystyką I2C: adres,
  numer ostatniego polecenia i samo polecenie, liczniki zapisów (W), odczytów (R) i błędów (E).
- Monitor portu szeregowego (115 200 bps) wypisuje każde polecenie.

## Komunikaty błędów

| Komunikat | Znaczenie | Co sprawdzić |
|---|---|---|
| `BRAK I2C V1.0` | pod RAM+&400 nie ma kodu I2C | zainstaluj I2C v1.0 (instalator, potem `NEW`) |
| `I2C BLAD E=1 (1=BRAK)` | Arduino nie potwierdziło adresu &42 | przewody, kierunek diody, zasilanie UNO, czy PC-1500 jest wyjęty z CE-150. Przez 1–2 s po resecie UNO (bootloader) brak odpowiedzi jest normalny |
| `I2C BLAD E=2` | Arduino nie przyjęło bajtu danych | zapis dłuższy niż 32 bajty (bufor biblioteki Wire) |
| `I2C BLAD E=3` | SDA cały czas w stanie niskim | zwarcie SDA do masy, UNO bez zasilania, brak podciągania |
| `ARDUINO ZAJETE` | po 50 odczytach odpowiedź nadal niegotowa (status 0) | czy szkic działa (LCD, monitor portu szeregowego) |
| `NIEZNANE POLECENIE` | Arduino nie zna polecenia (status 2) | wersja szkicu |
| `ZLY PARAMETR` | np. kanał ADC większy niż 3 albo wiersz LCD inny niż 0/1 (status 3) | wpisaną wartość |
| `POMIAR NIEUDANY` | Arduino nie zarejestrowało mierzonego zapisu | powtórz test 6 |

## Protokół

Każde polecenie to **osobny zapis** (W = długość, R = 0). Odpowiedź czyta się **osobnym
odczytem** 32 bajtów (W = 0, R = 32).

**Odpowiedź:** `[status][numer polecenia][N][N bajtów danych]`, N ≤ 29. Dane zaczynają się pod
adresem RX+3.

| Status | Znaczenie |
|---|---|
| 0 | Arduino jeszcze przetwarza polecenie. BASIC czyta ponownie, najwyżej 50 razy |
| 1 | OK |
| 2 | nieznane polecenie |
| 3 | zły parametr |

| Polecenie (bajt 0) | Parametry | Dane odpowiedzi |
|---|---|---|
| `P` (80) identyfikator | – | „PC1500 I2C v1.0” |
| `T` (84) tekst na LCD | wiersz 0/1, znaki (do 16) | – |
| `C` (67) czyszczenie LCD | – | – |
| `A` (65) pomiar ADC | kanał 0–3 (A0–A3) | wartość 0–1023 (2 B), napięcie w mV (2 B) |
| `K` (75) przyciski shielda | – | teraz, ostatnio naciśnięty (potem kasowany), licznik; 0 brak, 1 RIGHT, 2 UP, 3 DOWN, 4 LEFT, 5 SELECT |
| `E` (69) echo | do 29 bajtów | te same bajty |
| `F` (70) uzbrojenie pomiaru prędkości | – | – (odpowiedzi nie czytać) |
| `X` (88) dane testowe do pomiaru | dowolne | – |
| `S` (83) wynik pomiaru | – | ważny (1 B), bajty z adresem (1 B), czas w µs (4 B), SCL w Hz (2 B), bajty/s (2 B) |
| `L` (76) podświetlenie | 1 / 0 | – |
| `D` (68) odczyt wejść D2, D3 | – | D2, D3 (0/1) |
| `O` (79) ustawienie D2/D3 | pin 2/3, stan 0/1 (wyjście) albo 2 (wejście z podciąganiem) | – |
| `U` (85) czas od startu Arduino | – | ms (4 B) |

Liczby wielobajtowe: najmłodszy bajt pierwszy. Menu nie używa poleceń `D`, `O` i `U`.

**Własne polecenia z BASIC-a.** Podprogram z linii 920 wysyła L bajtów z bufora TX, czeka na
odpowiedź i zwraca E (wynik I2C), Q (status) i N (liczba bajtów danych). Przykład do dopisania
do programu: ustawienie D2 w stan wysoki, potem odczyt czasu od startu Arduino. Uruchom go
`RUN 1000`:

```
1000 A0 = (PEEK &7863) * 256 : IC = A0 + &400 : IA = A0 + &4FC
1010 TX = A0 + &300 : RX = A0 + &200 : D = &42
1020 POKE TX, 79, 2, 1 : L = 3 : GOSUB 920 : WAIT : PRINT "E="; E; " Q="; Q
1030 POKE TX, 85 : L = 1 : GOSUB 920 : U = PEEK (RX + 3) + 256 * PEEK (RX + 4)
1040 PRINT U + 65536 * PEEK (RX + 5); " MS" : END
```

Linia 1040 bierze tylko 3 młodsze bajty licznika, co wystarcza na 4,6 godziny pracy Arduino.

## Dwie zasady czasowe

Nasz master nie widzi, że Arduino przytrzymuje SCL (PC7 jest tylko wyjściem). Arduino musi więc
zdążyć z obsługą przerwania, zanim PC-1500 podniesie zegar. Czasy poniżej pochodzą z symulatora,
powiększone o 5% zmierzone na prawdziwym PC-1500.

- **Polecenie i odczyt odpowiedzi to zawsze dwa osobne `CALL`.**
  - Przy odczycie master daje Arduino około 72 µs na przygotowanie danych, a obsługa przerwania
    trwa według szacunku około 25–30 µs. To wygodny zapas.
  - Zapis i odczyt w jednym `CALL` (z powtórzonym startem) zostawiają tylko około 44 µs. To za
    mały zapas.
- **Najwyżej 32 bajty na transakcję,** bo tyle ma bufor biblioteki Wire. Dłuższy zapis skończy
  się kodem E=2.

## Wskazówki

- Niepodłączone wejścia A1–A3 pokazują przypadkowe wartości. Do testu ADC podłącz potencjometr
  (skrajne nóżki do 5 V i GND, środkowa do A1) albo użyj kanału 0.
- Błędy w teście ECHO: dołóż zewnętrzne rezystory 4,7 kΩ na A4 i A5, skróć przewody i sprawdź,
  czy dioda jest Schottky'ego.
- Pomiar prędkości: na prawdziwym PC-1500 wyszło 9,397 kHz i 1011 B/s. „kHz” to średnia z całego
  zapisu: 9 taktów na bajt (8 bitów + ACK) razy 32 bajty, podzielone przez czas. Pojedyncze
  okresy zegara mają od około 60 do 200 µs, zależnie od danych.
