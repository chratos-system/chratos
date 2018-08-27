#ifndef XRB_INTERFACE_H
#define XRB_INTERFACE_H

#if __cplusplus
extern "C" {
#endif

typedef unsigned char * chr_uint128; // 16byte array for public and private keys
typedef unsigned char * chr_uint256; // 32byte array for public and private keys
typedef unsigned char * chr_uint512; // 64byte array for signatures
typedef void * chr_transaction;

// Convert amount bytes 'source' to a 39 byte not-null-terminated decimal string 'destination'
void chr_uint128_to_dec (const chr_uint128 source, char * destination);
// Convert public/private key bytes 'source' to a 64 byte not-null-terminated hex string 'destination'
void chr_uint256_to_string (const chr_uint256 source, char * destination);
// Convert public key bytes 'source' to a 65 byte non-null-terminated account string 'destination'
void chr_uint256_to_address (chr_uint256 source, char * destination);
// Convert public/private key bytes 'source' to a 128 byte not-null-terminated hex string 'destination'
void chr_uint512_to_string (const chr_uint512 source, char * destination);

// Convert 39 byte decimal string 'source' to a byte array 'destination'
// Return 0 on success, nonzero on error
int chr_uint128_from_dec (const char * source, chr_uint128 destination);
// Convert 64 byte hex string 'source' to a byte array 'destination'
// Return 0 on success, nonzero on error
int chr_uint256_from_string (const char * source, chr_uint256 destination);
// Convert 128 byte hex string 'source' to a byte array 'destination'
// Return 0 on success, nonzero on error
int chr_uint512_from_string (const char * source, chr_uint512 destination);

// Check if the null-terminated string 'account' is a valid chr account number
// Return 0 on correct, nonzero on invalid
int chr_valid_address (const char * account);

// Create a new random number in to 'destination'
void chr_generate_random (chr_uint256 destination);
// Retrieve the deterministic private key for 'seed' at 'index'
void chr_seed_key (const chr_uint256 seed, int index, chr_uint256);
// Derive the public key 'pub' from 'key'
void chr_key_account (chr_uint256 key, chr_uint256 pub);

// Sign 'transaction' using 'private_key' and write to 'signature'
char * chr_sign_transaction (const char * transaction, const chr_uint256 private_key);
// Generate work for 'transaction'
char * chr_work_transaction (const char * transaction);

#if __cplusplus
} // extern "C"
#endif

#endif // XRB_INTERFACE_H
