/* SPDX-License-Identifier: BSD-3-Clause */

#include <ctype.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "log.h"
#include "tool_rc.h"
#include "tpm2.h"
#include "tpm2_alg_util.h"
#include "tpm2_attr_util.h"
#include "tpm2_openssl.h"
#include "tpm2_tool.h"
#include "tpm2_util.h"

bool tpm2_util_get_digest_from_quote(TPM2B_ATTEST *quoted, TPM2B_DIGEST *digest, TPM2B_DATA *extraData) {
    TPM2_GENERATED magic;
    TPMI_ST_ATTEST type;
    UINT16 nameSize = 0;
    UINT32 i = 0;

    // Ensure required headers are at least there
    if (quoted->size < 6) {
        LOG_ERR("Malformed TPM2B_ATTEST headers");
        return false;
    }

    memcpy(&magic, &quoted->attestationData[i], 4);i += 4;
    memcpy(&type, &quoted->attestationData[i], 2);i += 2;
    if (!tpm2_util_is_big_endian()) {
        magic = tpm2_util_endian_swap_32(magic);
        type = tpm2_util_endian_swap_16(type);
    }

    if (magic != TPM2_GENERATED_VALUE) {
        LOG_ERR("Malformed TPM2_GENERATED magic value");
        return false;
    }

    if (type != TPM2_ST_ATTEST_QUOTE) {
        LOG_ERR("Malformed TPMI_ST_ATTEST quote value");
        return false;
    }

    // Qualified signer name (skip)
    if (i+2 >= quoted->size) {
        LOG_ERR("Malformed TPM2B_NAME value");
        return false;
    }
    memcpy(&nameSize, &quoted->attestationData[i], 2);i += 2;
    if (!tpm2_util_is_big_endian()) {
        nameSize = tpm2_util_endian_swap_16(nameSize);
    }
    i += nameSize;

    // Extra data (skip)
    if (i+2 >= quoted->size) {
        LOG_ERR("Malformed TPM2B_DATA value");
        return false;
    }
    memcpy(&extraData->size, &quoted->attestationData[i], 2);i += 2;
    if (!tpm2_util_is_big_endian()) {
        extraData->size = tpm2_util_endian_swap_16(extraData->size);
    }
    if (extraData->size+i > quoted->size) {
        LOG_ERR("Malformed extraData TPM2B_DATA value");
        return false;
    }
    memcpy(&extraData->buffer, &quoted->attestationData[i], extraData->size);i += extraData->size;

    // Clock info (skip)
    i += 17;
    if (i >= quoted->size) {
        LOG_ERR("Malformed TPMS_CLOCK_INFO value");
        return false;
    }

    // Firmware info (skip)
    i += 8;
    if (i >= quoted->size) {
        LOG_ERR("Malformed firmware version value");
        return false;
    }

    // PCR select info
    UINT8 sos;
    TPMI_ALG_HASH hashAlg;
    UINT32 pcrSelCount = 0, j = 0;
    if (i+4 >= quoted->size) {
        LOG_ERR("Malformed TPML_PCR_SELECTION value");
        return false;
    }
    memcpy(&pcrSelCount, &quoted->attestationData[i], 4);i += 4;
    if (!tpm2_util_is_big_endian()) {
        pcrSelCount = tpm2_util_endian_swap_32(pcrSelCount);
    }
    for (j = 0; j < pcrSelCount; j++) {
        // Hash
        if (i+2 >= quoted->size) {
            LOG_ERR("Malformed TPMS_PCR_SELECTION value");
            return false;
        }
        memcpy(&hashAlg, &quoted->attestationData[i], 2);i += 2;
        if (!tpm2_util_is_big_endian()) {
            hashAlg = tpm2_util_endian_swap_16(hashAlg);
        }

        // SizeOfSelected
        if (i+1 >= quoted->size) {
            LOG_ERR("Malformed TPMS_PCR_SELECTION value");
            return false;
        }
        memcpy(&sos, &quoted->attestationData[i], 1);i += 1;

        // PCR Select (skip)
        i += sos;
        if (i >= quoted->size) {
            LOG_ERR("Malformed TPMS_PCR_SELECTION value");
            return false;
        }
    }

    // Digest
    if (i+2 >= quoted->size) {
        LOG_ERR("Malformed TPM2B_DIGEST value");
        return false;
    }
    memcpy(&digest->size, &quoted->attestationData[i], 2);i += 2;
    if (!tpm2_util_is_big_endian()) {
        digest->size = tpm2_util_endian_swap_16(digest->size);
    }

    if (digest->size+i > quoted->size) {
        LOG_ERR("Malformed TPM2B_DIGEST value");
        return false;
    }
    memcpy(&digest->buffer, &quoted->attestationData[i], digest->size);

    return true;
}

// verify that the quote digest equals the digest we calculated
bool tpm2_util_verify_digests(TPM2B_DIGEST *quoteDigest, TPM2B_DIGEST *pcrDigest) {

    // Sanity check -- they should at least be same size!
    if (quoteDigest->size != pcrDigest->size) {
        LOG_ERR("FATAL ERROR: PCR values failed to match quote's digest!");
        return false;
    }

    // Compare running digest with quote's digest
    int k;
    for (k = 0; k < quoteDigest->size; k++) {
        if (quoteDigest->buffer[k] != pcrDigest->buffer[k]) {
            LOG_ERR("FATAL ERROR: PCR values failed to match quote's digest!");
            return false;
        }
    }

    return true;
}

bool tpm2_util_concat_buffer(TPM2B_MAX_BUFFER *result, TPM2B *append) {

    if (!result || !append) {
        return false;
    }

    if ((result->size + append->size) < result->size) {
        return false;
    }

    if ((result->size + append->size) > TPM2_MAX_DIGEST_BUFFER) {
        return false;
    }

    memcpy(&result->buffer[result->size], append->buffer, append->size);
    result->size += append->size;

    return true;
}

bool tpm2_util_string_to_uint8(const char *str, uint8_t *value) {

    uint32_t tmp;
    bool result = tpm2_util_string_to_uint32(str, &tmp);
    if (!result) {
        return false;
    }

    /* overflow on 8 bits? */
    if (tmp > UINT8_MAX) {
        return false;
    }

    *value = (uint8_t) tmp;
    return true;
}

bool tpm2_util_string_to_uint16(const char *str, uint16_t *value) {

    uint32_t tmp;
    bool result = tpm2_util_string_to_uint32(str, &tmp);
    if (!result) {
        return false;
    }

    /* overflow on 16 bits? */
    if (tmp > UINT16_MAX) {
        return false;
    }

    *value = (uint16_t) tmp;
    return true;
}

bool tpm2_util_string_to_uint32(const char *str, uint32_t *value) {

    char *endptr;

    if (str == NULL || *str == '\0') {
        return false;
    }

    /* clear errno before the call, should be 0 afterwards */
    errno = 0;
    unsigned long int tmp = strtoul(str, &endptr, 0);
    if (errno || tmp > UINT32_MAX) {
        return false;
    }

    /*
     * The entire string should be able to be converted or fail
     * We already checked that str starts with a null byte, so no
     * need to check that again per the man page.
     */
    if (*endptr != '\0') {
        return false;
    }

    *value = (uint32_t) tmp;
    return true;
}

int tpm2_util_hex_to_byte_structure(const char *inStr, UINT16 *byteLength,
        BYTE *byteBuffer) {
    int strLength; //if the inStr likes "1a2b...", no prefix "0x"
    int i = 0;
    if (inStr == NULL || byteLength == NULL || byteBuffer == NULL)
        return -1;
    strLength = strlen(inStr);
    if (strLength % 2)
        return -2;
    for (i = 0; i < strLength; i++) {
        if (!isxdigit(inStr[i]))
            return -3;
    }

    if (*byteLength < strLength / 2)
        return -4;

    *byteLength = strLength / 2;

    for (i = 0; i < *byteLength; i++) {
        char tmpStr[4] = { 0 };
        tmpStr[0] = inStr[i * 2];
        tmpStr[1] = inStr[i * 2 + 1];
        byteBuffer[i] = strtol(tmpStr, NULL, 16);
    }
    return 0;
}

void tpm2_util_hexdump2(FILE *f, const BYTE *data, size_t len) {

    size_t i;
    for (i=0; i < len; i++) {
        fprintf(f, "%02x", data[i]);
    }
}

void tpm2_util_hexdump(const BYTE *data, size_t len) {

    if (!output_enabled) {
        return;
    }

    tpm2_util_hexdump2(stdout, data, len);
}

bool tpm2_util_hexdump_file(FILE *fd, size_t len) {
    BYTE* buff = (BYTE*)malloc(len);
    if (!buff) {
        LOG_ERR("malloc() failed");
        return false;
    }

    bool res = files_read_bytes(fd, buff, len);
    if (!res) {
        LOG_ERR("Failed to read file");
        free(buff);
        return false;
    }

    tpm2_util_hexdump(buff, len);

    free(buff);
    return true;
}

bool tpm2_util_print_tpm2b_file(FILE *fd)
{
    UINT16 len;
    bool res = files_read_16(fd, &len);
    if(!res) {
        LOG_ERR("File read failed");
        return false;
    }
    return tpm2_util_hexdump_file(fd, len);
}

bool tpm2_util_is_big_endian(void) {

    uint32_t test_word;
    uint8_t *test_byte;

    test_word = 0xFF000000;
    test_byte = (uint8_t *) (&test_word);

    return test_byte[0] == 0xFF;
}

#define STRING_BYTES_ENDIAN_CONVERT(size) \
    UINT##size tpm2_util_endian_swap_##size(UINT##size data) { \
    \
        UINT##size converted; \
        UINT8 *bytes = (UINT8 *)&data; \
        UINT8 *tmp = (UINT8 *)&converted; \
    \
        size_t i; \
        for(i=0; i < sizeof(UINT##size); i ++) { \
            tmp[i] = bytes[sizeof(UINT##size) - i - 1]; \
        } \
        \
        return converted; \
    }

STRING_BYTES_ENDIAN_CONVERT(16)
STRING_BYTES_ENDIAN_CONVERT(32)
STRING_BYTES_ENDIAN_CONVERT(64)

#define STRING_BYTES_ENDIAN_HTON(size) \
    UINT##size tpm2_util_hton_##size(UINT##size data) { \
    \
        bool is_big_endian = tpm2_util_is_big_endian(); \
        if (is_big_endian) { \
           return data; \
        } \
    \
        return tpm2_util_endian_swap_##size(data); \
    }

STRING_BYTES_ENDIAN_HTON(16)
STRING_BYTES_ENDIAN_HTON(32)
STRING_BYTES_ENDIAN_HTON(64)

/*
 * Converting from host-to-network (hton) or network-to-host (ntoh) is
 * the same operation: if endianess differs between host and data, swap
 * endianess. Thus we can just call the hton routines, but have some nice
 * names for folks.
 */
UINT16 tpm2_util_ntoh_16(UINT16 data) {
    return tpm2_util_hton_16(data);
}

UINT32 tpm2_util_ntoh_32(UINT32 data) {
    return tpm2_util_hton_32(data);
}
UINT64 tpm2_util_ntoh_64(UINT64 data) {
    return tpm2_util_hton_64(data);
}

UINT32 tpm2_util_pop_count(UINT32 data) {

    static const UINT8 bits_per_nibble[] =
        {0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4};

    UINT8 count = 0;
    UINT8 *d = (UINT8 *)&data;

    size_t i;
    for (i=0; i < sizeof(data); i++) {
        count += bits_per_nibble[d[i] & 0x0f];
        count += bits_per_nibble[d[i] >> 4];
    }

    return count;
}

#define TPM2_UTIL_KEYDATA_INIT { .len = 0 };

typedef struct tpm2_util_keydata tpm2_util_keydata;
struct tpm2_util_keydata {
    UINT16 len;
    struct {
        const char *name;
        TPM2B *value;
    } entries[2];
};

static void tpm2_util_public_to_keydata(TPM2B_PUBLIC *public, tpm2_util_keydata *keydata) {

    switch (public->publicArea.type) {
    case TPM2_ALG_RSA:
        keydata->len = 1;
        keydata->entries[0].name = tpm2_alg_util_algtostr(public->publicArea.type, tpm2_alg_util_flags_any);
        keydata->entries[0].value = (TPM2B *)&public->publicArea.unique.rsa;
        return;
    case TPM2_ALG_KEYEDHASH:
        keydata->len = 1;
        keydata->entries[0].name = tpm2_alg_util_algtostr(public->publicArea.type, tpm2_alg_util_flags_any);
        keydata->entries[0].value = (TPM2B *)&public->publicArea.unique.keyedHash;
        return;
    case TPM2_ALG_SYMCIPHER:
        keydata->len = 1;
        keydata->entries[0].name = tpm2_alg_util_algtostr(public->publicArea.type, tpm2_alg_util_flags_any);
        keydata->entries[0].value = (TPM2B *)&public->publicArea.unique.sym;
        return;
    case TPM2_ALG_ECC:
        keydata->len = 2;
        keydata->entries[0].name = "x";
        keydata->entries[0].value = (TPM2B *)&public->publicArea.unique.ecc.x;
        keydata->entries[1].name = "y";
        keydata->entries[1].value = (TPM2B *)&public->publicArea.unique.ecc.y;
        return;
    default:
        LOG_WARN("The algorithm type(0x%4.4x) is not supported",
                public->publicArea.type);
    }

    return;
}

void print_yaml_indent(size_t indent_count) {
    while (indent_count--) {
        tpm2_tool_output("  ");
    }
}

void tpm2_util_tpma_object_to_yaml(TPMA_OBJECT obj, char *indent) {

    if (!indent) {
        indent = "";
    }

    char *attrs = tpm2_attr_util_obj_attrtostr(obj);
    tpm2_tool_output("%sattributes:\n", indent);
    tpm2_tool_output("%s  value: %s\n", indent, attrs);
    tpm2_tool_output("%s  raw: 0x%x\n", indent, obj);
    free(attrs);
}

static void print_alg_raw(const char *name, TPM2_ALG_ID alg, const char *indent) {

    tpm2_tool_output("%s%s:\n", indent, name);
    tpm2_tool_output("%s  value: %s\n", indent, tpm2_alg_util_algtostr(alg, tpm2_alg_util_flags_any));
    tpm2_tool_output("%s  raw: 0x%x\n", indent, alg);
}

static void print_scheme_common(TPMI_ALG_RSA_SCHEME scheme, const char *indent) {
    print_alg_raw("scheme", scheme, indent);
}

static void print_sym(TPMT_SYM_DEF_OBJECT *sym, const char *indent) {

    print_alg_raw("sym-alg", sym->algorithm, indent);
    print_alg_raw("sym-mode", sym->mode.sym, indent);
    tpm2_tool_output("%ssym-keybits: %u\n", indent, sym->keyBits.sym);
}

static void print_rsa_scheme(TPMT_RSA_SCHEME *scheme, const char *indent) {

    print_scheme_common(scheme->scheme, indent);

    /*
     * everything is a union on a hash algorithm except for RSAES which
     * has nothing. So on RSAES skip the hash algorithm printing
     */
    if (scheme->scheme != TPM2_ALG_RSAES) {
        print_alg_raw("scheme-halg", scheme->details.oaep.hashAlg, indent);
    }
}

static void print_ecc_scheme(TPMT_ECC_SCHEME *scheme, const char *indent) {

    print_scheme_common(scheme->scheme, indent);

    /*
     * everything but ecdaa uses only hash alg
     * in a union, so we only need to do things differently
     * for ecdaa.
     */
    print_alg_raw("scheme-halg", scheme->details.oaep.hashAlg, indent);

    if (scheme->scheme == TPM2_ALG_ECDAA) {
        tpm2_tool_output("%sscheme-count: %u\n", indent, scheme->details.ecdaa.count);
    }
}

static void print_kdf_scheme(TPMT_KDF_SCHEME *kdf, const char *indent) {

    print_alg_raw("kdfa-alg", kdf->scheme, indent);

    /*
     * The hash algorithm for the KDFA is in a union, just grab one of them.
     */
    print_alg_raw("kdfa-halg", kdf->details.mgf1.hashAlg, indent);
}

void tpm2_util_public_to_yaml(TPM2B_PUBLIC *public, char *indent) {

    if (!indent) {
        indent = "";
    }

    tpm2_tool_output("%sname-alg:\n", indent);
    tpm2_tool_output("%s  value: %s\n", indent, tpm2_alg_util_algtostr(public->publicArea.nameAlg, tpm2_alg_util_flags_any));
    tpm2_tool_output("%s  raw: 0x%x\n", indent, public->publicArea.nameAlg);

    tpm2_util_tpma_object_to_yaml(public->publicArea.objectAttributes, indent);

    tpm2_tool_output("%stype:\n", indent);
    tpm2_tool_output("%s  value: %s\n", indent, tpm2_alg_util_algtostr(public->publicArea.type, tpm2_alg_util_flags_any));
    tpm2_tool_output("%s  raw: 0x%x\n", indent, public->publicArea.type);

    switch(public->publicArea.type) {
    case TPM2_ALG_SYMCIPHER: {
        TPMS_SYMCIPHER_PARMS *s = &public->publicArea.parameters.symDetail;
        print_sym(&s->sym, indent);
    } break;
    case TPM2_ALG_KEYEDHASH: {
        TPMS_KEYEDHASH_PARMS *k = &public->publicArea.parameters.keyedHashDetail;
        tpm2_tool_output("%salgorithm: \n", indent);
        tpm2_tool_output("%s  value: %s\n", indent, tpm2_alg_util_algtostr(k->scheme.scheme, tpm2_alg_util_flags_any));
        tpm2_tool_output("%s  raw: 0x%x\n", indent, k->scheme.scheme);

        if (k->scheme.scheme == TPM2_ALG_HMAC) {
            tpm2_tool_output("%shash-alg:\n", indent);
            tpm2_tool_output("%s  value: %s\n", indent, tpm2_alg_util_algtostr(k->scheme.details.hmac.hashAlg, tpm2_alg_util_flags_any));
            tpm2_tool_output("%s  raw: 0x%x\n", indent, k->scheme.details.hmac.hashAlg);
        } else if (k->scheme.scheme == TPM2_ALG_XOR) {
            tpm2_tool_output("%shash-alg:\n", indent);
            tpm2_tool_output("%s  value: %s\n", indent, tpm2_alg_util_algtostr(k->scheme.details.exclusiveOr.hashAlg, tpm2_alg_util_flags_any));
            tpm2_tool_output("%s  raw: 0x%x\n", indent, k->scheme.details.exclusiveOr.hashAlg);

            tpm2_tool_output("%skdfa-alg:\n", indent);
            tpm2_tool_output("%s  value: %s\n", indent, tpm2_alg_util_algtostr(k->scheme.details.exclusiveOr.kdf, tpm2_alg_util_flags_any));
            tpm2_tool_output("%s  raw: 0x%x\n", indent, k->scheme.details.exclusiveOr.kdf);
        }

    } break;
    case TPM2_ALG_RSA: {
        TPMS_RSA_PARMS *r = &public->publicArea.parameters.rsaDetail;
        tpm2_tool_output("%sexponent: 0x%x\n", indent, r->exponent);
        tpm2_tool_output("%sbits: %u\n", indent, r->keyBits);

        print_rsa_scheme(&r->scheme, indent);

        print_sym(&r->symmetric, indent);
    } break;
    case TPM2_ALG_ECC: {
        TPMS_ECC_PARMS *e = &public->publicArea.parameters.eccDetail;

        tpm2_tool_output("%scurve-id:\n", indent);
        tpm2_tool_output("%s  value: %s\n", indent, tpm2_alg_util_ecc_to_str(e->curveID));
        tpm2_tool_output("%s  raw: 0x%x\n", indent, e->curveID);

        print_kdf_scheme(&e->kdf, indent);

        print_ecc_scheme(&e->scheme, indent);

        print_sym(&e->symmetric, indent);
    } break;
    }


    tpm2_util_keydata keydata = TPM2_UTIL_KEYDATA_INIT;
    tpm2_util_public_to_keydata(public, &keydata);

    UINT16 i;
    /* if no keydata len will be 0 and it wont print */
    for (i=0; i < keydata.len; i++) {
        tpm2_tool_output("%s%s: ", indent, keydata.entries[i].name);
        tpm2_util_print_tpm2b(keydata.entries[i].value);
        tpm2_tool_output("%s\n", indent);
    }

    if (public->publicArea.authPolicy.size) {
        tpm2_tool_output("%sauthorization policy: ", indent);
        tpm2_util_hexdump(public->publicArea.authPolicy.buffer,
                public->publicArea.authPolicy.size);
        tpm2_tool_output("%s\n", indent);
    }
}

bool tpm2_util_calc_unique(TPMI_ALG_HASH name_alg, TPM2B_PRIVATE_VENDOR_SPECIFIC *key,
        TPM2B_DIGEST *seed, TPM2B_DIGEST *unique_data) {

    TPM2B_MAX_BUFFER buf = { .size = key->size + seed->size };
    if (buf.size > sizeof(buf.buffer)) {
        LOG_ERR("Seed and key size are too big");
        return false;
    }

    memcpy(buf.buffer, seed->buffer, seed->size);
    memcpy(&buf.buffer[seed->size], key->buffer,
        key->size);

    digester d = tpm2_openssl_halg_to_digester(name_alg);
    if (!d) {
        return false;
    }

    unique_data->size = tpm2_alg_util_get_hash_size(name_alg);
    d(buf.buffer, buf.size, unique_data->buffer);

    return true;
}

ESYS_TR tpm2_tpmi_hierarchy_to_esys_tr(TPMI_RH_PROVISION inh) {

    switch (inh) {
    case TPM2_RH_OWNER:
        return ESYS_TR_RH_OWNER;
    case TPM2_RH_PLATFORM:
        return ESYS_TR_RH_PLATFORM;
    case TPM2_RH_ENDORSEMENT:
        return ESYS_TR_RH_ENDORSEMENT;
    case TPM2_RH_NULL:
        return ESYS_TR_RH_NULL;
    case TPM2_RH_LOCKOUT:
        return ESYS_TR_RH_LOCKOUT;
    }
    return ESYS_TR_NONE;
}

tool_rc tpm2_util_sys_handle_to_esys_handle(ESYS_CONTEXT *context,
        TPM2_HANDLE sys_handle, ESYS_TR *esys_handle) {

    ESYS_TR h = tpm2_tpmi_hierarchy_to_esys_tr(sys_handle);
    if (h != ESYS_TR_NONE) {
        *esys_handle = h;
        return tool_rc_success;
    }

    return tpm2_from_tpm_public(context, sys_handle,
                    ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, esys_handle);
}

tool_rc tpm2_util_esys_handle_to_sys_handle(ESYS_CONTEXT *context,
        ESYS_TR esys_handle, TPM2_HANDLE *sys_handle) {

    TPM2B_NAME *loaded_name = NULL;
    tool_rc rc = tpm2_tr_get_name(context, esys_handle, &loaded_name);
    if (rc != tool_rc_success) {
        return rc;
    }

    size_t offset = 0;
    TPM2_HANDLE hndl;
    // TODO: this doesn't produce handles that _look_ right
    rc = tpm2_mu_tpm2_handle_unmarshal(loaded_name->name, loaded_name->size,
                &offset, &hndl);
    if (rc != tool_rc_success) {
        goto outname;
    }

    *sys_handle = hndl;

outname:
    free(loaded_name);

    return rc;
}

char *tpm2_util_getenv(const char *name) {

    return getenv(name);
}

/**
 * Parses a hierarchy value from an option argument.
 * @param value
 *  The string to parse, which can be a numerical string as
 *  understood by strtoul() with a base of 0, or an:
 *    - o - Owner hierarchy
 *    - p - Platform hierarchy
 *    - e - Endorsement hierarchy
 *    - n - Null hierarchy
 * @param hierarchy
 *  The parsed hierarchy as output.
 * @param flags
 *  What hierarchies should be supported by
 *  the parsing.
 * @return
 *  True on success, False otherwise.
 */

static bool filter_hierarchy_handles(TPMI_RH_PROVISION hierarchy,
    tpm2_handle_flags flags) {

    switch(hierarchy) {
        case TPM2_RH_OWNER:
            if ( !(flags & TPM2_HANDLE_FLAGS_O) ) {
                LOG_ERR("Unexpected handle - TPM2_RH_OWNER");
                return false;
            }
            break;
        case TPM2_RH_PLATFORM:
            if ( !(flags & TPM2_HANDLE_FLAGS_P) ) {
                LOG_ERR("Unexpected handle - TPM2_RH_PLATFORM");
                return false;
            }
            break;
        case TPM2_RH_ENDORSEMENT:
            if ( !(flags & TPM2_HANDLE_FLAGS_E) ) {
                LOG_ERR("Unexpected handle - TPM2_RH_ENDORSEMENT");
                return false;
            }
            break;
        case TPM2_RH_NULL:
            if ( !(flags & TPM2_HANDLE_FLAGS_N) ) {
                LOG_ERR("Unexpected handle - TPM2_RH_NULL");
                return false;
            }
            break;
        case TPM2_RH_LOCKOUT:
            if ( !(flags & TPM2_HANDLE_FLAGS_L) ) {
                LOG_ERR("Unexpected handle - TPM2_RH_LOCKOUT");
                return false;
            }
            break;
        default: //If specified a random offset to the permanent handle range
            if (flags == TPM2_HANDLE_ALL_W_NV ||
                flags == TPM2_HANDLE_FLAGS_NONE) {
                return true;
            }
            return false;
    }

    return true;
}

static bool filter_handles(TPMI_RH_PROVISION *hierarchy, tpm2_handle_flags flags) {

    TPM2_RH range = *hierarchy & TPM2_HR_RANGE_MASK;

    /*
     * if their is no range, then it could be NV or PCR, use flags
     * to figure out what it is.
     */
    if (range == 0) {
        if (flags & TPM2_HANDLE_FLAGS_NV) {
           *hierarchy += TPM2_HR_NV_INDEX;
           range = *hierarchy & TPM2_HR_RANGE_MASK;
        } else if (flags & TPM2_HANDLE_FLAGS_PCR) {
            *hierarchy += TPM2_HR_PCR;
            range = *hierarchy & TPM2_HR_RANGE_MASK;
        } else {
            LOG_ERR("Implicit indices are not supported.");
            return false;
        }
    }

    /* now that we have fixed up any non-ranged handles, check them */
    if (range == TPM2_HR_NV_INDEX) {
        if (!(flags & TPM2_HANDLE_FLAGS_NV)) {
            LOG_ERR("NV-Index handles are not supported by this command.");
            return false;
        }
        if (*hierarchy < TPM2_NV_INDEX_FIRST
                || *hierarchy > TPM2_NV_INDEX_LAST) {
            LOG_ERR("NV-Index handle is out of range.");
            return false;
        }
        return true;
    } else if (range == TPM2_HR_PCR) {
        if(!(flags & TPM2_HANDLE_FLAGS_PCR)) {
            LOG_ERR("PCR handles are not supported by this command.");
            return false;
        }
        /* first is 0 so no possible way unsigned is less than 0, thus no check */
        if (*hierarchy > TPM2_PCR_LAST) {
            LOG_ERR("PCR handle out of range.");
            return false;
        }
        return true;
    } else if (range == TPM2_HR_TRANSIENT) {
        if (!(flags & TPM2_HANDLES_FLAGS_TRANSIENT)) {
            LOG_ERR("Transient handles are not supported by this command.");
            return false;
        }
        return true;
    } else if (range == TPM2_HR_PERMANENT) {
        return filter_hierarchy_handles(*hierarchy, flags);
    } else if (range == TPM2_HR_PERSISTENT) {
        if (!(flags & TPM2_HANDLES_FLAGS_PERSISTENT)) {
            LOG_ERR("Persistent handles are not supported by this command.");
            return false;
        }
        if (*hierarchy < TPM2_PERSISTENT_FIRST ||
                *hierarchy > TPM2_PERSISTENT_LAST) {
            LOG_ERR("Persistent handle out of range.");
            return false;
        }
        return true;
    }

    /* else its a session flag and shouldn't use this interface */
    return false;
}

bool tpm2_util_handle_from_optarg(const char *value,
        TPMI_RH_PROVISION *hierarchy, tpm2_handle_flags flags) {

    if (!value || !value[0]) {
        return false;
    }

    if ((flags & TPM2_HANDLE_FLAGS_NV) &&
            (flags & TPM2_HANDLE_FLAGS_PCR)) {
        LOG_ERR("Cannot specify NV and PCR index together");
        return false;
    }

    *hierarchy = 0;

    bool is_o = !strncmp(value, "owner", strlen(value));
    if (is_o) {
        *hierarchy = TPM2_RH_OWNER;
    }

    bool is_p = !strncmp(value, "platform", strlen(value));
    if (is_p) {
        *hierarchy = TPM2_RH_PLATFORM;
    }

    bool is_e = !strncmp(value, "endorsement", strlen(value));
    if (is_e) {
        *hierarchy = TPM2_RH_ENDORSEMENT;
    }

    bool is_n = !strncmp(value, "null", strlen(value));
    if (is_n) {
        *hierarchy = TPM2_RH_NULL;
    }

    bool is_l = !strncmp(value, "lockout", strlen(value));
    if (is_l) {
        *hierarchy = TPM2_RH_LOCKOUT;
    }

    bool result = true;
    if (!*hierarchy) {
        /*
         * This branch is executed when hierarchy is specified as a hex handle.
         * The raw hex returned may be a generic (non hierarchy) TPM2_HANDLE.
         */
        result = tpm2_util_string_to_uint32(value, hierarchy);
    }
    if (!result) {

        char msg[256] = { 0 };

        char print_flags[32] = { '[', '\0' };

        if (flags & TPM2_HANDLE_FLAGS_O) {
            strncat(print_flags, "o|",
                    sizeof(print_flags) - strlen(print_flags) - 1);
        }

        if (flags & TPM2_HANDLE_FLAGS_P) {
            strncat(print_flags, "p|",
                    sizeof(print_flags) - strlen(print_flags) - 1);
        }

        if (flags & TPM2_HANDLE_FLAGS_E) {
            strncat(print_flags, "e|",
                    sizeof(print_flags) - strlen(print_flags) - 1);
        }

        if (flags & TPM2_HANDLE_FLAGS_N) {
            strncat(print_flags, "n|",
                    sizeof(print_flags) - strlen(print_flags) - 1);
        }

        if (flags & TPM2_HANDLE_FLAGS_L) {
            strncat(print_flags, "l|",
                    sizeof(print_flags) - strlen(print_flags) - 1);
        }

        size_t len = strlen(print_flags);
        if (print_flags[len -1] == '|') {
            len--;
            print_flags[len] = '\0';
        }

        strncat(print_flags, "]",
                sizeof(print_flags) - strlen(print_flags) - 1);
        len++;

        bool has_print_flags = len > 2;

        if (has_print_flags) {
            snprintf(msg, sizeof(msg), "expected %s or ", print_flags);
        }

        strncat(msg, "a handle number",
                sizeof(msg) - strlen(msg) - 1);

        LOG_ERR("Incorrect handle value, got: \"%s\", expected %s",
                 value, msg);
        return false;
    }

    /*
     * If the caller specifies the expected valid hierarchies, either as string,
     * or hex handles, they are additionally filtered here.
     */

    bool res = filter_handles(hierarchy, flags);
    if (!res) {
        LOG_ERR("Unknown or unsupported handle, got: \"%s\"",
                 value);
    }
    return res;
}

bool tpm2_util_get_label(const char *value, TPM2B_DATA *label) {

    if (!value) {
        label->size = 0;
        return true;
    }

    FILE *f = fopen(value, "rb");
    if (f) {
        /* set size one smaller for NUL byte */
        label->size = sizeof(label->buffer) - 1;
        size_t cnt = fread(label->buffer, 1, label->size, f);
        if (!feof(f)) {
            LOG_ERR("label file \"%s\" larger than expected. Expected %u",
                    value, label->size);
            fclose(f);
            return false;
        }
        if (ferror(f)) {
            LOG_ERR("reading label file \"%s\" error: %s",
                    value, strerror(errno));
            fclose(f);
            return false;
        }
        fclose(f);

        label->size = cnt;

        /* Set NUL byte and increment */
        label->buffer[label->size++] = '\0';

        return true;
    }

    size_t len = strlen(value);
    if (len > sizeof(label->buffer) - 1) {
        LOG_ERR("label file \"%s\" larger than expected. Expected %zu",
                value, sizeof(label->buffer) - 1);
        return false;
    }

    memcpy(label->buffer, value, len);

    label->size = len;
    /* Set NUL byte and increment */
    label->buffer[label->size++] = '\0';

    return true;
}
