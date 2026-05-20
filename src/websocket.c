#include "websocket.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rotate_left32(uint32_t value, unsigned int bits)
{
    return (value << bits) | (value >> (32u - bits));
}

static void sha1_transform(uint32_t state[5], const unsigned char block[64])
{
    uint32_t words[80];
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for (int i = 0; i < 16; i++)
    {
        words[i] = ((uint32_t)block[i * 4] << 24) |
                   ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) |
                   ((uint32_t)block[i * 4 + 3]);
    }

    for (int i = 16; i < 80; i++)
    {
        words[i] = rotate_left32(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1u);
    }

    for (int i = 0; i < 80; i++)
    {
        uint32_t f = 0u;
        uint32_t k = 0u;

        if (i < 20)
        {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }

        const uint32_t temp = rotate_left32(a, 5u) + f + e + k + words[i];
        e = d;
        d = c;
        c = rotate_left32(b, 30u);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void sha1_digest(const unsigned char *buffer, size_t length, unsigned char output[20])
{
    uint32_t state[5];
    unsigned char block[64];
    size_t remaining = length;
    size_t offset = 0u;
    uint64_t bitLength = (uint64_t)length * 8u;

    state[0] = 0x67452301u;
    state[1] = 0xEFCDAB89u;
    state[2] = 0x98BADCFEu;
    state[3] = 0x10325476u;
    state[4] = 0xC3D2E1F0u;

    while (remaining >= 64u)
    {
        memcpy(block, buffer + offset, 64u);
        sha1_transform(state, block);
        offset += 64u;
        remaining -= 64u;
    }

    memset(block, 0, sizeof(block));
    if (remaining > 0u)
    {
        memcpy(block, buffer + offset, remaining);
    }
    block[remaining] = 0x80u;

    if (remaining >= 56u)
    {
        sha1_transform(state, block);
        memset(block, 0, sizeof(block));
    }

    block[56] = (unsigned char)((bitLength >> 56) & 0xFFu);
    block[57] = (unsigned char)((bitLength >> 48) & 0xFFu);
    block[58] = (unsigned char)((bitLength >> 40) & 0xFFu);
    block[59] = (unsigned char)((bitLength >> 32) & 0xFFu);
    block[60] = (unsigned char)((bitLength >> 24) & 0xFFu);
    block[61] = (unsigned char)((bitLength >> 16) & 0xFFu);
    block[62] = (unsigned char)((bitLength >> 8) & 0xFFu);
    block[63] = (unsigned char)(bitLength & 0xFFu);
    sha1_transform(state, block);

    for (int i = 0; i < 5; i++)
    {
        output[i * 4] = (unsigned char)((state[i] >> 24) & 0xFFu);
        output[i * 4 + 1] = (unsigned char)((state[i] >> 16) & 0xFFu);
        output[i * 4 + 2] = (unsigned char)((state[i] >> 8) & 0xFFu);
        output[i * 4 + 3] = (unsigned char)(state[i] & 0xFFu);
    }
}

bool base64_encode(const unsigned char *input, size_t inputLength, char *output, size_t outputSize)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t outputLength = 0u;
    size_t index = 0u;

    if (input == NULL || output == NULL || outputSize == 0u)
    {
        return false;
    }

    while (index < inputLength)
    {
        const size_t remaining = inputLength - index;
        const unsigned int octetA = input[index++];
        const unsigned int octetB = remaining > 1u ? input[index++] : 0u;
        const unsigned int octetC = remaining > 2u ? input[index++] : 0u;
        const unsigned int triple = (octetA << 16u) | (octetB << 8u) | octetC;
        const int pad = remaining == 1u ? 2 : (remaining == 2u ? 1 : 0);

        if (outputLength + 4u >= outputSize)
        {
            return false;
        }

        output[outputLength++] = alphabet[(triple >> 18u) & 0x3Fu];
        output[outputLength++] = alphabet[(triple >> 12u) & 0x3Fu];
        output[outputLength++] = pad >= 2 ? '=' : alphabet[(triple >> 6u) & 0x3Fu];
        output[outputLength++] = pad >= 1 ? '=' : alphabet[triple & 0x3Fu];
    }

    if (outputLength >= outputSize)
    {
        return false;
    }

    output[outputLength] = '\0';
    return true;
}

bool websocket_compute_accept(const char *clientKey, char *acceptKey, size_t acceptKeySize)
{
    static const char websocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char combined[128];
    unsigned char digest[20];

    if (clientKey == NULL || acceptKey == NULL)
    {
        return false;
    }

    snprintf(combined, sizeof(combined), "%s%s", clientKey, websocketGuid);
    sha1_digest((const unsigned char *)combined, strlen(combined), digest);
    return base64_encode(digest, sizeof(digest), acceptKey, acceptKeySize);
}

bool websocket_generate_key(char *out, size_t outSize)
{
    unsigned char keyRaw[16];
    FILE *ur = fopen("/dev/urandom", "rb");
    if (ur != NULL)
    {
        fread(keyRaw, 1, sizeof(keyRaw), ur);
        fclose(ur);
    }
    else
    {
        for (size_t i = 0; i < sizeof(keyRaw); i++)
        {
            keyRaw[i] = (unsigned char)(rand() & 0xff);
        }
    }

    return base64_encode(keyRaw, sizeof(keyRaw), out, outSize);
}

bool websocket_accept_key(const char *clientKey, char *acceptKey, size_t acceptKeySize)
{
    return websocket_compute_accept(clientKey, acceptKey, acceptKeySize);
}
