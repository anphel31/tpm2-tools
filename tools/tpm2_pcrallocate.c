/* SPDX-License-Identifier: BSD-3-Clause */

#include "log.h"
#include "pcr.h"
#include "tpm2.h"
#include "tpm2_options.h"

static struct {
    TPML_PCR_SELECTION pcrSelection;
    struct {
        const char *ctx_path;
        const char *auth_str;
        tpm2_loaded_object object;
    } auth_hierarchy;
} ctx = {
    .pcrSelection = {
        .count = 2,
        .pcrSelections = { {
            .hash = TPM2_ALG_SHA1,
            .sizeofSelect = 3,
            .pcrSelect = { 0xff, 0xff, 0xff, }
            }, {
            .hash = TPM2_ALG_SHA256,
            .sizeofSelect = 3,
            .pcrSelect = { 0xff, 0xff, 0xff, }
        }, }
    },
    .auth_hierarchy.ctx_path = "platform",
};

static bool on_arg(int argc, char **argv){
    if (argc > 1) {
        LOG_ERR("Too many arguments");
        return false;
    }

    if(argc == 1 && !pcr_parse_selections(argv[0], &ctx.pcrSelection))
    {
        LOG_ERR("Could not parse pcr selections");
        return false;
    }

    return true;
}

static bool on_option(char key, char *value) {
    switch (key) {
    case 'P':
        ctx.auth_hierarchy.auth_str = value;
        break;
    }

    return true;
}

bool tpm2_tool_onstart(tpm2_options **opts) {
    const struct option topts[] = {
        { "auth",     required_argument, NULL, 'P' },
    };

    *opts = tpm2_options_new("P:", ARRAY_LEN(topts), topts, on_option, on_arg,
                             0);

    return *opts != NULL;
}

tool_rc tpm2_tool_onrun(ESYS_CONTEXT *ectx, tpm2_option_flags flags) {
    UNUSED(flags);

    tool_rc rc = tpm2_util_object_load_auth(ectx, ctx.auth_hierarchy.ctx_path,
        ctx.auth_hierarchy.auth_str, &ctx.auth_hierarchy.object, false,
        TPM2_HANDLE_FLAGS_P);
    if (rc != tool_rc_success) {
        LOG_ERR("Invalid platform authorization format.");
        return rc;
    }

    rc = tpm2_pcr_allocate(ectx, &ctx.auth_hierarchy.object, &ctx.pcrSelection);
    if (rc == tool_rc_success) {
        pcr_print_pcr_selections(&ctx.pcrSelection);
    }

    return rc;
}

tool_rc tpm2_tool_onstop(ESYS_CONTEXT *ectx) {
    UNUSED(ectx);
    return tpm2_session_close(&ctx.auth_hierarchy.object.session);
}
