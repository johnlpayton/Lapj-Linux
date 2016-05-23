#ifndef LXTYPE_H
#define LXTYPE_H 1

typedef unsigned char byte;
typedef unsigned short ushort16;
typedef char                S8;
typedef short               S16;
typedef int                 S32;
typedef long long             S64;
typedef char                I8;
typedef short               I16;
typedef int                 I32;
typedef long long             I64;
typedef unsigned char       U8;
typedef unsigned short      U16;
typedef unsigned int        U32;
typedef unsigned long long    U64;
#ifndef intptr_t
#define intptr_t int
#endif

    typedef struct S_jskdqueue
    {
        struct S_jskdqueue* next;                      // next
        struct S_jskdqueue* prev;                      // previous
        int   val;                            // payload pointer
    }t_jskdqueue;
    typedef struct S_jskdqueue t_DQue;

#endif