/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "log.h"
#include "object.h"
#include "tpm2_alg_util.h"
#include "tpm2_convert.h"
#include "tpm2_openssl.h"
#include "tpm2_options.h"

typedef struct tpm2_verifysig_ctx tpm2_verifysig_ctx;
struct tpm2_verifysig_ctx {
    union {
        struct {
            UINT8 halg :1;
            UINT8 msg :1;
            UINT8 sig :1;
            UINT8 pcr :1;
            UINT8 extra :1;
            UINT8 key_context :1;
            UINT8 fmt;
        };
        UINT8 all;
    } flags;
    TPMI_ALG_SIG_SCHEME format;
    TPMI_ALG_HASH halg;
    TPM2B_DIGEST msgHash;
    TPM2B_DIGEST pcrHash;
    TPM2B_DIGEST quoteHash;
    TPM2B_DATA quoteExtraData;
    TPM2B_DATA extraData;
    TPMT_SIGNATURE signature;
    char *msg_file_path;
    char *sig_file_path;
    char *out_file_path;
    char *pcr_file_path;
    const char *pubkey_file_path;
    tpm2_loaded_object key_context_object;
};

static tpm2_verifysig_ctx ctx = {
        .format = TPM2_ALG_ERROR,
        .halg = TPM2_ALG_SHA1,
        .msgHash = TPM2B_TYPE_INIT(TPM2B_DIGEST, buffer),
        .pcrHash = TPM2B_TYPE_INIT(TPM2B_DIGEST, buffer),
        .quoteHash = TPM2B_TYPE_INIT(TPM2B_DIGEST, buffer),
        .quoteExtraData = TPM2B_TYPE_INIT(TPM2B_DATA, buffer),
        .extraData = TPM2B_TYPE_INIT(TPM2B_DATA, buffer),
};

static bool verify_signature() {

    bool result = false;

    // Read in the AKpub they provided as an RSA object
    FILE *pubkey_input = fopen(ctx.pubkey_file_path, "rb");
    if (!pubkey_input) {
        LOG_ERR("Could not open RSA pubkey input file \"%s\" error: \"%s\"",
                ctx.pubkey_file_path, strerror(errno));
        return false;
    }
    RSA *pubKey = tpm2_openssl_get_public_RSA_from_pem(pubkey_input, ctx.pubkey_file_path);
    if (pubKey == NULL) {
        LOG_ERR("Failed to load RSA public key from file");
        goto err;
    }

    // Get the signature ready
    if (ctx.signature.sigAlg != TPM2_ALG_RSASSA) {
        LOG_ERR("Only RSASSA is supported for signatures");
        goto err;
    }
    TPM2B_PUBLIC_KEY_RSA sig = ctx.signature.signature.rsassa.sig;
    tpm2_util_hexdump(sig.buffer, sig.size);

    // Verify the signature matches message digest
    int opensslHash = tpm2_openssl_halgid_from_tpmhalg(ctx.signature.signature.rsassa.hash);
    if (!RSA_verify(opensslHash, ctx.msgHash.buffer, ctx.msgHash.size,
            sig.buffer, sig.size, pubKey)) {
        LOG_ERR("Error validating signed message with public key provided");
        goto err;
    }

    // Ensure nonce is the same as given
    if (ctx.flags.extra) {
        if (
            ctx.quoteExtraData.size != ctx.extraData.size
            || memcmp(ctx.quoteExtraData.buffer, ctx.extraData.buffer, ctx.extraData.size) != 0
        ) {
            LOG_ERR("Error validating nonce from quote");
            goto err;
        }
    }

    // Also ensure digest from quote matches PCR digest
    if (ctx.flags.pcr) {
        if (!tpm2_util_verify_digests(&ctx.quoteHash, &ctx.pcrHash)) {
            LOG_ERR("Error validating PCR composite against signed message");
            goto err;
        }
    }

    result = true;

err:
    if (pubkey_input) {
        fclose(pubkey_input);
    }

    RSA_free(pubKey);

    return result;
}

static TPM2B_ATTEST *message_from_file(const char *msg_file_path) {

    unsigned long size;

    bool result = files_get_file_size_path(msg_file_path, &size);
    if (!result) {
        return NULL;
    }

    if (!size) {
        LOG_ERR("The msg file \"%s\" is empty", msg_file_path);
        return NULL;
    }

    TPM2B_ATTEST *msg = (TPM2B_ATTEST *) calloc(1, sizeof(TPM2B_ATTEST) + size);
    if (!msg) {
        LOG_ERR("OOM");
        return NULL;
    }

    UINT16 tmp = msg->size = size;
    if (!files_load_bytes_from_path(msg_file_path, msg->attestationData, &tmp)) {
        free(msg);
        return NULL;
    }
    return msg;
}

static bool pcrs_from_file(const char *pcr_file_path,
        TPML_PCR_SELECTION *pcrSel, tpm2_pcrs *pcrs) {

    bool result = false;
    unsigned long size;

    if (!files_get_file_size_path(pcr_file_path, &size)) {
        return false;
    }

    if (!size) {
        LOG_ERR("The pcr file \"%s\" is empty", pcr_file_path);
        return false;
    }

    FILE *pcr_input = fopen(pcr_file_path, "rb");
    if (!pcr_input) {
        LOG_ERR("Could not open PCRs input file \"%s\" error: \"%s\"",
                pcr_file_path, strerror(errno));
        goto out;
    }

    // Import TPML_PCR_SELECTION structure to pcr outfile
    if (fread(pcrSel, sizeof(TPML_PCR_SELECTION), 1, pcr_input) != 1) {
        LOG_ERR("Failed to read PCR selection from file");
        goto out;
    }

    // Import PCR digests to pcr outfile
    if (fread(&pcrs->count, sizeof(UINT32), 1, pcr_input) != 1) {
        LOG_ERR("Failed to read PCR digests header from file");
        goto out;
    }

    if (pcrs->count > ARRAY_LEN(pcrs->pcr_values)) {
        LOG_ERR("Malformed PCR file, pcr count cannot be greater than %zu, got: %zu",
                ARRAY_LEN(pcrs->pcr_values), pcrs->count);
        goto out;
    }

    UINT32 j;
    for (j = 0; j < pcrs->count; j++) {
        if (fread(&pcrs->pcr_values[j], sizeof(TPML_DIGEST), 1, pcr_input) != 1) {
            LOG_ERR("Failed to read PCR digest from file");
            goto out;
        }
    }

    result = true;

out:
    if (pcr_input) {
        fclose(pcr_input);
    }

    return result;
}

static tool_rc init(void) {

    /* check flags for mismatches */
    if (!(ctx.pubkey_file_path && ctx.flags.sig && ctx.flags.msg && ctx.flags.halg)) {
        LOG_ERR(
                "--pubkey (-c), --msg (-m), --halg (-g) and --sig (-s) are required");
        return tool_rc_option_error;
    }

    TPM2B_ATTEST *msg = NULL;
    TPML_PCR_SELECTION pcrSel;
    tpm2_pcrs pcrs;
    tool_rc return_value = tool_rc_general_error;

    if (ctx.flags.msg) {
        msg = message_from_file(ctx.msg_file_path);
        if (!msg) {
            /* message_from_file() logs specific error no need to here */
            return tool_rc_general_error;
        }
    }

    if (ctx.flags.sig) {
        tpm2_convert_sig_fmt fmt = ctx.flags.fmt ? signature_format_plain : signature_format_tss;
        bool res = tpm2_convert_sig_load(ctx.sig_file_path, fmt, ctx.format, ctx.halg, &ctx.signature);
        if (!res) {
            goto err;
        }
    }

    /* If no digest is specified, compute it */
    if (!ctx.flags.msg) {
        /*
         * This is a redundant check since main() checks this case, but we'll add it here to silence any
         * complainers.
         */
        LOG_ERR("No digest set and no message file to compute from, cannot compute message hash!");
        goto err;
    }

    if (ctx.flags.pcr) {
        if (!pcrs_from_file(ctx.pcr_file_path, &pcrSel, &pcrs)) {
            /* pcrs_from_file() logs specific error no need to here */
            goto err;
        }

        if (!tpm2_openssl_hash_pcr_banks(ctx.halg, &pcrSel, &pcrs, &ctx.pcrHash)) {
            LOG_ERR("Failed to hash PCR values related to quote!");
            goto err;
        }
        if (!pcr_print_pcr_struct(&pcrSel, &pcrs)) {
            LOG_ERR("Failed to print PCR values related to quote!");
            goto err;
        }
    }

    // Figure out the extra data (nonce) from this message
    if (!tpm2_util_get_digest_from_quote(msg, &ctx.quoteHash, &ctx.quoteExtraData)) {
        LOG_ERR("Failed to get digest from quote!");
        goto err;
    }

    // Figure out the digest for this message
    bool res = tpm2_openssl_hash_compute_data(ctx.halg, msg->attestationData,
        msg->size, &ctx.msgHash);
    if (!res) {
        LOG_ERR("Compute message hash failed!");
        goto err;
    }

    return_value = tool_rc_success;

err:
    free(msg);
    return return_value;

}

static bool on_option(char key, char *value) {

	switch (key) {
	case 'u':
	    ctx.pubkey_file_path = value;
	    break;
	case 'g': {
		ctx.halg = tpm2_alg_util_from_optarg(value, tpm2_alg_util_flags_hash);
		if (ctx.halg == TPM2_ALG_ERROR) {
			LOG_ERR("Unable to convert algorithm, got: \"%s\"", value);
			return false;
		}
		ctx.flags.halg = 1;
	}
		break;
	case 'm': {
		ctx.msg_file_path = value;
		ctx.flags.msg = 1;
	}
		break;
	case 'F': {
		ctx.format = tpm2_alg_util_from_optarg(value, tpm2_alg_util_flags_sig);
		if (ctx.format == TPM2_ALG_ERROR) {
		    LOG_ERR("Unknown signing scheme, got: \"%s\"", value);
		    return false;
		}

		ctx.flags.fmt = 1;
	} break;
	case 'q':
		ctx.extraData.size = sizeof(ctx.extraData) - 2;
		if(tpm2_util_hex_to_byte_structure(value, &ctx.extraData.size, ctx.extraData.buffer) != 0)
		{
			LOG_ERR("Could not convert \"%s\" from a hex string to byte array!", value);
			return false;
		}
		ctx.flags.extra = 1;
		break;
	case 's':
		ctx.sig_file_path = value;
		ctx.flags.sig = 1;
		break;
	case 'f':
		ctx.pcr_file_path = value;
		ctx.flags.pcr = 1;
		break;
		/* no default */
	}

    return true;
}

bool tpm2_tool_onstart(tpm2_options **opts) {

    const struct option topts[] = {
            { "hash-algorithm",     required_argument, NULL, 'g' },
            { "message",            required_argument, NULL, 'm' },
            { "format",             required_argument, NULL, 'F' },
            { "signature",          required_argument, NULL, 's' },
            { "pcr",                required_argument, NULL, 'f' },
            { "public",             required_argument, NULL, 'u' },
            { "qualification",      required_argument, NULL, 'q' },
    };


    *opts = tpm2_options_new("g:m:F:s:u:f:q:", ARRAY_LEN(topts), topts,
                             on_option, NULL, TPM2_OPTIONS_NO_SAPI);

    return *opts != NULL;
}

tool_rc tpm2_tool_onrun(ESYS_CONTEXT *ectx, tpm2_option_flags flags) {

	UNUSED(ectx);
	UNUSED(flags);

    /* initialize and process */
    tool_rc rc = init();
    if (rc != tool_rc_success) {
        return rc;
    }

    bool res = verify_signature();
    if (!res) {
        LOG_ERR("Verify signature failed!");
        return tool_rc_general_error;
    }

    return tool_rc_success;
}
