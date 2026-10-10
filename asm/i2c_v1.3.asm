; =====================================================================
; I2C v1.3 - programowy master I2C dla Sharp PC-1500(A), najszybsza wersja
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
; Z v1.1 (czasy z tablicy cykli MAME):
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
; Nowe w v1.2 (nadawanie ok. 1,15 raza szybsze niz w v1.1):
; 5. Test konca bajtu (znacznik, A = 0) tylko po bicie 1: znacznik jest
;    jedynka, wiec po bicie 0 (C = 0) nie moze to byc koniec.
; 6. SEND wysyla adres i wszystkie W bajtow z TX w jednej petli, bez
;    SJP/RTN na bajt; ACK bez SEC/REC. Adres do odczytu konczy petle od
;    razu, bo UL = W (FREE zeruje UL, a po zapisie UL = W).
; 7. Sciezka "stan L, bit 1" przechodzi wprost do taktu SCL stanu H.
; 8. Po STARCIE adres zaczyna sie w stanie L (SDA juz = 0).
; Nowe w v1.3 (nadawanie ok. 1,07 raza szybsze niz w v1.2):
; 9. Bez BII A,&FF: koniec bajtu (A = 0) pokazuje flaga Z ustawiona przez
;    SHL/ROL. WYMAGA procesora, w ktorym SHL i ROL ustawiaja Z (tak jest w
;    MAME; instrukcja LH5801 mowi inaczej) - instalator v1.3 sprawdza to
;    najpierw i bez tego nie instaluje kodu.
; 10. Liczniki: zamiast LDA UL + CPA (IP,&FD/&FE) (18 cykli) rozkaz
;    CPI UL,n (7 cykli); n (W i R) wpisuje do kodu kazde wywolanie (FREE,
;    RD).
; Po adresie do odczytu SCL zostaje w stanie niskim ok. 75 us, zeby
; urzadzenie (np. Arduino) zdazylo przygotowac dane.
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
; Koniec bajtu: flaga Z po SHL/ROL (patrz punkt 9).
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
FREE:   LDI  UL,0               ; UL = 0 (= W przy odczycie bez zapisu)
        LDA  (IP,&FD)           ; W
        STA  (IP,WCMP+1)        ; W do rozkazu CPI UL,W w SEND
        BZR+ WR
        LDA  (IP,&FE)           ; R
        BZR+ RD                 ; W = 0, R > 0: od razu odczyt
WR:     LDI  UH,TP              ; U = TX
        REC                     ; C = 0: adres do zapisu
        SJP  ADDR               ; adres, potem W bajtow z TX
        BCS+ WEND               ; C = 1: wszystko potwierdzone
        LDA  UL                 ; brak ACK: UL = 0 -> po adresie
        BZS+ NAKA
        BCH+ NAKD
WEND:   LDA  (IP,&FE)           ; R
        BZS+ OK
RD:     STA  (IP,RCMP+1)        ; A = R (z FREE albo WEND) do CPI UL,R
        SEC                     ; C = 1: adres do odczytu (UL = W, wiec SEND
        SJP  ADDR               ; konczy po samym adresie)
        BCR+ NAKA
        NOP                     ; czas dla urzadzenia: SCL nisko ok. 75 us
        NOP
        REC                     ; C = 0: petla odbioru tego wymaga
        LDI  UH,RP
        LDI  UL,0               ; U = RX
        LDI  XL,&0F             ; X = &F00F (OPB: odczyt SDA)
RBYTE:  LDI  A,1                ; znacznik: po 8 bitach wyjdzie do C
RBIT:   ORI  #(Y),&80           ; SCL = 1
        BII  #(X),BM            ; odczyt SDA
        BZS+ R0
        SEC                     ; bit 1 (w petli C = 0, wiec bit 0 bez REC)
R0:     ROL                     ; A = A*2 + bit
        ANI  #(Y),&7F           ; SCL = 0
        BCR- RBIT               ; C = 1: znacznik wyszedl, bajt gotowy
        SIN  U                  ; zapisz, U++
RCMP:   CPI  UL,0               ; UL - R (R wpisane w RD): C = 1 przy ostatnim
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
; ADDR: START (albo powtorzony START), potem bajt D*2+C i dalsze bajty z TX.
; Po STARCIE SDA = 0, wiec nadawanie zaczyna sie w stanie L.
ADDR:   SJP  START
        LDA  (IP,&FC)           ; D
        ROL                     ; D*2 + C (0 zapis, 1 odczyt)
        SEC
        ROL                     ; C = bit 7, znacznik 1 -> bit 0
        BCH+ LDEC
;
; SEND: nadaje bajt z A, potem kolejne bajty z TX (LIN U), az UL = W.
; Wynik: C = 1 wszystko potwierdzone, C = 0 brak ACK (UL mowi, po ktorym).
; Dwa stany: H (SDA = 1) i L (SDA = 0); SDA zmienia tylko bit inny niz
; poprzedni. Koniec bajtu (znacznik: A = 0, flaga Z po SHL/ROL) liczy sie
; tylko przy bicie 1 - przy bicie 0 (C = 0) to nie moze byc znacznik.
; Zatrzask SDA = 0 (patrz v1.1), wiec SDA = 0 to tylko SDA = wyjscie.
H0:     ORI  #(X),BM            ; H, bit 0: SDA = 0 -> stan L
LC:     ORI  #(Y),&80           ; impuls SCL
        ANI  #(Y),&7F
        SHL                     ; C = nastepny bit
LDEC:   BCR- LC                 ; L, bit 0: SDA bez zmian
        BZS+ SACKL              ; C = 1 i Z = 1: znacznik, koniec bajtu
        ANI  #(X),DB            ; L, bit 1: SDA = 1 -> stan H
HC:     ORI  #(Y),&80           ; impuls SCL
        ANI  #(Y),&7F
        SHL
HDEC:   BCR- H0                 ; H, bit 0
        BZR- HC                 ; H, bit 1: SDA bez zmian
        BCH+ SACKH              ; znacznik w stanie H: SDA juz zwolnione
SACKL:  ANI  #(X),DB            ; zwolnij SDA: ACK nadaje urzadzenie
SACKH:  ORI  #(Y),&80           ; 9. impuls SCL
        BII  #(&F00F),BM        ; ACK = 0 (odczyt zeruje zatrzask w modelu MAME)
        BZR+ SNAK
        ANI  #(Y),&7F           ; SCL = 0
WCMP:   CPI  UL,0               ; wyslano W bajtow? (W wpisane w FREE; rowne: C = 1)
        BZS+ SDONE
        LIN  U                  ; A = bajt, U++
        SEC
        ROL                     ; C = bit 7, znacznik 1 -> bit 0
        BCH- HDEC               ; po ACK SDA jest zwolnione: stan H
SNAK:   ANI  #(Y),&7F           ; SCL = 0
        REC                     ; C = 0: brak ACK
SDONE:  RTN
