; =====================================================================
; I2C v1.0 - software I2C master for the Sharp PC-1500(A)
; SCL = PC7 (pin 10, &F008 bit 7), SDA = PB2 (CMT-IN, pin 27) or PB0
; (pin 9) - chosen in the installer. CPU LH5801 @ 1.3 MHz.
;
; Call from BASIC (IA = RAM+&4FC, IS = RAM+&4FF, IC = RAM+&400):
;   POKE IA, D, W, R     D = device address (7 bits, 0..127)
;                        W = number of bytes to send from TX (0..255)
;                        R = number of bytes to receive into RX (0..255)
;   CALL IC              or   CALL IC,E   (E = result)
; Transaction:
;   W>0 or R=0:    START, D*2+0 (write), bytes TX+0 .. TX+W-1
;   R>0:           (repeated) START, D*2+1 (read), bytes into RX+0 ..
;                  RX+R-1 (ACK after each byte, NACK after the last one)
;   STOP at the end. W=0 and R=0: address only - presence test (scanner).
; Result (byte IS, and with CALL IC,E also the variable E; returns with C=1):
;   0 = OK
;   1 = no ACK after the address (no device at this address)
;   2 = no ACK after a data byte (the device refused the write)
;   3 = SDA stays low (bus blocked; 10 SCL pulses did not release it),
;       the transaction was not started
; Buffers as in SERIN/SEROUT v6.x: TX = RAM+&300, RX = RAM+&200, each
; at the start of a memory page, so the byte counter = low byte of the address.
;
; Lines:
;   SCL: port C has outputs only, so PC7 drives SCL push-pull:
;        no "clock stretching" (the device cannot hold the clock).
;   SDA: open drain emulated with the direction register DDB (&F00D):
;        SDA = 1: DDB bit = 0 (input), the resistor makes the high level
;        SDA = 0: DDB bit = 1 (output) and output latch OPB (&F00F) = 0.
;        The latch is cleared after every switch to output, because in
;        the LH5811 model of MAME a write to an input bit does not reach
;        the latch, and a read of the port writes the pin state into it.
; SCL clock: about 10 kHz (a byte takes about 1 ms). Every line change is
; a separate ORI/ANI instruction (>= 17 cycles = 13 us), so the minimum
; I2C times (4.7 us) always hold without delay loops; I2C has no lower
; limit for the clock frequency.
;
; Registers: X = &F00D (DDB), Y = &F008 (port C), U = buffer pointer,
; A = data. Bit counters without a register: a sentinel bit as in SEROUT.
; Z after SHL is tested with BII A,&FF, because according to the LH5801
; manual the shift instructions do not set the Z flag.
;
; Load address: IC = RAM+&400 (&4400 on a PC-1500A), 241 bytes, followed
; by the parameters D, W, R and the result (IA = RAM+&4FC .. IS = RAM+&4FF).
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
WR:     SJP  START
        LDA  (IP,&FC)           ; D
        SHL                     ; D*2+0: write
        SJP  SEND
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
RD:     SJP  START              ; START or repeated START
        LDA  (IP,&FC)
        SEC
        ROL                     ; D*2+1: read
        SJP  SEND
        BCS+ NAKA
        LDI  UH,RP
        LDI  UL,0               ; U = RX
RNEXT:  SJP  RECV               ; A = byte, SCL = 0
        SIN  U                  ; store, U++
        LDA  UL
        CPA  (IP,&FE)           ; R bytes received?
        BZS+ RLAST
        SJP  SDA0               ; ACK: SDA = 0 ...
        ORI  #(Y),&80           ; ... during the 9th SCL pulse
        ANI  #(Y),&7F
        BCH- RNEXT              ; RECV releases SDA
RLAST:  ORI  #(Y),&80           ; NACK (SDA = 1) during the 9th SCL pulse
        ANI  #(Y),&7F
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
; START: SDA 1 -> 0 while SCL = 1, then SCL = 0
START:  ANI  #(X),DB            ; SDA = 1
        ORI  #(Y),&80           ; SCL = 1
        SJP  SDA0               ; SDA 1 -> 0 while SCL = 1: START
        ANI  #(Y),&7F           ; SCL = 0
        RTN
;
; SDA0: SDA = 0 (output, latch 0); A and the C flag unchanged
SDA0:   ORI  #(X),BM            ; SDA = output ...
        ANI  #(&F00F),DB        ; ... latch OPB = 0
        RTN
;
; SEND: send A (most significant bit first); C = 0 ACK, C = 1 no ACK
SEND:   SEC
        ROL                     ; C = bit 7, sentinel 1 -> bit 0
SBIT:   BCS+ SONE
        SJP  SDA0               ; bit 0: SDA = 0
        BCH+ SCLK
SONE:   ANI  #(X),DB            ; bit 1: SDA = 1
SCLK:   ORI  #(Y),&80           ; SCL pulse
        ANI  #(Y),&7F
        SHL                     ; C = next bit
        BII  A,&FF              ; A = 0 -> 8 bits sent
        BZR- SBIT
        ANI  #(X),DB            ; release SDA: the device sends the ACK
        ORI  #(Y),&80           ; 9th SCL pulse
        SEC                     ; C = 1: no ACK ...
        BII  #(&F00F),BM
        BZR+ SNAK
        REC                     ; ... SDA = 0: ACK, C = 0
SNAK:   ANI  #(Y),&7F           ; SCL = 0
        RTN
;
; RECV: receive a byte into A (most significant bit first), ends with SCL = 0
RECV:   ANI  #(X),DB            ; SDA = 1: the device sends
        LDI  A,1                ; sentinel: leaves into C after 8 bits
RBIT:   ORI  #(Y),&80           ; SCL = 1
        SEC
        BII  #(&F00F),BM        ; read SDA
        BZR+ RONE
        REC
RONE:   ROL                     ; A = A*2 + bit
        ANI  #(Y),&7F           ; SCL = 0
        BCR- RBIT
        RTN
