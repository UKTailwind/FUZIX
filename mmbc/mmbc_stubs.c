/* mmbc_stubs.c - TEMPORARY stand-ins for the statement region
 * (mmbc_stmt.c) so the binary links and the burn-down gate runs while
 * that region is being ported.  Every statement errs and is commented
 * out by lenient mode.  DELETE this file when mmbc_stmt.c lands. */

#include "mmbc.h"

void statement_inner(void)
{
    struct tok *t = peek(0);

    if (t == NULL)
        return;
    cv_err("mmbc: statement not yet ported");
}

char *zero_of(struct sym *s)
{
    (void)s;
    return sstr("/* zero_of: not yet ported */");
}

char *signature(struct routine *r)
{
    return sfmt("/* signature of %s: not yet ported */", r->cname);
}
