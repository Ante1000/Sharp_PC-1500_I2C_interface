; =====================================================================
; I2C v1.3 - software I2C master for the Sharp PC-1500(A), fastest version
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
; From v1.1 (times from the MAME cycle table):
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
; From v1.2 (sending about 1.15 times faster than in v1.1):
; 5. The end-of-byte test (sentinel, A = 0) only after a 1 bit: the
;    sentinel is a one, so after a 0 bit (C = 0) it cannot be the end.
; 6. SEND sends the address and all W bytes from TX in one loop, without
;    SJP/RTN per byte; the ACK without SEC/REC. The read address ends the
;    loop at once, because UL = W (FREE clears UL, after a write UL = W).
; 7. The path "state L, bit 1" goes straight into the SCL clock of state H.
; 8. After START the address starts in state L (SDA already = 0).
; New in v1.3 (sending about 1.07 times faster than in v1.2):
; 9. No BII A,&FF: the end of a byte (A = 0) is shown by the Z flag set by
;    SHL/ROL. This NEEDS a CPU whose SHL and ROL set Z (MAME does so; the
;    LH5801 manual says otherwise) - the v1.3 installer tests this first
;    and does not install the code without it.
; 10. Counters: the instruction CPI UL,n (7 cycles) instead of LDA UL +
;    CPA (IP,&FD/&FE) (18 cycles); every call writes n (W and R) into the
;    code (FREE, RD).
; After the read address SCL stays low for about 75 us, so that the
; device (e.g. an Arduino) has time to prepare its data.
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
; End of a byte: the Z flag after SHL/ROL (see point 9).
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
FREE:   LDI  UL,0               ; UL = 0 (= W when reading without a write)
        LDA  (IP,&FD)           ; W
        STA  (IP,WCMP+1)        ; W into the instruction CPI UL,W in SEND
        BZR+ WR
        LDA  (IP,&FE)           ; R
        BZR+ RD                 ; W = 0, R > 0: read at once
WR:     LDI  UH,TP              ; U = TX
        REC                     ; C = 0: address for writing
        SJP  ADDR               ; address, then W bytes from TX
        BCS+ WEND               ; C = 1: everything acknowledged
        LDA  UL                 ; no ACK: UL = 0 -> after the address
        BZS+ NAKA
        BCH+ NAKD
WEND:   LDA  (IP,&FE)           ; R
        BZS+ OK
RD:     STA  (IP,RCMP+1)        ; A = R (from FREE or WEND) into CPI UL,R
        SEC                     ; C = 1: address for reading (UL = W, so SEND
        SJP  ADDR               ; ends after the address alone)
        BCR+ NAKA
        NOP                     ; time for the device: SCL low for about 75 us
        NOP
        REC                     ; C = 0: the receive loop needs this
        LDI  UH,RP
        LDI  UL,0               ; U = RX
        LDI  XL,&0F             ; X = &F00F (OPB: read SDA)
RBYTE:  LDI  A,1                ; sentinel: leaves into C after 8 bits
RBIT:   ORI  #(Y),&80           ; SCL = 1
        BII  #(X),BM            ; read SDA
        BZS+ R0
        SEC                     ; bit 1 (C = 0 in the loop, so bit 0 needs no REC)
R0:     ROL                     ; A = A*2 + bit
        ANI  #(Y),&7F           ; SCL = 0
        BCR- RBIT               ; C = 1: the sentinel left, byte complete
        SIN  U                  ; store, U++
RCMP:   CPI  UL,0               ; UL - R (R written in RD): C = 1 for the last one
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
; ADDR: START (or repeated START), then the byte D*2+C and further bytes from TX.
; After START SDA = 0, so sending starts in state L.
ADDR:   SJP  START
        LDA  (IP,&FC)           ; D
        ROL                     ; D*2 + C (0 write, 1 read)
        SEC
        ROL                     ; C = bit 7, sentinel 1 -> bit 0
        BCH+ LDEC
;
; SEND: sends the byte in A, then further bytes from TX (LIN U), until UL = W.
; Result: C = 1 everything acknowledged, C = 0 no ACK (UL tells after which).
; Two states: H (SDA = 1) and L (SDA = 0); only a bit that differs from the
; previous one changes SDA. The end of a byte (sentinel: A = 0, Z flag after
; SHL/ROL) only counts after a 1 bit - after a 0 bit (C = 0) it cannot be the
; sentinel. SDA latch = 0 (see v1.1), so SDA = 0 just means SDA = output.
H0:     ORI  #(X),BM            ; H, bit 0: SDA = 0 -> state L
LC:     ORI  #(Y),&80           ; SCL pulse
        ANI  #(Y),&7F
        SHL                     ; C = next bit
LDEC:   BCR- LC                 ; L, bit 0: SDA unchanged
        BZS+ SACKL              ; C = 1 and Z = 1: sentinel, end of the byte
        ANI  #(X),DB            ; L, bit 1: SDA = 1 -> state H
HC:     ORI  #(Y),&80           ; SCL pulse
        ANI  #(Y),&7F
        SHL
HDEC:   BCR- H0                 ; H, bit 0
        BZR- HC                 ; H, bit 1: SDA unchanged
        BCH+ SACKH              ; sentinel in state H: SDA already released
SACKL:  ANI  #(X),DB            ; release SDA: the device sends the ACK
SACKH:  ORI  #(Y),&80           ; 9th SCL pulse
        BII  #(&F00F),BM        ; ACK = 0 (the read clears the latch in the MAME model)
        BZR+ SNAK
        ANI  #(Y),&7F           ; SCL = 0
WCMP:   CPI  UL,0               ; W bytes sent? (W written in FREE; equal: C = 1)
        BZS+ SDONE
        LIN  U                  ; A = byte, U++
        SEC
        ROL                     ; C = bit 7, sentinel 1 -> bit 0
        BCH- HDEC               ; after the ACK SDA is released: state H
SNAK:   ANI  #(Y),&7F           ; SCL = 0
        REC                     ; C = 0: no ACK
SDONE:  RTN
