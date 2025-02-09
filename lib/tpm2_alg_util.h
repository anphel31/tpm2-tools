/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef LIB_TPM2_ALG_UTIL_H_
#define LIB_TPM2_ALG_UTIL_H_

#include <stdbool.h>

#include <tss2/tss2_esys.h>

#include "tool_rc.h"

typedef enum tpm2_alg_util_flags tpm2_alg_util_flags;
enum tpm2_alg_util_flags {
    tpm2_alg_util_flags_none        = 0,
    tpm2_alg_util_flags_hash        = 1 << 0,
    tpm2_alg_util_flags_keyedhash   = 1 << 0,
    tpm2_alg_util_flags_symmetric   = 1 << 2,
    tpm2_alg_util_flags_asymmetric  = 1 << 3,
    tpm2_alg_util_flags_kdf         = 1 << 4,
    tpm2_alg_util_flags_mgf         = 1 << 5,
    tpm2_alg_util_flags_sig         = 1 << 6,
    tpm2_alg_util_flags_mode        = 1 << 7,
    tpm2_alg_util_flags_base        = 1 << 8,
    tpm2_alg_util_flags_misc        = 1 << 9,
    tpm2_alg_util_flags_enc_scheme  = 1 << 10,
    tpm2_alg_util_flags_rsa_scheme  = 1 << 11,
    tpm2_alg_util_flags_any         = ~0
};

/**
 * Convert a "nice-name" string to an algorithm id.
 * @param name
 *  The "nice-name" to convert.
 * @return
 *  TPM2_ALG_ERROR on error, or a valid algorithm identifier.
 */
TPM2_ALG_ID tpm2_alg_util_strtoalg(const char *name, tpm2_alg_util_flags flags);

/**
 * Convert an id to a nice-name.
 * @param id
 *  The id to convert.
 * @return
 *  The nice-name.
 */
const char *tpm2_alg_util_algtostr(TPM2_ALG_ID id, tpm2_alg_util_flags flags);

/**
 * XXX DOC AND TESTME
 * @param id
 * @return
 */
tpm2_alg_util_flags tpm2_alg_util_algtoflags(TPM2_ALG_ID id);

/**
 * Converts either a string from algorithm number or algorithm nice-name to
 * an algorithm id.
 * @param optarg
 *  The string to convert from an algorithm number or nice name.
 * @return
 *  TPM2_ALG_ERROR on error or the algorithm id.
 */
TPM2_ALG_ID tpm2_alg_util_from_optarg(const char *optarg, tpm2_alg_util_flags flags);

/**
 * Contains the information from parsing an argv style vector of strings for
 * pcr digest language specifications.
 */
typedef struct tpm2_pcr_digest_spec tpm2_pcr_digest_spec;
struct tpm2_pcr_digest_spec {
    TPML_DIGEST_VALUES digests;
    TPMI_DH_PCR pcr_index;
};

/**
 * Parses an argv array that contains a digest specification at each location
 * within argv.
 *
 * The digest specification is as follows:
 *   - A pcr identifier as understood by strtoul with 0 as the base.
 *   - A colon followed by the algorithm hash specification.
 *   - The algorithm hash specification is as follows:
 *       - The algorithm friendly name or raw numerical as understood by
 *         strtoul with a base of 0.
 *       - An equals sign
 *       - The hex hash value,
 *
 *   This all distills to a string that looks like this:
 *   <pcr index>:<hash alg id>=<hash value>
 *
 *   Example:
 *   "4:sha=f1d2d2f924e986ac86fdf7b36c94bcdf32beec15"
 *
 *   Note:
 *   Multiple specifications of PCR and hash are OK. Multiple hashes
 *   cause the pcr to be extended with both hashes. Multiple same PCR
 *   values cause the PCR to be extended multiple times. Extension
 *   is done in order from left to right as specified.
 *
 *   At most 5 hash extensions per PCR entry are supported. This
 *   is to keep the parser simple.
 *
 * @param argv
 *  The argv of digest specifications to parse.
 * @param len
 *  The number of digest specifications to parse.
 * @param digests
 *  An array of tpm2_pcr_digest_spec big enough to hold len items.
 * @return
 *  True if parsing was successful, False otherwise.
 *  @note
 *  This function logs errors via LOG_ERR.
 */
bool pcr_parse_digest_list(char **argv, int len,
        tpm2_pcr_digest_spec *digest_spec);

/**
 * Retrieves the size of a hash in bytes for a given hash
 * algorithm or 0 if unknown/not found.
 * @param id
 *  The HASH algorithm identifier.
 * @return
 *  0 on failure or the size of the hash bytes.
 */
UINT16 tpm2_alg_util_get_hash_size(TPMI_ALG_HASH id);

/**
 * Retrieves an appropriate signature scheme (scheme) signable by
 * specified key (keyHandle) and hash algorithm (halg).
 * @param context
 *  Enhanced System API (ESAPI) context for tpm
 * @param keyHandle
 *  Handle to key used in signing operation
 * @param halg
 *  Hash algorithm for message
 * @param sig_scheme
 *  Signature scheme (optional, use TPM2_ALG_NULL for default)
 * @param scheme
 *  Signature scheme output
 * @return
 *  tool_rc indicating status.
 *  On error scheme is left unmodified.
 */
tool_rc tpm2_alg_util_get_signature_scheme(ESYS_CONTEXT *context,
        ESYS_TR keyHandle, TPMI_ALG_HASH halg,
        TPMI_ALG_SIG_SCHEME sig_scheme,
        TPMT_SIG_SCHEME *scheme);

/**
 *
 * @param alg_spec
 *  Friendly specification of public algorithm set (algname:...:....)
 * @param public
 *  Public structure which will contain relevant information about
 *  specified algorithm
 * @pre public is caller allocated and must not be NULL
 * @return
 */
bool tpm2_alg_util_handle_ext_alg(const char *alg_spec, TPM2B_PUBLIC *public);

/**
 *
 * @param alg_details
 * @param name_halg
 * @param attrs
 * @param auth_policy
 * @param def_attrs
 * @param is_sealing
 * @param public
 * @return
 */
bool tpm2_alg_util_public_init(char *alg_details, char *name_halg, char *attrs, char *auth_policy, char *unique_file,
        TPMA_OBJECT def_attrs, TPM2B_PUBLIC *public);

/**
 * Returns an ECC curve as a friendly name.
 * @param curve_id
 *  The curve to look up a friendly string for.
 * @return
 *  The friendly string or NULL if not found.
 */
const char *tpm2_alg_util_ecc_to_str(TPM2_ECC_CURVE curve_id);

/**
 * Determines if a size is a valid AES key size.
 * @param size_in_bytes
 *  The size of a potential AES key in bytes.
 * @return
 *  true if valid, false otherwise.
 */
bool tpm2_alg_util_is_aes_size_valid(UINT16 size_in_bytes);

#endif /* LIB_TPM2_ALG_UTIL_H_ */
