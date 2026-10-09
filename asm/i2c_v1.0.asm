; =====================================================================
; I2C v1.0 - programowy master I2C dla Sharp PC-1500(A)
; SCL = PC7 (pin 10, &F008 bit 7), SDA = PB2 (CMT-IN, pin 27) albo PB0
; (pin 9) - wybor w instalatorze. CPU LH5801 @ 1,3 MHz.
;
; Wywolanie z BASIC-a (IA = RAM+&4FC, IS = RAM+&4FF, IC = RAM+&400):
;   POKE IA, D, W, R     D = adres urzadzenia (7 bitow, 0..127)
;                        W = liczba bajtow do wyslania z TX (0..255)
;                        R = liczba bajtow do odebrania do RX (0..255)
;   CALL IC              albo   CALL IC,E   (E = wynik)
; Transakcja:
;   W>0 albo R=0:  START, D*2+0 (zapis), bajty TX+0 .. TX+W-1
;   R>0:           START (powtorzony), D*2+1 (odczyt), bajty do RX+0 ..
;                  RX+R-1 (ACK po kazdym bajcie, NACK po ostatnim)
;   na koncu STOP. W=0 i R=0: sam adres - test obecnosci (skaner).
; Wynik (bajt IS, a przy CALL IC,E takze zmienna E; powrot z C=1):
;   0 = OK
;   1 = brak ACK po adresie (nie ma urzadzenia pod tym adresem)
;   2 = brak ACK po bajcie danych (urzadzenie odrzucilo zapis)
;   3 = SDA stale w stanie niskim (magistrala zablokowana; 10 impulsow
;       SCL jej nie zwolnilo), transakcja nie zostala zaczeta
; Bufory jak w SERIN/SEROUT v6.x: TX = RAM+&300, RX = RAM+&200, kazdy
; od poczatku strony pamieci, wiec licznik bajtow = mlodszy bajt adresu.
;
; Linie:
;   SCL: port C ma tylko wyjscia, wiec PC7 steruje SCL push-pull:
;        nie ma "clock stretching" (urzadzenie nie moze wstrzymac zegara).
;   SDA: open-drain udawany rejestrem kierunku DDB (&F00D):
;        SDA = 1: bit DDB = 0 (wejscie), stan wysoki daje rezystor
;        SDA = 0: bit DDB = 1 (wyjscie) i zatrzask OPB (&F00F) = 0.
;        Zatrzask jest zerowany po kazdym przelaczeniu na wyjscie, bo w
;        modelu LH5811 z MAME zapis do bitu-wejscia nie trafia do
;        zatrzasku, a odczyt portu wpisuje do niego stan nozki.
; Zegar SCL: ok. 11 kHz (bajt ok. 1 ms). Kazda zmiana linii to osobny
; rozkaz ORI/ANI (>= 17 cykli = 13 us), wiec minimalne czasy I2C (4,7 us)
; sa zawsze zachowane bez petli opozniajacych; I2C nie ma dolnej granicy
; czestotliwosci zegara.
;
; Rejestry: X = &F00D (DDB), Y = &F008 (port C), U = wskaznik bufora,
; A = dane. Liczniki bitow bez rejestru: znacznik (sentinel) jak w SEROUT.
; Z po SHL sprawdzamy przez BII A,&FF, bo wedlug instrukcji LH5801 rozkazy
; przesuniec nie ustawiaja flagi Z.
;
; Adres ladowania: IC = RAM+&400 (&4400 na PC-1500A), 241 bajtow, a za
; kodem parametry D, W, R i wynik (IA = RAM+&4FC .. IS = RAM+&4FF).
; Symbole wstawiane przez BASIC: IP = strona kodu (adresy SJP i
; parametrow), TP/RP = strony buforow TX/RX, BM = maska bitu SDA w
; porcie B (4 = PB2, 1 = PB0), DB = 255-BM.
; =====================================================================
        RIE                     ; bez przerwan
        LDI  XH,&F0
        LDI  XL,&0D             ; X = &F00D (DDB: kierunek SDA)
        LDI  YH,&F0
        LDI  YL,&08             ; Y = &F008 (port C: SCL = PC7)
        ANI  #(X),DB            ; SDA = 1 (zwolnione)
        ORI  #(Y),&80           ; SCL = 1
        LDI  UL,9               ; SDA = 0: do 10 impulsow SCL
BUSY:   BII  #(&F00F),BM        ; SDA = 1 -> magistrala wolna
        BZR+ FREE
        ANI  #(Y),&7F           ; impuls SCL: urzadzenie, ktore
        ORI  #(Y),&80           ; trzyma SDA, wysyla kolejny bit
        LOP  UL,BUSY
        LDI  A,3                ; SDA nadal 0: wynik 3, bez START/STOP
        BCH+ EXIT
FREE:   LDA  (IP,&FD)           ; W
        BZR+ WR
        LDA  (IP,&FE)           ; R
        BZR+ RD                 ; W = 0, R > 0: od razu odczyt
WR:     SJP  START
        LDA  (IP,&FC)           ; D
        SHL                     ; D*2+0: zapis
        SJP  SEND
        BCS+ NAKA
        LDI  UH,TP
        LDI  UL,0               ; U = TX
WNEXT:  LDA  UL
        CPA  (IP,&FD)           ; wyslano W bajtow?
        BZS+ WEND
        LIN  U                  ; A = bajt, U++
        SJP  SEND
        BCS+ NAKD
        BCH- WNEXT
WEND:   LDA  (IP,&FE)           ; R
        BZS+ OK
RD:     SJP  START              ; START albo powtorzony START
        LDA  (IP,&FC)
        SEC
        ROL                     ; D*2+1: odczyt
        SJP  SEND
        BCS+ NAKA
        LDI  UH,RP
        LDI  UL,0               ; U = RX
RNEXT:  SJP  RECV               ; A = bajt, SCL = 0
        SIN  U                  ; zapisz, U++
        LDA  UL
        CPA  (IP,&FE)           ; odebrano R bajtow?
        BZS+ RLAST
        SJP  SDA0               ; ACK: SDA = 0 ...
        ORI  #(Y),&80           ; ... w 9. impulsie SCL
        ANI  #(Y),&7F
        BCH- RNEXT              ; RECV zwalnia SDA
RLAST:  ORI  #(Y),&80           ; NACK (SDA = 1) w 9. impulsie SCL
        ANI  #(Y),&7F
OK:     LDI  A,0
        BCH+ STOP
NAKA:   LDI  A,1
        BCH+ STOP
NAKD:   LDI  A,2
STOP:   SJP  SDA0               ; SDA = 0 przy SCL = 0
        ORI  #(Y),&80           ; SCL = 1
        ANI  #(X),DB            ; SDA 0 -> 1 przy SCL = 1: STOP
EXIT:   STA  (IP,&FF)           ; IS = wynik
        STA  XL
        LDI  XH,0               ; X = wynik ...
        SEC                     ; ... C=1: BASIC wpisze X do zmiennej z CALL IC,E
        RTN
;
; START: SDA 1 -> 0 przy SCL = 1, potem SCL = 0
START:  ANI  #(X),DB            ; SDA = 1
        ORI  #(Y),&80           ; SCL = 1
        SJP  SDA0               ; SDA 1 -> 0 przy SCL = 1: START
        ANI  #(Y),&7F           ; SCL = 0
        RTN
;
; SDA0: SDA = 0 (wyjscie, zatrzask 0); A i flaga C bez zmian
SDA0:   ORI  #(X),BM            ; SDA = wyjscie ...
        ANI  #(&F00F),DB        ; ... zatrzask OPB = 0
        RTN
;
; SEND: wyslij A (najstarszy bit pierwszy); C = 0 ACK, C = 1 brak ACK
SEND:   SEC
        ROL                     ; C = bit 7, znacznik 1 -> bit 0
SBIT:   BCS+ SONE
        SJP  SDA0               ; bit 0: SDA = 0
        BCH+ SCLK
SONE:   ANI  #(X),DB            ; bit 1: SDA = 1
SCLK:   ORI  #(Y),&80           ; impuls SCL
        ANI  #(Y),&7F
        SHL                     ; C = nastepny bit
        BII  A,&FF              ; A = 0 -> wyslano 8 bitow
        BZR- SBIT
        ANI  #(X),DB            ; zwolnij SDA: ACK nadaje urzadzenie
        ORI  #(Y),&80           ; 9. impuls SCL
        SEC                     ; C = 1: brak ACK ...
        BII  #(&F00F),BM
        BZR+ SNAK
        REC                     ; ... SDA = 0: ACK, C = 0
SNAK:   ANI  #(Y),&7F           ; SCL = 0
        RTN
;
; RECV: odbierz bajt do A (najstarszy bit pierwszy), konczy z SCL = 0
RECV:   ANI  #(X),DB            ; SDA = 1: nadaje urzadzenie
        LDI  A,1                ; znacznik: po 8 bitach wyjdzie do C
RBIT:   ORI  #(Y),&80           ; SCL = 1
        SEC
        BII  #(&F00F),BM        ; odczyt SDA
        BZR+ RONE
        REC
RONE:   ROL                     ; A = A*2 + bit
        ANI  #(Y),&7F           ; SCL = 0
        BCR- RBIT
        RTN
