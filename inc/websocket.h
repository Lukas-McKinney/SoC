#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stddef.h>
#include <stdbool.h>

bool base64_encode(const unsigned char *input, size_t inputLength, char *output, size_t outputSize);
void sha1_digest(const unsigned char *buffer, size_t length, unsigned char output[20]);
bool websocket_compute_accept(const char *clientKey, char *acceptKey, size_t acceptKeySize);
bool websocket_generate_key(char *out, size_t outSize);
bool websocket_accept_key(const char *clientKey, char *acceptKey, size_t acceptKeySize);

#endif
