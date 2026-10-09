; =====================================================================
; I2C v1.1 - programowy master I2C dla Sharp PC-1500(A), wersja szybsza
; SCL = PC7 (pin 10, &F008 bit 7), SDA = PB2 (CMT-IN, pin 27) albo PB0
; (pin 9) - wybor w instalatorze. CPU LH5801 @ 1,3 MHz.
;
; Wywolanie, parametry, wyniki i pamiec bez zmian wzgledem v1.0:
;   POKE IA, D, W, R : CALL IC   albo   CALL IC,E   (E = wynik)
;   IC = RAM+&400, IA = RAM+&4FC (D, W, R), IS = RAM+&4FF (wynik),
;   TX = RAM+&300, RX = RAM+&200.
;   Wynik: 0 = OK, 1 = brak ACK po adresie, 2 = brak ACK po bajcie
;   danych, 3 = SDA stale w stanie niskim (transakcja nie zaczeta).
;
; Co przyspiesza v1.1 (czasy z tablicy cykli MAME):
; 1. Bit 0 przy nadawaniu to sam rozkaz ORI #(X),BM (SDA = wyjscie),
;    bez zerowania zatrzasku OPB i bez SJP/RTN. Zatrzask SDA ma zawsze
;    wartosc 0, gdy SEND zaczyna bajt: po STARCIE zeruje go SDA0, a po
;    bajcie danych odczyt ACK (SDA = 0) wpisuje 0 do zatrzasku w modelu
;    MAME; w modelu prawdziwego zatrzasku 0 zostaje po SDA0. Przy NACK
;    nastepny jest STOP, ktory zatrzask zeruje sam.
; 2. Petla nadawania ma dwa stany: H (SDA = 1) i L (SDA = 0). SDA zmienia
;    sie tylko wtedy, gdy bit rozni sie od poprzedniego, a bajt konczacy
;    sie bitem 1 nie musi juz zwalniac SDA przed ACK.
; 3. Odbior: X wskazuje na OPB (&F00F), wiec odczyt SDA to BII #(X),BM
;    (14 cykli zamiast 20). Bit 0 nie potrzebuje REC: w petli C = 0, bo
;    znacznik wychodzi do C dopiero po 8. bicie. ACK odbiornika uzywa
;    adresow bezwzglednych &F00D, bez przelaczania X.
; 4. Adres (zapis i odczyt) wysyla jedna procedura ADDR, ktora przechodzi
;    prosto w SEND. START zawiera kod SDA0 (bez SJP/RTN), a STOP wywoluje
;    SDA0 wewnatrz START.
; Po bajcie adresu do odczytu jest celowe opoznienie, zeby urzadzenie
; (np. Arduino) mialo czas przygotowac dane - SCL zostaje w stanie
; niskim ok. 70 us, jak w v1.0.
;
; Linie (jak w v1.0):
;   SCL: port C ma tylko wyjscia, PC7 steruje SCL push-pull, bez
;        "clock stretching".
;   SDA: open-drain udawany rejestrem kierunku DDB (&F00D):
;        SDA = 1: bit DDB = 0 (wejscie), stan wysoki daje rezystor;
;        SDA = 0: bit DDB = 1 (wyjscie) przy zatrzasku OPB = 0.
; Kazda zmiana linii to osobny rozkaz ORI/ANI (>= 17 cykli = 13 us), wiec
; minimalne czasy I2C (4,7 us) sa zachowane bez petli opozniajacych.
;
; Rejestry: X = &F00D (DDB), w odbiorze &F00F (OPB); Y = &F008 (port C);
; U = wskaznik bufora; A = dane. Licznik bitow: znacznik (sentinel).
; Z po SHL sprawdzamy przez BII A,&FF, bo wedlug instrukcji LH5801 rozkazy
; przesuniec nie ustawiaja flagi Z.
;
; Adres ladowania: IC = RAM+&400 (&4400 na PC-1500A), za kodem parametry
; D, W, R i wynik (IA = RAM+&4FC .. IS = RAM+&4FF).
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
WR:     REC                     ; C = 0: adres do zapisu
        SJP  ADDR
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
RD:     SEC                     ; C = 1: adres do odczytu
        SJP  ADDR
        BCS+ NAKA
        SJP  SRET               ; 30 cykli dla urzadzenia na przygotowanie danych
        LDI  UH,RP
        LDI  UL,0               ; U = RX
        LDI  XL,&0F             ; X = &F00F (OPB: odczyt SDA), C = 0
RBYTE:  LDI  A,1                ; znacznik: po 8 bitach wyjdzie do C
RBIT:   ORI  #(Y),&80           ; SCL = 1
        BII  #(X),BM            ; odczyt SDA
        BZS+ R0
        SEC                     ; bit 1 (w petli C = 0, wiec bit 0 bez REC)
R0:     ROL                     ; A = A*2 + bit
        ANI  #(Y),&7F           ; SCL = 0
        BCR- RBIT               ; C = 1: znacznik wyszedl, bajt gotowy
        SIN  U                  ; zapisz, U++
        LDA  UL
        CPA  (IP,&FE)           ; C = 1: to byl ostatni (R-ty) bajt
        BCS+ R9                 ; ostatni: NACK (SDA = 1)
        ORI  #(&F00D),BM        ; ACK: SDA = wyjscie ...
        ANI  #(X),DB            ; ... zatrzask OPB = 0
R9:     ORI  #(Y),&80           ; 9. impuls SCL (ACK albo NACK)
        ANI  #(Y),&7F           ; (ORI/ANI nie zmieniaja C)
        BCS+ RDONE
        ANI  #(&F00D),DB        ; zwolnij SDA; C = 0
        BCH- RBYTE
RDONE:  LDI  XL,&0D             ; X = DDB
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
; START: SDA 1 -> 0 przy SCL = 1, potem SCL = 0 (A i C bez zmian).
; SDA0 (wejscie z STOP przy SCL = 0): SDA = 0 (wyjscie, zatrzask 0), a
; powtorne SCL = 0 niczego wtedy nie zmienia.
START:  ANI  #(X),DB            ; SDA = 1
        ORI  #(Y),&80           ; SCL = 1
SDA0:   ORI  #(X),BM            ; SDA = wyjscie ...
        ANI  #(&F00F),DB        ; ... zatrzask OPB = 0: SDA 1 -> 0 (START)
        ANI  #(Y),&7F           ; SCL = 0
        RTN
;
; ADDR: START (albo powtorzony START), potem bajt D*2+C przez SEND. Po
; STARCIE ADDR zwalnia SDA (przy SCL = 0 wolno), wiec SEND zaczyna w stanie H.
ADDR:   SJP  START
        LDA  (IP,&FC)           ; D
        ROL                     ; D*2 + C (0 zapis, 1 odczyt)
        ANI  #(X),DB            ; SDA = 1
; SEND: wyslij A (najstarszy bit pierwszy); C = 0 ACK, C = 1 brak ACK.
; Wejscie przy SDA zwolnionym (po ACK poprzedniego bajtu): stan H.
; Petla ma dwa stany: H (SDA = 1) i L (SDA = 0); linie SDA zmienia tylko
; bit inny niz poprzedni. Na wejsciu zatrzask SDA = 0 (patrz wyzej), wiec
; SDA = 0 to tylko SDA = wyjscie.
SEND:   SEC
        ROL                     ; C = bit 7, znacznik 1 -> bit 0
HB:     BCR+ HL                 ; stan H, bit 0: SDA = 0
HC:     ORI  #(Y),&80           ; impuls SCL
        ANI  #(Y),&7F
        SHL                     ; C = nastepny bit
        BII  A,&FF              ; A = 0 -> wyslano 8 bitow
        BZR- HB
        BCH+ SACKH              ; SDA juz zwolnione
HL:     ORI  #(X),BM            ; SDA = 0 -> stan L
LC:     ORI  #(Y),&80           ; impuls SCL
        ANI  #(Y),&7F
        SHL
        BII  A,&FF
        BZS+ SACKL
LB:     BCR- LC                 ; stan L, bit 0: SDA bez zmian
        ANI  #(X),DB            ; bit 1: SDA = 1 -> stan H
        BCH- HC
SACKL:  ANI  #(X),DB            ; zwolnij SDA: ACK nadaje urzadzenie
SACKH:  ORI  #(Y),&80           ; 9. impuls SCL
        SEC                     ; C = 1: brak ACK ...
        BII  #(&F00F),BM        ; (odczyt ACK = 0 zeruje zatrzask w modelu MAME)
        BZR+ SNAK
        REC                     ; ... SDA = 0: ACK, C = 0
SNAK:   ANI  #(Y),&7F           ; SCL = 0
SRET:   RTN
