#include <stdio.h>
#include <string.h>

#ifndef from
#define from	at2
#endif

#ifndef to
#define to	code
#endif

#include "scancodes.h"

int main(void)
{

	int i, j;
	unsigned int keycode[4096];

	memset(keycode, 0, 4096 * sizeof(int));

	for (i = 0; scancodes[i].code; i++) {
		if (scancodes[i].from && scancodes[i].to) {
			if (!keycode[scancodes[i].from])
				keycode[scancodes[i].from] = scancodes[i].to;
			else
				printf("Clash: %3d and %3d on 0x%02x\n",
					scancodes[i].to, keycode[scancodes[i].from], scancodes[i].from);
		}
	}

	for (j = 4095; j > 0; j--)
		if (keycode[j]) break;

	for (i = 0; i <= j; i++) {
		if (keycode[i])
			printf("%3d,", keycode[i]);
		else
			printf("  0,");
		if ((i & 0xf) == 0xf)
			printf("\n");
	}
#if 0
	for (i = 0; i <= j; i++) {
		if (!keycode[i])
			printf("%03x ", i);
		else
			printf("    ");
		if ((i & 0xf) == 0xf)
			printf("\n");
	}
#endif

	printf("\n");
	return 0;
}
