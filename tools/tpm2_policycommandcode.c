/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdlib.h>

#include "files.h"
#include "log.h"
#include "tpm2_cc_util.h"
#include "tpm2_policy.h"
#include "tpm2_tool.h"

typedef struct tpm2_policycommandcode_ctx tpm2_policycommandcode_ctx;
struct tpm2_policycommandcode_ctx {
   const char *session_path;
   TPM2_CC command_code;
   const char *out_policy_dgst_path;
   TPM2B_DIGEST *policy_digest;
   tpm2_session *session;
};

static tpm2_policycommandcode_ctx ctx;

static bool on_option(char key, char *value) {

    switch (key) {
    case 'S':
        ctx.session_path = value;
        break;
    case 'L':
        ctx.out_policy_dgst_path = value;
        break;
    }
    return true;
}

bool is_input_option_args_valid(void) {

    if (!ctx.session_path) {
        LOG_ERR("Must specify -S session file.");
        return false;
    }

    return true;
}

bool on_arg (int argc, char **argv) {

    if (argc > 1) {
        LOG_ERR("Specify only the TPM2 command code.");
        return false;
    }

    if (!argc) {
        LOG_ERR("TPM2 command code must be specified.");
        return false;
    }

    bool result = tpm2_cc_util_from_str(argv[0], &ctx.command_code);
    if (!result) {
        return false;
    }

    return true;
}

bool tpm2_tool_onstart(tpm2_options **opts) {

    static struct option topts[] = {
        { "session", required_argument,  NULL,   'S' },
        { "policy",  required_argument,  NULL,   'L' },
    };

    *opts = tpm2_options_new("S:L:", ARRAY_LEN(topts), topts, on_option,
                             on_arg, 0);

    return *opts != NULL;
}

tool_rc tpm2_tool_onrun(ESYS_CONTEXT *ectx, tpm2_option_flags flags) {

    UNUSED(flags);

    bool retval = is_input_option_args_valid();
    if (!retval) {
        return tool_rc_option_error;
    }

    tool_rc rc = tpm2_session_restore(ectx, ctx.session_path, false, &ctx.session);
    if (rc != tool_rc_success) {
        return rc;
    }

    rc = tpm2_policy_build_policycommandcode(ectx, ctx.session,
        ctx.command_code);
    if (rc != tool_rc_success) {
        LOG_ERR("Could not build TPM policy_command_code");
        return rc;
    }

    return tpm2_policy_tool_finish(ectx, ctx.session, ctx.out_policy_dgst_path);
}

tool_rc tpm2_tool_onstop(ESYS_CONTEXT *ectx) {
    UNUSED(ectx);
    free(ctx.policy_digest);
    return tpm2_session_close(&ctx.session);
}
