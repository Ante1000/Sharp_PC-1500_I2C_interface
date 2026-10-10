; =====================================================================
; I2C v1.1 - software I2C master for the Sharp PC-1500(A), faster version
; SCL = PC7 (pin 10, &F008 bit 7), SDA = PB2 (CMT-IN, pin 27) or PB0
; (pin 9) - chosen in the installer. CPU LH5801 @ 1.3 MHz.
;
; Call, parameters, results and memory unchanged from v1.0:
;   POKE IA, D, W, R : CALL IC   or   CALL IC,E   (E = result)
;   IC = RAM+&400, IA = RAM+&4FC (D, W, R), IS = RAM+&4FF (result),
;   TX = RAM+&300, RX = RAM+&200.
;   Result: 0 = OK, 1 = no ACK after the address, 2 = no ACK after a data
;   byte, 3 = SDA stays low (transaction not started).
;
; What makes v1.1 faster (times from the MAME cycle table):
; 1. A 0 bit when sending is the single instruction ORI #(X),BM (SDA =
;    output), without clearing the OPB latch and without SJP/RTN. The SDA
;    latch is always 0 when SEND starts a byte: after START, SDA0 clears
;    it, and after a data byte the ACK read (SDA = 0) writes 0 into the
;    latch in the MAME model; in the model of a real latch the 0 stays
;    from SDA0. After a NACK comes STOP, which clears the latch itself.
; 2. The send loop has two states: H (SDA = 1) and L (SDA = 0). SDA only
;    changes when a bit differs from the previous one, and a byte that
;    ends with a 1 bit no longer has to release SDA before the ACK.
; 3. Receiving: X points to OPB (&F00F), so reading SDA is BII #(X),BM
;    (14 cycles instead of 20). A 0 bit needs no REC: C = 0 inside the
;    loop, because the sentinel only leaves into C after the 8th bit.
;    The receiver ACK uses the absolute address &F00D, without switching X.
; 4. One routine ADDR sends the address (for write and read) and runs
;    straight into SEND. START contains the SDA0 code (without SJP/RTN),
;    and STOP calls SDA0 inside START.
; After the read address there is a deliberate delay, so that the device
; (e.g. an Arduino) has time to prepare its data - SCL stays low for
; about 70 us, as in v1.0.
;
; Lines (as in v1.0):
;   SCL: port C has outputs only, PC7 drives SCL push-pull, no
;        "clock stretching".
;   SDA: open drain emulated with the direction register DDB (&F00D):
;        SDA = 1: DDB bit = 0 (input), the resistor makes the high level;
;        SDA = 0: DDB bit = 1 (output) with output latch OPB = 0.
; Every line change is a separate ORI/ANI instruction (>= 17 cycles =
; 13 us), so the minimum I2C times (4.7 us) hold without delay loops.
;
; Registers: X = &F00D (DDB), while receiving &F00F (OPB); Y = &F008
; (port C); U = buffer pointer; A = data. Bit counter: a sentinel bit.
; Z after SHL is tested with BII A,&FF, because according to the LH5801
; manual the shift instructions do not set the Z flag.
;
; Load address: IC = RAM+&400 (&4400 on a PC-1500A), followed by the
; parameters D, W, R and the result (IA = RAM+&4FC .. IS = RAM+&4FF).
; Symbols filled in by BASIC: IP = page of the code (SJP addresses and
; parameters), TP/RP = pages of the TX/RX buffers, BM = bit mask of SDA
; in port B (4 = PB2, 1 = PB0), DB = 255-BM.
; =====================================================================
        RIE                     ; no interrupts
        LDI  XH,&F0
        LDI  XL,&0D             ; X = &F00D (DDB: direction of SDA)
        LDI  YH,&F0
        LDI  YL,&08             ; Y = &F008 (port C: SCL = PC7)
        ANI  #(X),DB            ; SDA = 1 (released)
        ORI  #(Y),&80           ; SCL = 1
        LDI  UL,9               ; SDA = 0: up to 10 SCL pulses
BUSY:   BII  #(&F00F),BM        ; SDA = 1 -> bus free
        BZR+ FREE
        ANI  #(Y),&7F           ; SCL pulse: a device that holds
        ORI  #(Y),&80           ; SDA sends its next bit
        LOP  UL,BUSY
        LDI  A,3                ; SDA still 0: result 3, no START/STOP
        BCH+ EXIT
FREE:   LDA  (IP,&FD)           ; W
        BZR+ WR
        LDA  (IP,&FE)           ; R
        BZR+ RD                 ; W = 0, R > 0: read at once
WR:     REC                     ; C = 0: address for writing
        SJP  ADDR
        BCS+ NAKA
        LDI  UH,TP
        LDI  UL,0               ; U = TX
WNEXT:  LDA  UL
        CPA  (IP,&FD)           ; W bytes sent?
        BZS+ WEND
        LIN  U                  ; A = byte, U++
        SJP  SEND
        BCS+ NAKD
        BCH- WNEXT
WEND:   LDA  (IP,&FE)           ; R
        BZS+ OK
RD:     SEC                     ; C = 1: address for reading
        SJP  ADDR
        BCS+ NAKA
        SJP  SRET               ; 30 cycles for the device to prepare its data
        LDI  UH,RP
        LDI  UL,0               ; U = RX
        LDI  XL,&0F             ; X = &F00F (OPB: read SDA), C = 0
RBYTE:  LDI  A,1                ; sentinel: leaves into C after 8 bits
RBIT:   ORI  #(Y),&80           ; SCL = 1
        BII  #(X),BM            ; read SDA
        BZS+ R0
        SEC                     ; bit 1 (C = 0 in the loop, so bit 0 needs no REC)
R0:     ROL                     ; A = A*2 + bit
        ANI  #(Y),&7F           ; SCL = 0
        BCR- RBIT               ; C = 1: the sentinel left, byte complete
        SIN  U                  ; store, U++
        LDA  UL
        CPA  (IP,&FE)           ; C = 1: this was the last (R-th) byte
        BCS+ R9                 ; last one: NACK (SDA = 1)
        ORI  #(&F00D),BM        ; ACK: SDA = output ...
        ANI  #(X),DB            ; ... latch OPB = 0
R9:     ORI  #(Y),&80           ; 9th SCL pulse (ACK or NACK)
        ANI  #(Y),&7F           ; (ORI/ANI do not change C)
        BCS+ RDONE
        ANI  #(&F00D),DB        ; release SDA; C = 0
        BCH- RBYTE
RDONE:  LDI  XL,&0D             ; X = DDB
OK:     LDI  A,0
        BCH+ STOP
NAKA:   LDI  A,1
        BCH+ STOP
NAKD:   LDI  A,2
STOP:   SJP  SDA0               ; SDA = 0 while SCL = 0
        ORI  #(Y),&80           ; SCL = 1
        ANI  #(X),DB            ; SDA 0 -> 1 while SCL = 1: STOP
EXIT:   STA  (IP,&FF)           ; IS = result
        STA  XL
        LDI  XH,0               ; X = result ...
        SEC                     ; ... C=1: BASIC copies X into E of CALL IC,E
        RTN
;
; START: SDA 1 -> 0 while SCL = 1, then SCL = 0 (A and C unchanged).
; SDA0 (entry from STOP while SCL = 0): SDA = 0 (output, latch 0), and
; the second SCL = 0 then changes nothing.
START:  ANI  #(X),DB            ; SDA = 1
        ORI  #(Y),&80           ; SCL = 1
SDA0:   ORI  #(X),BM            ; SDA = output ...
        ANI  #(&F00F),DB        ; ... latch OPB = 0: SDA 1 -> 0 (START)
        ANI  #(Y),&7F           ; SCL = 0
        RTN
;
; ADDR: START (or repeated START), then the byte D*2+C through SEND. After
; START, ADDR releases SDA (allowed while SCL = 0), so SEND starts in state H.
ADDR:   SJP  START
        LDA  (IP,&FC)           ; D
        ROL                     ; D*2 + C (0 write, 1 read)
        ANI  #(X),DB            ; SDA = 1
; SEND: send A (most significant bit first); C = 0 ACK, C = 1 no ACK.
; Entry with SDA released (after the ACK of the previous byte): state H.
; The loop has two states: H (SDA = 1) and L (SDA = 0); only a bit that
; differs from the previous one changes SDA. On entry the SDA latch = 0
; (see above), so SDA = 0 just means SDA = output.
SEND:   SEC
        ROL                     ; C = bit 7, sentinel 1 -> bit 0
HB:     BCR+ HL                 ; state H, bit 0: SDA = 0
HC:     ORI  #(Y),&80           ; SCL pulse
        ANI  #(Y),&7F
        SHL                     ; C = next bit
        BII  A,&FF              ; A = 0 -> 8 bits sent
        BZR- HB
        BCH+ SACKH              ; SDA already released
HL:     ORI  #(X),BM            ; SDA = 0 -> state L
LC:     ORI  #(Y),&80           ; SCL pulse
        ANI  #(Y),&7F
        SHL
        BII  A,&FF
        BZS+ SACKL
LB:     BCR- LC                 ; state L, bit 0: SDA unchanged
        ANI  #(X),DB            ; bit 1: SDA = 1 -> state H
        BCH- HC
SACKL:  ANI  #(X),DB            ; release SDA: the device sends the ACK
SACKH:  ORI  #(Y),&80           ; 9th SCL pulse
        SEC                     ; C = 1: no ACK ...
        BII  #(&F00F),BM        ; (reading ACK = 0 clears the latch in the MAME model)
        BZR+ SNAK
        REC                     ; ... SDA = 0: ACK, C = 0
SNAK:   ANI  #(Y),&7F           ; SCL = 0
SRET:   RTN
