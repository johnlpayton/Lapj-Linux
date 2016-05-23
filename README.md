# Lapj-Linux
Lapj ECC host Linux
Source code for Linux based host using Lapj error correction over a serial port.
The ECC method is used to detect when a frame is corrupted and retransmit.  The
aglorithm is a sliding window (11 frames), Go-Back-N.  It is needed when
the client (or host) drops characters (or otherwise mangles then).

It uses character framing of the form 
  <SF><Control> ... <CRC><EF>
Transparency uses character stuffing. to escape characters inside a frame.
A callback routine is optionally called to detect characters outside a frame.  This is primarily used
when development of software on a target hits a breakpoint or an exception and stops
executing with an error message.

The framing uses single characters for the start (<SF> default 0xae) and end (<EF> default 0xab>).
The escape character (<ES> default 0xad) sends a two character sequence for each character to be escaped.
The values are set at initialization.

The CRC  use the polynomial 0xA6 recommened by (Koopman) with a very efficient table
look up algorithm (2 tables of 16 bytes with two lookup steps per character).

 
Philip Koopman, Tridib Chakravarty, "Cyclic Redundancy Code (CRC) Polynomial Selection For Embedded Networks"
Preprint: The International Conference on Dependable Systems and Networks, DSN-2004
