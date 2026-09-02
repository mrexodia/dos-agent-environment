/*
 * dosrand.c -- DJGPP entropy for wolfSSL (CUSTOM_RAND_GENERATE_SEED).
 *
 * DOS has no /dev/urandom. Mix the Pentium time-stamp counter, the BIOS
 * tick counter at 0x40:0x6C, and clock() into a xorshift state. Under
 * emulation the TSC jitter provides practical entropy; this is a museum
 * build, not a HSM.
 */
#include <time.h>
#include <go32.h>

static unsigned long st = 0;

static void stir(void)
{
	unsigned long long tsc;
	unsigned long ticks, c;
	__asm__ __volatile__("rdtsc" : "=A"(tsc));
	ticks = _farpeekl(_dos_ds, 0x46C);
	c = (unsigned long)clock();
	st ^= (unsigned long)tsc ^ (tsc >> 32) ^ ticks ^ (c << 7) ^ 0x9E3779B9u;
	if (!st)
		st = 0xA5A5A5A5u;
}

int dos_rand_seed(unsigned char *output, unsigned int sz)
{
	unsigned int i;
	for (i = 0; i < sz; i++) {
		if ((i & 15) == 0)
			stir();
		/* xorshift32 */
		st ^= st << 13;
		st ^= st >> 17;
		st ^= st << 5;
		output[i] = (unsigned char)(st >> 24);
	}
	return 0;
}
