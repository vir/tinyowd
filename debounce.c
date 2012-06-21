#include <io.h>
#include <sleep.h>
#include <interrupt.h>

#define COUNT_TO 4

int8_t debounced_state;
/*
 * This function must be called every (20...50)/COUNT_TO mS
 * It uses vertical counters.
 * See http://www.dattalo.com/technical/software/pic/vertcnt.html
 * and http://www.dattalo.com/technical/software/pic/debounce.html
 */
int8_t debounce(int8_t newsample)
{
	int8_t delta, changes;
	static int8_t cA = 0;
	static int8_t cB = 0;
#if COUNT_TO > 4
	static int8_t cC = 0;
#endif
	delta = newsample ^ debounced_state; /* find changes */
	/* Increment counters */
#if COUNT_TO == 4
	cA ^= cB;
	cB = ~cB;
#elif COUNT_TO == 5
	/* A+ = A^B; B+ = A^C; C+ = ~(B|C) */
# if 0
	register int8_t w;
	w = cC ^ cA ^ cB;
	cB ^= w;
	w ^= cB;
	cA ^= w;
	cC |= w;
	cC = ~cC;
	/* above code is translated from microchip assembler - may be wrong */
# else
	/* Trying to figure out...
	 * A+ = A^B; B+ = A+^B^C; C+ = ~((A+^A)|C) = ~((A+^(B+^C)) | C) =
	 *  = ~((A+ ^ B+ ^ C) | C) */
	cA = cA ^ cB;
	cB = cA ^ cB ^cC;
	cC = ~((cA ^ cB ^ cC) | cC);
# endif
#elif COUNT_TO == 6
	/* A+ = A ^ (B&C); B+ = (B^C) | A&~C; C+ = ~C; */
	cB = cB ^ cC;        /* temp */
	cA = cA ^ (cB & cC); /* A+ = (B&C)^A */
	cC = ~cC;            /* C+ = ~C */
	cB |= cC & cA;       /* B+ = (B^C)|(A&~C) = temp|(C+ & A+) = A&~C */
#elif COUNT_TO == 7
	/* A+ = A ^ (B&C); B+ = B^C; C+ = ~C | (A&~B) */
	cB = cC ^ cB;           /* B+ = B^C */
	cA = cA ^ ((cB^cC)&cC); /* A+ = A^(B&C) = A^((B+^C)&C) */
	cC = ~C;
	cC |= cA & cB; /* C+ = ~C | (A&~B) =?= ~C | A&(B^C) = ~C | (A+ & B+) */
#elif COUNT_TO == 8
	/* A+ = A ^ (B&C); B+ = B^C; C+ = ~C */
	cA = cA ^ (cB & cC);
	cB = cB ^ cC;
	cC = ~cC;
#else
# error "Invalid COUNT_TO value"
#endif
	/* reset counters if no changes */
	cA &= delta;
	cB &= delta;
#if COUNT_TO > 4
	cC &= delta;
	changes = ~(~delta | cA | cB | cC);
#else
	changes = ~(~delta | cA | cB);
#endif
	debounced_state ^= changes;
	return changes;
}



