/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef STRING_BYTES_H
#define STRING_BYTES_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <tss2/tss2_esys.h>

#include "tpm2_session.h"

#if defined (__GNUC__)
#define COMPILER_ATTR(...) __attribute__((__VA_ARGS__))
#else
#define COMPILER_ATTR(...)
#endif

#define xstr(s) str(s)
#define str(s) #s

#define UNUSED(x) (void)x

#define ARRAY_LEN(x) (sizeof(x)/sizeof(x[0]))

#define PSTR(x) x ? x : "(null)"

#define BUFFER_SIZE(type, field) (sizeof((((type *)NULL)->field)))

#define TSS2_APP_RC_LAYER TSS2_RC_LAYER(5)

#define TPM2B_TYPE_INIT(type, field) { .size = BUFFER_SIZE(type, field), }
#define TPM2B_INIT(xsize) { .size = xsize, }
#define TPM2B_EMPTY_INIT TPM2B_INIT(0)
#define TPM2B_SENSITIVE_CREATE_EMPTY_INIT { \
           .sensitive = { \
                .data = {   \
                    .size = 0 \
                }, \
                .userAuth = {   \
                    .size = 0 \
                } \
            } \
    }

#define TPMT_TK_CREATION_EMPTY_INIT { \
        .tag = 0, \
		.hierarchy = 0, \
		.digest = TPM2B_EMPTY_INIT \
    }

#define TPML_PCR_SELECTION_EMPTY_INIT { \
        .count = 0, \
    } //ignore pcrSelections since count is 0.

#define TPMS_CAPABILITY_DATA_EMPTY_INIT { \
        .capability = 0, \
    } // ignore data since capability is 0.

#define TPMT_TK_HASHCHECK_EMPTY_INIT { \
		.tag = 0, \
		.hierarchy = 0, \
		.digest = TPM2B_EMPTY_INIT \
    }

#define TSS2L_SYS_AUTH_COMMAND_INIT(cnt, array) { \
        .count = cnt, \
        .auths = array, \
    }

/*
 * This macro is useful as a wrapper around SAPI functions to automatically
 * retry function calls when the RC is TPM2_RC_RETRY.
 */
#define TSS2_RETRY_EXP(expression)                         \
    ({                                                     \
        TSS2_RC __result = 0;                              \
        do {                                               \
            __result = (expression);                       \
        } while (tpm2_error_get(__result) == TPM2_RC_RETRY); \
        __result;                                          \
    })

typedef struct {
    UINT16 size;
    BYTE buffer[0];
} TPM2B;

int tpm2_util_hex_to_byte_structure(const char *inStr, UINT16 *byteLength, BYTE *byteBuffer);

/**
 * Pulls the TPM2B_DIGEST out of a TPM2B_ATTEST quote.
 * @param quoted
 *  The attestation quote structure.
^ * @param digest
^ *  The digest from the quote.
^ * @param extraData
^ *  The extraData from the quote.
 * @return
 *  True on success, false otherwise.
 */
bool tpm2_util_get_digest_from_quote(TPM2B_ATTEST *quoted, TPM2B_DIGEST *digest, TPM2B_DATA *extraData);

/**
 * Compares two digests to ensure they are equal (for validation).
 * @param quoteDigest
 *  The digest from the quote.
 * @param pcrDigest
 *  The digest calculated off-TMP from the PCRs.
 * @return
 *  True on success, false otherwise.
 */
bool tpm2_util_verify_digests(TPM2B_DIGEST *quoteDigest, TPM2B_DIGEST *pcrDigest);

/**
 * Appends a TPM2B buffer to a MAX buffer.
 * @param result
 *  The MAX buffer to append to
 * @param append
 *  The buffer to append to result.
 * @return
 *  true on success, false otherwise.
 */
bool tpm2_util_concat_buffer(TPM2B_MAX_BUFFER *result, TPM2B *append);

/**
 * Converts a numerical string into a uint32 value.
 * @param str
 *  The numerical string to convert.
 * @param value
 *  The value to store the conversion into.
 * @return
 *  true on success, false otherwise.
 */
bool tpm2_util_string_to_uint32(const char *str, uint32_t *value);

/**
 * Converts a numerical string into a uint16 value.
 * @param str
 *  The numerical string to convert.
 * @param value
 *  The value to store the conversion into.
 * @return
 *  true on success, false otherwise.
 */
bool tpm2_util_string_to_uint16(const char *str, uint16_t *value);

/**
 * Converts a numerical string into a uint8 value.
 * @param str
 *  The numerical string to convert.
 * @param value
 *  The value to store the conversion into.
 * @return
 *  true on success, false otherwise.
 */
bool tpm2_util_string_to_uint8(const char *str, uint8_t *value);

/**
 * Prints an xxd compatible hexdump to stdout if output is enabled,
 * ie no -Q option.
 *
 * @param data
 *  The data to print.
 * @param len
 *  The length of the data.
 */
void tpm2_util_hexdump(const BYTE *data, size_t len);

/**
 * Similar to tpm2_util_hexdump(), but:
 *   - does NOT respect the -Q option
 *   - allows specification of the output stream.
 * @param f
 *  The FILE output stream.
 * @param data
 *  The data to convert to hex.
 * @param len
 *  The length of the data.
 */
void tpm2_util_hexdump2(FILE *f, const BYTE *data, size_t len);

/**
 * Prints a file as a hex string to stdout if quiet mode
 * is not enabled.
 * ie no -Q option.
 *
 * @param fd
 *  A readable open file.
 * @param len
 *  The length of the data to read and print.
 * @return
 *  true if len bytes were successfully read and printed,
 *  false otherwise
 */
bool tpm2_util_hexdump_file(FILE *fd, size_t len);

/**
 * Prints a TPM2B as a hex dump respecting the -Q option
 * to stdout.
 *
 * @param buffer the TPM2B to print.
 */
#define tpm2_util_print_tpm2b(b) _tpm2_util_print_tpm2b((TPM2B *)b)
static inline void _tpm2_util_print_tpm2b(TPM2B *buffer) {

    return tpm2_util_hexdump(buffer->buffer, buffer->size);
}

/**
 * Prints a TPM2B as a hex dump to the FILE specified. Does NOT
 * respect -Q like tpm2_util_print_tpm2b().
 *
 * @param out
 *  The output FILE.
 * @param
 *   buffer the TPM2B to print.
 */
#define tpm2_util_print_tpm2b2(o, b) _tpm2_util_print_tpm2b2(o, (TPM2B *)b)
static inline void _tpm2_util_print_tpm2b2(FILE *out, TPM2B *buffer) {

    return tpm2_util_hexdump2(out, buffer->buffer, buffer->size);
}

/**
 * Determines if given PCR value is selected in TPMS_PCR_SELECTION structure.
 * @param pcr_selection the TPMS_PCR_SELECTION structure to check pcr against.
 * @param pcr the PCR ID to check selection status of.
 */
static inline bool tpm2_util_is_pcr_select_bit_set(TPMS_PCR_SELECTION *pcr_selection, UINT32 pcr) {
    return (pcr_selection->pcrSelect[((pcr) / 8)] & (1 << ((pcr) % 8)));
}

/**
 * Reads a TPM2B object from FILE* and prints data in hex.
 * @param fd
 *  A readable open file.
 */
bool tpm2_util_print_tpm2b_file(FILE *fd);

/**
 * Checks if the host is big endian
 * @return
 *  True of the host is big endian false otherwise.
 */
bool tpm2_util_is_big_endian(void);

/**
 * Swaps the endianess of 16 bit value.
 * @param data
 *  A 16 bit value to swap the endianess on.
 * @return
 * The 16 bit value with the endianess swapped.
 */
UINT16 tpm2_util_endian_swap_16(UINT16 data);

/**
 * Just like string_bytes_endian_convert_16 but for 32 bit values.
 */
UINT32 tpm2_util_endian_swap_32(UINT32 data);

/**
 * Just like string_bytes_endian_convert_16 but for 64 bit values.
 */
UINT64 tpm2_util_endian_swap_64(UINT64 data);

/**
 * Converts a 16 bit value from host endianess to network endianess.
 * @param data
 *  The data to possibly swap endianess.
 * @return
 *  The swapped data.
 */
UINT16 tpm2_util_hton_16(UINT16 data);

/**
 * Just like string_bytes_endian_hton_16 but for 32 bit values.
 */
UINT32 tpm2_util_hton_32(UINT32 data);

/**
 * Just like string_bytes_endian_hton_16 but for 64 bit values.
 */
UINT64 tpm2_util_hton_64(UINT64 data);

/**
 * Converts a 16 bit value from network endianess to host endianess.
 * @param data
 *  The data to possibly swap endianess.
 * @return
 *  The swapped data.
 */
UINT16 tpm2_util_ntoh_16(UINT16 data);

/**
 * Just like string_bytes_endian_ntoh_16 but for 32 bit values.
 */
UINT32 tpm2_util_ntoh_32(UINT32 data);

/**
 * Just like string_bytes_endian_ntoh_16 but for 64 bit values.
 */
UINT64 tpm2_util_ntoh_64(UINT64 data);

/**
 * Counts the number of set bits aka a population count.
 * @param data
 *  The data to count set bits in.
 * @return
 *  The number of set bits or population count.
 */
UINT32 tpm2_util_pop_count(UINT32 data);

/**
 * Prints whitespace indention for yaml output.
 * @param indent_count
 *  Number of times to indent
 */
void print_yaml_indent(size_t indent_count);

/**
 * Convert a TPM2B_PUBLIC into a yaml format and output if not quiet.
 * @param public
 *  The TPM2B_PUBLIC to output in YAML format.
 * @param indent
 *  The level of indentation, can be NULL
 */
void tpm2_util_public_to_yaml(TPM2B_PUBLIC *public, char *indent);


/**
 * Convert a TPMA_OBJECT to a yaml format and output if not quiet.
 * @param obj
 *  The TPMA_OBJECT attributes to print.
 * @param indent
 *  The level of indentation, can be NULL
 */
void tpm2_util_tpma_object_to_yaml(TPMA_OBJECT obj, char *indent);

/**
 * Calculates the unique public field. The unique public field is the digest, based on name algorithm
 * of the key + protection seed (concatenated).
 *
 * @param namealg
 *  The name algorithm of the object, from the public portion.
 * @param key
 *  The key bytes themselves. It seems odd that the type is TPM2B_PRIVATE_VENDOR_SPECIFIC
 *  but this for access to the ANY field.
 * @param seed
 *  The seed, from the sensitive portion.
 * @param unique
 *  The result, a generated unique value for the public portion.
 * @return
 *  True on success, false otherwise.
 */
bool tpm2_util_calc_unique(TPMI_ALG_HASH name_alg, TPM2B_PRIVATE_VENDOR_SPECIFIC *key,
        TPM2B_DIGEST *seed, TPM2B_DIGEST *unique);


/**
 * Uses TR_FromTPMPublic() to construct the ESYS_TR object corresponding
 * to the passed TPM2_HANDLE.
 * @param context
 *  an ESAPI context
 * @param sys_handle
 *  the TPM2_HANDLE to construct an ESYS_TR handle for
 * @param esys_handle
 *  pointer to an ESYS_TR handle to output the found handle into
 * @return
 *  A tool_rc indicating status.
 */
tool_rc tpm2_util_sys_handle_to_esys_handle(ESYS_CONTEXT *context,
        TPM2_HANDLE sys_handle, ESYS_TR *esys_handle);

/**
 * Get the underlying TPM2_HANDLE for the given ESYS_TR handle
 * @param context
 *  an ESAPI context
 * @param esys_handle
 *  the ESYS_TR for which a TPM handle is desired
 * @param sys_handle
 *  pointer to the TPM2_HANDLE in which to store the output
 * @return
 *  A tool_rc indicating status.
 */
tool_rc tpm2_util_esys_handle_to_sys_handle(ESYS_CONTEXT *context,
        ESYS_TR esys_handle, TPM2_HANDLE *sys_handle);

/**
 * Map a TPMI_RH_PROVISION to the corresponding ESYS_TR constant
 * @param inh
 *  The hierarchy to map
 */
ESYS_TR tpm2_tpmi_hierarchy_to_esys_tr(TPMI_RH_PROVISION inh);

char *tpm2_util_getenv(const char *name);

typedef enum tpm2_handle_flags tpm2_handle_flags;
enum tpm2_handle_flags {
    TPM2_HANDLE_FLAGS_NONE = 0,
    TPM2_HANDLE_FLAGS_O              = 1 << 0,
    TPM2_HANDLE_FLAGS_P              = 1 << 1,
    TPM2_HANDLE_FLAGS_E              = 1 << 2,
    TPM2_HANDLE_FLAGS_N              = 1 << 3,
    TPM2_HANDLE_FLAGS_L              = 1 << 4,
    TPM2_HANDLE_FLAGS_ALL_HIERACHIES = 0x1F,
    TPM2_HANDLES_FLAGS_TRANSIENT     = 1 << 5,
    TPM2_HANDLES_FLAGS_PERSISTENT    = 1 << 6,
    /* bits 7 and 8 are mutually exclusive */
    TPM2_HANDLE_FLAGS_NV             = 1 << 7,
    TPM2_HANDLE_ALL_W_NV             = 0xFF,
    TPM2_HANDLE_FLAGS_PCR            = 1 << 8,
    TPM2_HANDLE_ALL_W_PCR            = 0x17F,
};

/**
 * Converts an option from the command line into a valid TPM handle, checking
 * for errors and if the tool supports it based on flags settings.
 * @param value
 *  The command line value to convert.
 * @param handle
 *  The output handle.
 * @param flags
 *  The flags indicating what is supported by the tool.
 * @return
 *  true on success, false otherwise.
 */
bool tpm2_util_handle_from_optarg(const char *value,
        TPMI_RH_PROVISION *hierarchy, tpm2_handle_flags flags);

bool tpm2_util_get_label(const char *value, TPM2B_DATA *label);

#endif /* STRING_BYTES_H */
