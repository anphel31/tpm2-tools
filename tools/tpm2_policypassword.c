/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdlib.h>

#include "files.h"
#include "log.h"
#include "tpm2_policy.h"
#include "tpm2_tool.h"

typedef struct tpm2_policypassword_ctx tpm2_policypassword_ctx;
struct tpm2_policypassword_ctx {
   //File path for the session context data
   const char *session_path;
   //File path for storing the policy digest output
   const char *out_policy_dgst_path;

   TPM2B_DIGEST *policy_digest;
   tpm2_session *session;
};

static tpm2_policypassword_ctx ctx;

static bool on_option(char key, char *value) {

    switch (key) {
    case 'L':
        ctx.out_policy_dgst_path = value;
        break;
    case 'S':
        ctx.session_path = value;
        break;
    }

    return true;
}

bool tpm2_tool_onstart(tpm2_options **opts) {

    static struct option topts[] = {
        { "policy",             required_argument, NULL, 'L' },
        { "session",            required_argument, NULL, 'S' },
    };

    *opts = tpm2_options_new("S:L:", ARRAY_LEN(topts), topts, on_option,
                             NULL, 0);

    return *opts != NULL;
}

bool is_input_option_args_valid(void) {

    if (!ctx.session_path) {
        LOG_ERR("Must specify -S session file.");
        return false;
    }
    return true;
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

    rc = tpm2_policy_build_policypassword(ectx, ctx.session);
    if (rc != tool_rc_success) {
        LOG_ERR("Could not build policypassword TPM");
        return rc;
    }

    return tpm2_policy_tool_finish(ectx, ctx.session, ctx.out_policy_dgst_path);
}

tool_rc tpm2_tool_onstop(ESYS_CONTEXT *ectx) {
    UNUSED(ectx);
    free(ctx.policy_digest);
    return tpm2_session_close(&ctx.session);
}
