#include "oryx.h"

void rc4_init(unsigned char *s, unsigned char *key, unsigned long Len)
{
    int i =0;
    int j =0;
    char k[256] = {0};
    unsigned char tmp = 0;
    for(i=0;i<256;i++)
    {
        s[i]=i;
        k[i]=key[i%Len];
    }
    for(i=0; i<256; i++)
    {
        j=(j+s[i]+k[i])%256;
        tmp = s[i];
        s[i] = s[j];
        s[j] = tmp;
    }
}

void rc4_crypt(unsigned char *s, unsigned char *input, unsigned long input_len)
{
    int i = 0;
    int j = 0;
    int t = 0;
    unsigned long k = 0;
    unsigned char tmp;
    for(k=0;k<input_len;k++)
    {
        i=(i+1)%256;
        j=(j+s[i])%256;
        tmp = s[i];
        s[i] = s[j];
        s[j] = tmp;
        t=(s[i]+s[j])%256;
        input[k] ^= s[t];
    }
}

