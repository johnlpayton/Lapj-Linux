

#include <stdlib.h>
#include <stdio.h>
/**********************************************************************
 *
 * Cursor library
 *
**********************************************************************/
#if VT102 || 1
void CU_goRC(int r, int c)              // x,y
{
    printf("\033[%d;%dH",r,c);
}
void CU_goUP(void)                      // Up one line same collumn
{
    printf("\033[A");
}
void CU_goDWN(void)                     // Down one line same collumn
{
    printf("\033[B");
}
void CU_goRGH(void)                     // Right one char same row
{
    printf("\033[C");
}
void CU_goLFT(void)                     // left one char same row
{
    printf("\033[D");
}
void CU_CLS(void)                       // clear screen and home
{
    printf("\033[2J");
}
void CU_KEOL(void)                       // kill to eol
{
    printf("\033[K");
}
void CU_KEOS(void)                       // kill to end of screen
{
    printf("\033[J");
}
void CU_HOME(void)                       // home
{
    printf("\033[1;1H");
}
void CU_CR(void)                       // home
{
    printf("\r");
}
void CU_LF(void)                       // home
{
    printf("\n");
}
void CU_getCUR(int *x, int *y)           // report cursor
{
    printf("\033[6n");
}
void CU_setFG(int c)                    // set color
{
    printf("\033[%dm",30+(7&c));
}
void CU_setBG(int c)                    // set color
{
    printf("\033[%dm",40+(7&c));
}
void CU_resetColors(void)                    // set color
{
    printf("\033[0m");
}
#endif

#if VTWIN32
int CUposX,CUposY;
#include <conio.h>
void CU_goRC(int r, int c)              // x,y
{
    gotoxy(c,r);
}
void CU_goUP(void)                      // Up one line same collumn
{
    int x,y;
    CU_getCUR(&x,&y);
    if(y>1) gotoxy(x,y-1);
}
void CU_goDWN(void)                     // Down one line same collumn
{
    int x,y;
    CU_getCUR(&x,&y);
    if(y<25) gotoxy(x,y+1);
}
void CU_goRGH(void)                     // Right one char same row
{
    int x,y;
    CU_getCUR(&x,&y);
    if(x<80) gotoxy(x+1,y);
}
void CU_goLFT(void)                     // left one char same row
{
    int x,y;
    CU_getCUR(&x,&y);
    if(x>0) gotoxy(x-1,y);
}
void CU_CLS(void)                       // clear screen and home
{
    clrscr();
}
void CU_KEOL(void)                       // kill to eol
{
    clreol();
}
void CU_HOME(void)                       // home
{
    gotoxy(1,1);
}
void CU_CR(void)                       // home
{
    putch('\r');
}
void CU_LF(void)                       // home
{
    putch('\n');
}
void CU_getCUR(int *x, int *y)           // report cursor
{
    *x = wherex();
    *y = wherey();
}
#endif
