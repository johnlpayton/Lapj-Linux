/*
*=====================================================================================
* SHELL PROGRAM FOR TEST OF LAPJ
*
* This set of software isolates Lapj from the main build of mkevbecc so ti can
* be tested then the same modules used for a new mxvsp.  Mkecbecc has to contain"
*
*   PROVIDE(DoLapj = 0x7_0000);
*  in the ld file
*  and cross link to
*  ..\\Lapj\\
*  for the source and include files
*
* The BCC makefile also has to be modified to reference the directory
*
* An assembly instruction
*   jmp     DoLapj
* is located at 0x7_0000
* This program houses the subroutines that are needed.  It should not be executed in
* stand alone mode.  Eventually, we aould like to make a *.a library and an appropiate BCC
* library (perhaps a DLL) but for now, static cross linking of source is used.
*
*Note the k_enqueue and k_dequeue primatives are housed in the main program
*I'm not sure how the linking is done but the sources are used so these snippits
*are mainly for cut/paste into msvsp because mkevbecc has kernel support.
*BlockCRC8 is also for cut/paste
*SysTimer will be depreciated.  Lapj will count down from internal timers. The
*tic rate needs to be 100ms but the initialization routine can overwrite the
*T4xx constants
*
*=====================================================================================
*/
// on a linux system supply missing calls

#ifndef _LINUX
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "kernel.h"
#include "kuart5235.h"
#include "k_iproto.h"
#include "jsk_buff.h"
#include "lapj.h"

int SysTimer= 0;

int main()
{

    printf("Hello world\r\n");

    exit(1);                            // This is a jump to reset
}
#else
#include <stdio.h>
#include <stdlib.h>
#include "lxtype.h"
#include "jsk_buff.h"
#include "lapj.h"
#endif

/*
* enqueue at the end. This is Before  the header q
*/
t_DQue* k_enqueue(t_DQue* q, t_DQue* elem)
{
    //======== Illegal call
    if(!q || !elem)
        return NULL;

    elem->next = q;
    elem->prev = q->prev;
    //======== add protection here
    q->prev->next = elem;
    q->prev = elem;
    //========
    return elem;
}

/*
* deque an element Call using k_dequeue(Hdr->prev) to get a fifo
*/
t_DQue* k_dequeue(t_DQue* elem)
{
    //======== check for empty
    if(elem->next == elem->prev)
        return(NULL);

    //======== add protection here
    elem->prev->next = elem->next;
    elem->next->prev = elem->prev;
    //========
    elem->next = elem;
    elem->prev = elem;
    return elem;
}

/*
**************************************************************************
*
*  FUNCTION: U8 BlockCRC8(U8* src, int n, U8* pCRC8)
*
*  PURPOSE: Compute a crc block
*
*  ARGUMENTS:
*   src:    block pointer
*   n:      byte count
*   pCRC8   pointer crc storage location
*
*  RETURN:
*   Value of the CRC after computation
*
*  COMMENTS:
*   The CRC is computed over a block.   The user can supply
*   a U8 pointer to an existing CRC.  If the pointer is NULL,
*   a temporary location is initialized to 0xFF and used.
*
*   The user must keep the CRC memory byte if he wants to span
*   over multiple calls.
*
*   Note, if a valid CRC has been appended to the block and
*   the composite block run through a CRC, the result is 0.
*   This is a mathematical relationship independant of the
*   initialization value.  Some implementations invert the CRC
*   before it is sent.  The value is dependant on the polynomial.
*   I don't know why people do that. Probably they don't understand
*   the underlying mathematics.
*
*   The tables use the polynomial 0xA6 recommened by Koopman
*
* Cyclic Redundancy Code (CRC) Polynomial Selection For Embedded Networks
* Philip Koopman, Tridib Chakravarty
* Preprint: The International Conference on Dependable Systems and Networks, DSN-2004
*
*   Note, the LSB corresponds to x^8 because serial conversion shifts the
*   lsb out and the crc should be shifted with x^8 first.  The 0xA6 notation
*   is in conventional math format x^8, x^7, x^5 .. x^1. Can cause some
*   confusion to readers but if the polynomial definition is bit
*   reversed then shifts are to the right for single but stuff things work out.
*   A sevond note is that CRCs are linear computations.
*   Let crcByte(n) be the result of a crc run on a byte (8 iterations)
*   then form a byte <aaaabbbb>
*   crcByte(<aaaabbbb>)
*   = crcbyte(<aaaa0000> + <0000bbbb>)
*   = crcbyte(<aaaa0000>) + crcbyte(<0000bbbb>)
*   where all adds are bitwise
*
**************************************************************************
*/
static U8 CRC8TableFL[16]={
0x00, 0x6b, 0xd6, 0xbd, 0x67, 0x0c, 0xb1, 0xda,
0xce, 0xa5, 0x18, 0x73, 0xa9, 0xc2, 0x7f, 0x14
};
static U8 CRC8TableFH[16]={
0x00, 0x57, 0xae, 0xf9, 0x97, 0xc0, 0x39, 0x6e,
0xe5, 0xb2, 0x4b, 0x1c, 0x72, 0x25, 0xdc, 0x8b
};
U8 BlockCRC8(U8* src, int n, U8* pCRC8)
{
    int k;
    U8  t;
    U8  tmpcrc;

    if(!pCRC8)                                  // create a variable if we are not passed one
    {
        pCRC8 = &tmpcrc;
        *pCRC8 = 0xff;
    }

    for(k=0; k<n; k++)
    {
        t=*pCRC8^src[k];                        // add the input
        *pCRC8 = CRC8TableFL[t & 0xF] ^         //   crc(0000nnnn)
                 CRC8TableFH[(t >>4) & 0xF];    // + crc(mmmm0000)
    }
    return(*pCRC8);
}
