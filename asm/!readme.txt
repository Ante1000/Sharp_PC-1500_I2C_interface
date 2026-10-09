I2C v1.0 - LH5801 assembler source of the I2C master for the Sharp PC-1500(A)
(SCL = PC7, SDA = PB2 or PB0)

i2c_v1.0.asm / .lst      I2C master, 241 bytes, loaded at RAM+&400 (&4400 on a PC-1500A),
                         parameters D, W, R and the result at RAM+&4FC..&4FF

Operands that the installer (Basic/pc1500_i2c_installer-v1.0.txt) fills in:
  IP                   high byte of the code address (SJP targets and parameters)
  TP, RP               high bytes of the TX and RX buffers (the same buffers as the
                       Software UART SERIN/SEROUT v6.x, if it is installed too)
  BM, DB               bit mask of SDA in port B (4 = PB2, 1 = PB0) and 255-BM
See README.md in the main folder and the comment block at the top of the source for details.
