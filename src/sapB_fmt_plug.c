/*
 * this is a SAP-BCODE plugin for john the ripper.
 * tested on linux/x86 only, rest is up to you.. at least, someone did the reversing :-)
 *
 * please note: this code is in a "works for me"-state, feel free to modify/speed up/clean/whatever it...
 *
 * (c) x7d8 sap loverz, public domain, btw
 * cheers: see test-cases.
 *
 * Heavily modified by magnum 2011 for performance and for
 * SIMD, OMP and encodings support. No rights reserved.
 */

#include <string.h>
#include <ctype.h>

#include "arch.h"
#include "misc.h"
#include "common.h"
#include "formats.h"
#include "unicode.h"

#include "md5.h"

#define FORMAT_LABEL			"sapb"
#define FORMAT_NAME			"SAP BCODE"

#ifdef MD5_SSE_PARA
#define MMX_COEF			4
#include "sse-intrinsics.h"
#define NBKEYS				(MMX_COEF * MD5_SSE_PARA)
#define DO_MMX_MD5(in, out)		SSEmd5body(in, (unsigned int*)out, 1)
#define ALGORITHM_NAME			"SSE2i " MD5_N_STR
#elif defined(MMX_COEF)
#define NBKEYS				MMX_COEF
#define DO_MMX_MD5(in, out)		mdfivemmx_nosizeupdate(out, (unsigned char*)in, 1)
#if MMX_COEF == 4
#define ALGORITHM_NAME			"SSE2 4x"
#elif MMX_COEF == 2
#define ALGORITHM_NAME			"MMX 2x"
#elif defined(MMX_COEF)
#define ALGORITHM_NAME			"?"
#endif
#else
#define ALGORITHM_NAME			"32/" ARCH_BITS_STR
#endif

#if defined(_OPENMP) && (defined (MD5_SSE_PARA) || !defined(MMX_COEF))
#include <omp.h>
static unsigned int omp_t = 1;
#ifdef MD5_SSE_PARA
#define OMP_SCALE			128
#else
#define OMP_SCALE			2048
#endif
#endif

#define BENCHMARK_COMMENT		""
#define BENCHMARK_LENGTH		0

#define SALT_FIELD_LENGTH		40	/* the max listed username length */
#define SALT_LENGTH			12	/* the max used username length */
#define PLAINTEXT_LENGTH		8	/* passwordlength max 8 chars */
#define CIPHERTEXT_LENGTH		SALT_FIELD_LENGTH + 1 + 16	/* SALT + $ + 2x8 bytes for BCODE-representation */

#define BINARY_SIZE			8	/* half of md5 */

#ifdef MMX_COEF
#define MIN_KEYS_PER_CRYPT		NBKEYS
#define MAX_KEYS_PER_CRYPT		NBKEYS
#define GETPOS(i, index)		( (index&(MMX_COEF-1))*4 + ((i)&(0xffffffff-3))*MMX_COEF + ((i)&3) + (index>>(MMX_COEF>>1))*16*MMX_COEF*4 )
#define GETOUTPOS(i, index)		( (index&(MMX_COEF-1))*4 + ((i)&(0xffffffff-3))*MMX_COEF + ((i)&3) + (index>>(MMX_COEF>>1))*16*MMX_COEF)
#else
#define MIN_KEYS_PER_CRYPT		1
#define MAX_KEYS_PER_CRYPT		1
#endif

/* char transition table for BCODE (from disp+work) */
#define TRANSTABLE_LENGTH 16*16
unsigned char transtable[TRANSTABLE_LENGTH]=
{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0x3F, 0x40, 0x41, 0x50, 0x43, 0x44, 0x45, 0x4B, 0x47, 0x48, 0x4D, 0x4E, 0x54, 0x51, 0x53, 0x46,
 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x56, 0x55, 0x5C, 0x49, 0x5D, 0x4A,
 0x42, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x58, 0x5B, 0x59, 0xFF, 0x52,
 0x4C, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x57, 0x5E, 0x5A, 0x4F, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

#define BCODE_ARRAY_LENGTH 3*16
unsigned char bcodeArr[BCODE_ARRAY_LENGTH]=
{0x14,0x77,0xF3,0xD4,0xBB,0x71,0x23,0xD0,0x03,0xFF,0x47,0x93,0x55,0xAA,0x66,0x91,
0xF2,0x88,0x6B,0x99,0xBF,0xCB,0x32,0x1A,0x19,0xD9,0xA7,0x82,0x22,0x49,0xA2,0x51,
0xE2,0xB7,0x33,0x71,0x8B,0x9F,0x5D,0x01,0x44,0x70,0xAE,0x11,0xEF,0x28,0xF0,0x0D};

// For backwards compatibility, we must support salts padded with spaces to a field width of 40
static struct fmt_tests tests[] = {
 	{"F           $E3A65AAA9676060F", "X"},
	{"JOHNNY                                  $7F7207932E4DE471", "CYBERPUNK"},
	{"VAN         $487A2A40A7BA2258", "HAUSER"},
	{"RoOT        $8366A4E9E6B72CB0", "KID"},
	{"MAN         $9F48E7CE5B184D2E", "U"},
	{"------------$2CF190AF13E858A2", "-------"},
	{"SAP*$7016BFF7C5472F1B", "MASTER"},
	{"DDIC$C94E2F7DD0178374", "DDIC"},
	{"dollar$$$---$C3413C498C48EB67", "DOLLAR$$$---"},
	{NULL}
};

#define TEMP_ARRAY_SIZE 4*16
#define DEFAULT_OFFSET 15

static char (*saved_plain)[PLAINTEXT_LENGTH + 1];
static int (*keyLen);

#ifdef MMX_COEF

static unsigned char (*saved_key);
static unsigned char (*interm_key);
static unsigned char (*crypt_key);
static unsigned int (*clean_pos);

#else

static ARCH_WORD_32 (*crypt_key)[BINARY_SIZE/sizeof(ARCH_WORD_32)];
static char (*saved_key)[PLAINTEXT_LENGTH + 1];

#endif

static struct saltstruct {
	unsigned int l;
	unsigned char s[SALT_LENGTH];
} *cur_salt;
#define SALT_SIZE			sizeof(struct saltstruct)

static void init(struct fmt_main *pFmt)
{
#if defined (_OPENMP) && (defined(MD5_SSE_PARA) || !defined(MMX_COEF))
	omp_t = omp_get_max_threads();
	pFmt->params.min_keys_per_crypt = omp_t * MIN_KEYS_PER_CRYPT;
	omp_t *= OMP_SCALE;
	pFmt->params.max_keys_per_crypt = omp_t * MAX_KEYS_PER_CRYPT;
#endif
#ifdef MMX_COEF
	saved_key = mem_calloc_tiny(64 * pFmt->params.max_keys_per_crypt, MEM_ALIGN_SIMD);
	interm_key = mem_calloc_tiny(64 * pFmt->params.max_keys_per_crypt, MEM_ALIGN_SIMD);
	crypt_key = mem_calloc_tiny(16 * pFmt->params.max_keys_per_crypt, MEM_ALIGN_SIMD);
	clean_pos = mem_calloc_tiny(sizeof(*clean_pos) * pFmt->params.max_keys_per_crypt, MEM_ALIGN_WORD);
#else
	saved_key = mem_calloc_tiny(sizeof(*saved_key) * pFmt->params.max_keys_per_crypt, MEM_ALIGN_NONE);
	crypt_key = mem_calloc_tiny(sizeof(*crypt_key) * pFmt->params.max_keys_per_crypt, MEM_ALIGN_WORD);
#endif
	saved_plain = mem_calloc_tiny(sizeof(*saved_plain) * pFmt->params.max_keys_per_crypt, MEM_ALIGN_NONE);
	keyLen = mem_calloc_tiny(sizeof(*keyLen) * pFmt->params.max_keys_per_crypt, MEM_ALIGN_WORD);
}

static int valid(char *ciphertext, struct fmt_main *pFmt)
{
	int i;
	char *p;

	if (!ciphertext) return 0;

	p = strrchr(ciphertext, '$');
	if (!p) return 0;

	if (strlen(&p[1]) != BINARY_SIZE * 2) return 0;

	p++;
	for (i = 0; i < BINARY_SIZE * 2; i++)
		if (!(((p[i]>='A' && p[i]<='F')) ||
			((p[i]>='a' && p[i]<='f')) ||
			((p[i]>='0' && p[i]<='9')) ))
			return 0;
	return 1;
}

static void set_salt(void *salt)
{
	cur_salt = salt;
}

static void set_key(char *key, int index)
{
	memcpy(saved_plain[index], key, PLAINTEXT_LENGTH);
	keyLen[index] = -1;
}

static char *get_key(int index) {
	saved_plain[index][PLAINTEXT_LENGTH] = 0;
	enc_strupper(saved_plain[index]);
	return saved_plain[index];
}

static int cmp_all(void *binary, int count) {
#ifdef MMX_COEF
	unsigned int x,y=0;
#ifdef MD5_SSE_PARA
#ifdef _OPENMP
	for(;y<MD5_SSE_PARA*omp_t;y++)
#else
	for(;y<MD5_SSE_PARA;y++)
#endif
#endif
		for(x = 0; x < MMX_COEF; x++)
		{
			if( ((ARCH_WORD_32*)binary)[0] == ((ARCH_WORD_32*)crypt_key)[y*MMX_COEF*4+x] )
				return 1;
		}
	return 0;
#else
	int index;
	for (index = 0; index < count; index++)
		if (!memcmp(binary, crypt_key[index], BINARY_SIZE))
			return 1;
	return 0;
#endif
}

static int cmp_exact(char *source, int index){
	return 1;
}

static int cmp_one(void * binary, int index)
{
#ifdef MMX_COEF
	unsigned int i,x,y;
	x = index&(MMX_COEF-1);
	y = index/MMX_COEF;
	for(i=0;i<(BINARY_SIZE/4);i++)
		if ( ((ARCH_WORD_32*)binary)[i] != ((ARCH_WORD_32*)crypt_key)[y*MMX_COEF*4+i*MMX_COEF+x] )
			return 0;
	return 1;
#else
	return !memcmp(binary, crypt_key[index], BINARY_SIZE);
#endif
}

static void crypt_all(int count) {
#if MMX_COEF
#if defined(_OPENMP) && (defined(MD5_SSE_PARA) || !defined(MMX_COEF))
	int t;
#pragma omp parallel for
	for (t = 0; t < omp_t; t++)
#define ti (t*NBKEYS+index)
#else
#define t  0
#define ti index
#endif
	{
		unsigned int index, i;

		for (index = 0; index < NBKEYS; index++) {
			int len;

			if ((len = keyLen[ti]) < 0) {
				int temp;
				len = 0;
				unsigned char *key = (unsigned char*)saved_plain[ti];
				while((temp = *key++) && len < PLAINTEXT_LENGTH) {
					temp = transtable[CP_up[temp]];
					if (temp & 0x80) temp = '^';
					saved_key[GETPOS(len, ti)] = temp;
					len++;
				}
				keyLen[ti] = len;
			}

			for (i = 0; i < cur_salt->l; i++)
				saved_key[GETPOS((len + i), ti)] = cur_salt->s[i];
			saved_key[GETPOS((len + i), ti)] = 0x80;
			((unsigned int *)saved_key)[14*MMX_COEF + (ti&3) + (ti>>2)*16*MMX_COEF] = (len + i) << 3;

			for (i = i + len + 1; i <= clean_pos[ti]; i++)
				saved_key[GETPOS(i, ti)] = 0;
			clean_pos[ti] = len + cur_salt->l;
		}

		DO_MMX_MD5(&saved_key[t*NBKEYS*64], &crypt_key[t*NBKEYS*16]);

#if MD5_SSE_PARA
		for (i = 0; i < MD5_SSE_PARA; i++)
			memset(&interm_key[t*64*NBKEYS+i*64*MMX_COEF+32*MMX_COEF], 0, 32*MMX_COEF);
#else
		memset(&interm_key[32*MMX_COEF], 0, 32*MMX_COEF);
#endif
		//now: walld0rf-magic [tm], (c), <g>
		for (index = 0; index < NBKEYS; index++) {
			unsigned int sum20, I1, I2, I3, I4, revI1;
			int len = keyLen[ti];

			//some magic in between....yes, #4 is ignored...
			//sum20 will be between 0x20 and 0x2F
			sum20 = crypt_key[GETOUTPOS(5, ti)]%4 + crypt_key[GETOUTPOS(3, ti)]%4 + crypt_key[GETOUTPOS(2, ti)]%4 + crypt_key[GETOUTPOS(1, ti)]%4 + crypt_key[GETOUTPOS(0, ti)]%4 + 0x20;

			I1 = 0;
			I2 = 0;
			revI1 = 0;
			I3 = 0;

			do {
				if (I1 < len) {
					if ((crypt_key[GETOUTPOS(DEFAULT_OFFSET - revI1, ti)] % 2) != 0) {
						interm_key[GETPOS(I2, ti)] = bcodeArr[BCODE_ARRAY_LENGTH - revI1 - 1];
						I2++;
					}
					interm_key[GETPOS(I2, ti)] = saved_key[GETPOS(I1, ti)];
					I2++;
					I1++;
					revI1++;
				}
				if (I3 < cur_salt->l) {
					interm_key[GETPOS(I2, ti)] = cur_salt->s[I3++];
					I2++;
				}

				I4 = I2 - I1 - I3;
				interm_key[GETPOS(I2, ti)] = bcodeArr[I4];
				I2++;
				interm_key[GETPOS(I2, ti)] = 0;
			} while (++I2 < sum20);

			interm_key[GETPOS(sum20, ti)] = 0x80;
			((unsigned int *)interm_key)[14*MMX_COEF + (ti&3) + (ti>>2)*16*MMX_COEF] = sum20 << 3;
		}

		DO_MMX_MD5(&interm_key[t*NBKEYS*64], &crypt_key[t*NBKEYS*16]);

		for (index = 0; index < NBKEYS; index++) {
			*(ARCH_WORD_32*)&crypt_key[GETOUTPOS(0, ti)] ^= *(ARCH_WORD_32*)&crypt_key[GETOUTPOS(8, ti)];
			*(ARCH_WORD_32*)&crypt_key[GETOUTPOS(4, ti)] ^= *(ARCH_WORD_32*)&crypt_key[GETOUTPOS(12, ti)];
		}
	}

#else

#ifdef _OPENMP
	int t;
#pragma omp parallel for
	for (t = 0; t < count; t++)
#else
#define t 0
#endif
	{
		unsigned char temp_key[BINARY_SIZE*2];
		unsigned char final_key[BINARY_SIZE*2];
		unsigned int i;
		unsigned int sum20;
		int I1, I2;
		int revI1;
		int I3;
		char destArray[TEMP_ARRAY_SIZE];
		int I4;
		MD5_CTX ctx;

		if (keyLen[t] < 0) {
			keyLen[t] = strlen(saved_plain[t]);

			if (keyLen[t] > PLAINTEXT_LENGTH)
				keyLen[t] = PLAINTEXT_LENGTH;

			for (i = 0; i < keyLen[t]; i++)
				saved_key[t][i] = transtable[CP_up[ARCH_INDEX(saved_plain[t][i])]];
			saved_key[t][i] = 0;
		}

		MD5_Init(&ctx);
		MD5_Update(&ctx, saved_key[t], keyLen[t]);
		MD5_Update(&ctx, cur_salt->s, cur_salt->l);
		MD5_Final(temp_key,&ctx);

		//some magic in between....yes, #4 is ignored...
		//sum20 will be between 0x20 and 0x2F
		sum20 = temp_key[5]%4 + temp_key[3]%4 + temp_key[2]%4 + temp_key[1]%4 + temp_key[0]%4 + 0x20;

		I1 = 0;
		I2 = 0;
		revI1 = 0;
		I3 = 0;

		//now: walld0rf-magic [tm], (c), <g>
		do {
			if (I1 < keyLen[t]) {
				if ((temp_key[DEFAULT_OFFSET + revI1] % 2) != 0)
					destArray[I2++] = bcodeArr[BCODE_ARRAY_LENGTH + revI1 - 1];
				destArray[I2++] = saved_key[t][I1++];
				revI1--;
			}
			if (I3 < cur_salt->l)
				destArray[I2++] = cur_salt->s[I3++];

			I4 = I2 - I1 - I3;
			I2++;
			destArray[I2-1] = bcodeArr[I4];
			destArray[I2++] = 0;
		} while (I2 < sum20);
		//end of walld0rf-magic [tm], (c), <g>

		MD5_Init(&ctx);
		MD5_Update(&ctx, destArray, sum20);
		MD5_Final(final_key, &ctx);

		for (i = 0; i < 8; i++)
			((char*)crypt_key[t])[i] = final_key[i + 8] ^ final_key[i];
	}
#endif
#undef t
#undef ti
}

static void *binary(char *ciphertext)
{
	static ARCH_WORD_32 binary[BINARY_SIZE / sizeof(ARCH_WORD_32)];
	char *realcipher = (char*)binary;
	int i;
	char* newCiphertextPointer;

	newCiphertextPointer = strrchr(ciphertext, '$') + 1;

	for(i=0;i<BINARY_SIZE;i++)
	{
		realcipher[i] = atoi16[ARCH_INDEX(newCiphertextPointer[i*2])]*16 + atoi16[ARCH_INDEX(newCiphertextPointer[i*2+1])];
	}
	return (void *)realcipher;
}

/*
 * Strip the padding spaces from salt. Usernames w/ spaces at the end are not
 * supported (SAP does not support them either)
 *
 * In sapB, we truncate salt length to 12 octets.
 *
 * ciphertext starts with salt
 */
static void *get_salt(char *ciphertext)
{
	int i;
	char *p;
	static struct saltstruct out;

	p = strrchr(ciphertext, '$') - 1;

	i = (int)(p - ciphertext);
	while (ciphertext[i] == ' ' || i >= SALT_LENGTH)
		i--;
	out.l = i + 1;

	// Salt is already uppercased in split()
	for (i = 0; i < out.l; ++i) {
		out.s[i] = transtable[ARCH_INDEX(ciphertext[i])];
		if (out.s[i] & 0x80) out.s[i] = '^';
	}
	return &out;
}

static char *split(char *ciphertext, int index)
{
	static char out[CIPHERTEXT_LENGTH + 1];
  	memset(out, 0, CIPHERTEXT_LENGTH + 1);
	strnzcpy(out, ciphertext, CIPHERTEXT_LENGTH + 1);
	enc_strupper(out); // username (==salt)
	return out;
}

static int binary_hash_0(void *binary) { return *(ARCH_WORD_32*)binary & 0xF; }
static int binary_hash_1(void *binary) { return *(ARCH_WORD_32*)binary & 0xFF; }
static int binary_hash_2(void *binary) { return *(ARCH_WORD_32*)binary & 0xFFF; }
static int binary_hash_3(void *binary) { return *(ARCH_WORD_32*)binary & 0xFFFF; }
static int binary_hash_4(void *binary) { return *(ARCH_WORD_32*)binary & 0xFFFFF; }
static int binary_hash_5(void *binary) { return *(ARCH_WORD_32*)binary & 0xFFFFFF; }
static int binary_hash_6(void *binary) { return *(ARCH_WORD_32*)binary & 0x7FFFFFF; }

#ifdef MMX_COEF
#define HASH_OFFSET (index&(MMX_COEF-1))+(index/MMX_COEF)*MMX_COEF*4
static int get_hash_0(int index) { return ((ARCH_WORD_32 *)crypt_key)[HASH_OFFSET] & 0xf; }
static int get_hash_1(int index) { return ((ARCH_WORD_32 *)crypt_key)[HASH_OFFSET] & 0xff; }
static int get_hash_2(int index) { return ((ARCH_WORD_32 *)crypt_key)[HASH_OFFSET] & 0xfff; }
static int get_hash_3(int index) { return ((ARCH_WORD_32 *)crypt_key)[HASH_OFFSET] & 0xffff; }
static int get_hash_4(int index) { return ((ARCH_WORD_32 *)crypt_key)[HASH_OFFSET] & 0xfffff; }
static int get_hash_5(int index) { return ((ARCH_WORD_32 *)crypt_key)[HASH_OFFSET] & 0xffffff; }
static int get_hash_6(int index) { return ((ARCH_WORD_32 *)crypt_key)[HASH_OFFSET] & 0x7ffffff; }
#else
static int get_hash_0(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0xF; }
static int get_hash_1(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0xFF; }
static int get_hash_2(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0xFFF; }
static int get_hash_3(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0xFFFF; }
static int get_hash_4(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0xFFFFF; }
static int get_hash_5(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0xFFFFFF; }
static int get_hash_6(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0x7FFFFFF; }
#endif

// Public domain hash function by DJ Bernstein
static int salt_hash(void *salt)
{
	struct saltstruct *s = (struct saltstruct*)salt;
	unsigned int hash = 5381;
	unsigned int i;

	for (i = 0; i < s->l; i++)
		hash = ((hash << 5) + hash) ^ s->s[i];

	return hash & (SALT_HASH_SIZE - 1);
}

struct fmt_main fmt_sapB = {
	{
		FORMAT_LABEL,
		FORMAT_NAME,
		ALGORITHM_NAME,
		BENCHMARK_COMMENT,
		BENCHMARK_LENGTH,
		PLAINTEXT_LENGTH,
		BINARY_SIZE,
		SALT_SIZE,
		MIN_KEYS_PER_CRYPT,
		MAX_KEYS_PER_CRYPT,
#if !defined(MMX_COEF) || defined(MD5_SSE_PARA)
		FMT_OMP |
#endif
		FMT_8_BIT | FMT_SPLIT_UNIFIES_CASE,
		tests
	}, {
		init,
		fmt_default_prepare,
		valid,
		split,
		binary,
		get_salt,
		{
			binary_hash_0,
			binary_hash_1,
			binary_hash_2,
			binary_hash_3,
			binary_hash_4,
			binary_hash_5,
			binary_hash_6
		},
		salt_hash,
		set_salt,
		set_key,
		get_key,
		fmt_default_clear_keys,
		crypt_all,
		{
			get_hash_0,
			get_hash_1,
			get_hash_2,
			get_hash_3,
			get_hash_4,
			get_hash_5,
			get_hash_6
		},
		cmp_all,
		cmp_one,
		cmp_exact
	}
};
