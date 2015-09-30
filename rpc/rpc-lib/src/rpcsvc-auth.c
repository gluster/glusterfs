/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "rpcsvc.h"
#include "logging.h"
#include "dict.h"

extern rpcsvc_auth_t *
rpcsvc_auth_null_init (rpcsvc_t *svc, dict_t *options);

extern rpcsvc_auth_t *
rpcsvc_auth_unix_init (rpcsvc_t *svc, dict_t *options);

extern rpcsvc_auth_t *
rpcsvc_auth_glusterfs_init (rpcsvc_t *svc, dict_t *options);
extern rpcsvc_auth_t *
rpcsvc_auth_glusterfs_v2_init (rpcsvc_t *svc, dict_t *options);

int
rpcsvc_auth_add_initer (struct list_head *list, char *idfier,
                        rpcsvc_auth_initer_t init)
{
        struct rpcsvc_auth_list         *new = NULL;

        if ((!list) || (!init) || (!idfier))
                return -1;

        new = GF_CALLOC (1, sizeof (*new), gf_common_mt_rpcsvc_auth_list);
        if (!new) {
                return -1;
        }

        new->init = init;
        strncpy (new->name, idfier, sizeof (new->name) - 1);
        INIT_LIST_HEAD (&new->authlist);
        list_add_tail (&new->authlist, list);
        return 0;
}



int
rpcsvc_auth_add_initers (rpcsvc_t *svc)
{
        int     ret = -1;

        ret = rpcsvc_auth_add_initer (&svc->authschemes, "auth-glusterfs",
                                      (rpcsvc_auth_initer_t)
                                      rpcsvc_auth_glusterfs_init);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to add AUTH_GLUSTERFS");
                goto err;
        }


        ret = rpcsvc_auth_add_initer (&svc->authschemes, "auth-glusterfs-v2",
                                      (rpcsvc_auth_initer_t)
                                      rpcsvc_auth_glusterfs_v2_init);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR,
                        "Failed to add AUTH_GLUSTERFS-v2");
                goto err;
        }

        ret = rpcsvc_auth_add_initer (&svc->authschemes, "auth-unix",
                                      (rpcsvc_auth_initer_t)
                                      rpcsvc_auth_unix_init);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to add AUTH_UNIX");
                goto err;
        }

        ret = rpcsvc_auth_add_initer (&svc->authschemes, "auth-null",
                                      (rpcsvc_auth_initer_t)
                                      rpcsvc_auth_null_init);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to add AUTH_NULL");
                goto err;
        }

        ret = 0;
err:
        return ret;
}


int
rpcsvc_auth_init_auth (rpcsvc_t *svc, dict_t *options,
                       struct rpcsvc_auth_list *authitem)
{
        int             ret = -1;

        if ((!svc) || (!options) || (!authitem))
                return -1;

        if (!authitem->init) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "No init function defined");
                ret = -1;
                goto err;
        }

        authitem->auth = authitem->init (svc, options);
        if (!authitem->auth) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Registration of auth failed:"
                        " %s", authitem->name);
                ret = -1;
                goto err;
        }

        authitem->enable = 1;
        gf_log (GF_RPCSVC, GF_LOG_TRACE, "Authentication enabled: %s",
                authitem->auth->authname);

        ret = 0;
err:
        return ret;
}


int
rpcsvc_auth_init_auths (rpcsvc_t *svc, dict_t *options)
{
        int                     ret = -1;
        struct rpcsvc_auth_list *auth = NULL;
        struct rpcsvc_auth_list *tmp = NULL;

        if (!svc)
                return -1;

        if (list_empty (&svc->authschemes)) {
                gf_log (GF_RPCSVC, GF_LOG_WARNING, "No authentication!");
                ret = 0;
                goto err;
        }

        /* If auth null and sys are not disabled by the user, we must enable
         * it by default. This is a globally default rule, the user is still
         * allowed to disable the two for particular subvolumes.
         */
        if (!dict_get (options, "rpc-auth.auth-null")) {
                ret = dict_set_str (options, "rpc-auth.auth-null", "on");
                if (ret)
                        gf_log ("rpc-auth", GF_LOG_DEBUG,
                                "dict_set failed for 'auth-nill'");
        }

        if (!dict_get (options, "rpc-auth.auth-unix")) {
                ret = dict_set_str (options, "rpc-auth.auth-unix", "on");
                if (ret)
                        gf_log ("rpc-auth", GF_LOG_DEBUG,
                                "dict_set failed for 'auth-unix'");
        }

        if (!dict_get (options, "rpc-auth.auth-glusterfs")) {
                ret = dict_set_str (options, "rpc-auth.auth-glusterfs", "on");
                if (ret)
                        gf_log ("rpc-auth", GF_LOG_DEBUG,
                                "dict_set failed for 'auth-unix'");
        }

        list_for_each_entry_safe (auth, tmp, &svc->authschemes, authlist) {
                ret = rpcsvc_auth_init_auth (svc, options, auth);
                if (ret == -1)
                        goto err;
        }

        ret = 0;
err:
        return ret;

}

int
rpcsvc_set_addr_namelookup (rpcsvc_t *svc, dict_t *options)
{
        int             ret;
        static char     *addrlookup_key = "rpc-auth.addr.namelookup";

        if (!svc || !options)
                return (-1);

        /* By default it's disabled */
        ret = dict_get_str_boolean (options, addrlookup_key, _gf_false);
        if (ret < 0) {
                svc->addr_namelookup = _gf_false;
        } else {
                svc->addr_namelookup = ret;
        }

        if (svc->addr_namelookup)
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "Addr-Name lookup enabled");

        return (0);
}

int
rpcsvc_set_allow_insecure (rpcsvc_t *svc, dict_t *options)
{
        int             ret = -1;
        char            *allow_insecure_str = NULL;
        gf_boolean_t    is_allow_insecure = _gf_false;

        GF_ASSERT (svc);
        GF_ASSERT (options);

        ret = dict_get_str (options, "rpc-auth-allow-insecure",
                            &allow_insecure_str);
        if (0 == ret) {
                ret = gf_string2boolean (allow_insecure_str,
                                         &is_allow_insecure);
                if (0 == ret) {
                        if (_gf_true == is_allow_insecure)
                                svc->allow_insecure = 1;
                        else
                                svc->allow_insecure = 0;
                }
        } else {
                /* By default set allow-insecure to true */
                svc->allow_insecure = 1;

                /* setting in options for the sake of functions that look
                 * configuration params for allow insecure,  eg: gf_auth
                 */
                ret = dict_set_str (options, "rpc-auth-allow-insecure", "on");
                if (ret < 0)
                        gf_log ("rpc-auth", GF_LOG_DEBUG,
                                        "dict_set failed for 'allow-insecure'");
        }

        return ret;
}

int
rpcsvc_set_root_squash (rpcsvc_t *svc, dict_t *options)
{
        int  ret = -1;
        uid_t anonuid = -1;
        gid_t anongid = -1;

        GF_ASSERT (svc);
        GF_ASSERT (options);

        ret = dict_get_str_boolean (options, "root-squash", 0);
        if (ret != -1)
                svc->root_squash = ret;
        else
                svc->root_squash = _gf_false;

        ret = dict_get_uint32 (options, "anonuid", &anonuid);
        if (!ret)
                svc->anonuid = anonuid;
        else
                svc->anonuid = RPC_NOBODY_UID;

        ret = dict_get_uint32 (options, "anongid", &anongid);
        if (!ret)
                svc->anongid = anongid;
        else
                svc->anongid = RPC_NOBODY_GID;

        if (svc->root_squash)
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "root squashing enabled "
                        "(uid=%d, gid=%d)", svc->anonuid, svc->anongid);

        return 0;
}

int
rpcsvc_auth_init (rpcsvc_t *svc, dict_t *options)
{
        int             ret = -1;

        if ((!svc) || (!options))
                return -1;

        (void) rpcsvc_set_allow_insecure (svc, options);
        (void) rpcsvc_set_root_squash (svc, options);
        (void) rpcsvc_set_addr_namelookup (svc, options);
        ret = rpcsvc_auth_add_initers (svc);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to add initers");
                goto out;
        }

        ret = rpcsvc_auth_init_auths (svc, options);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to init auth schemes");
                goto out;
        }

out:
        return ret;
}

int
rpcsvc_auth_reconf (rpcsvc_t *svc, dict_t *options)
{
        int ret = 0;

        if ((!svc) || (!options))
                return (-1);

        ret = rpcsvc_set_allow_insecure (svc, options);
        if (ret)
                return (-1);

        ret = rpcsvc_set_root_squash (svc, options);
        if (ret)
                return (-1);

        return rpcsvc_set_addr_namelookup (svc, options);
}


rpcsvc_auth_t *
__rpcsvc_auth_get_handler (rpcsvc_request_t *req)
{
        struct rpcsvc_auth_list *auth = NULL;
        struct rpcsvc_auth_list *tmp = NULL;
        rpcsvc_t                *svc = NULL;

        if (!req)
                return NULL;

        svc = req->svc;
        if (!svc) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "!svc");
                goto err;
        }

        if (list_empty (&svc->authschemes)) {
                gf_log (GF_RPCSVC, GF_LOG_WARNING, "No authentication!");
                goto err;
        }

        list_for_each_entry_safe (auth, tmp, &svc->authschemes, authlist) {
                if (!auth->enable)
                        continue;
                if (auth->auth->authnum == req->cred.flavour)
                        goto err;

        }

        auth = NULL;
err:
        if (auth)
                return auth->auth;
        else
                return NULL;
}

rpcsvc_auth_t *
rpcsvc_auth_get_handler (rpcsvc_request_t *req)
{
        rpcsvc_auth_t           *auth = NULL;

        auth = __rpcsvc_auth_get_handler (req);
        if (auth)
                goto ret;

        gf_log (GF_RPCSVC, GF_LOG_TRACE, "No auth handler: %d",
                req->cred.flavour);

        /* The requested scheme was not available so fall back the to one
         * scheme that will always be present.
         */
        req->cred.flavour = AUTH_NULL;
        req->verf.flavour = AUTH_NULL;
        auth = __rpcsvc_auth_get_handler (req);
ret:
        return auth;
}


int
rpcsvc_auth_request_init (rpcsvc_request_t *req, struct rpc_msg *callmsg)
{
        int32_t                 ret = 0;
        rpcsvc_auth_t           *auth = NULL;

        if (!req || !callmsg) {
                ret = -1;
                goto err;
        }

        req->cred.flavour = rpc_call_cred_flavour (callmsg);
        req->cred.datalen = rpc_call_cred_len (callmsg);
        req->verf.flavour = rpc_call_verf_flavour (callmsg);
        req->verf.datalen = rpc_call_verf_len (callmsg);

        auth = rpcsvc_auth_get_handler (req);
        if (!auth) {
                ret = -1;
                goto err;
        }

        gf_log (GF_RPCSVC, GF_LOG_TRACE, "Auth handler: %s", auth->authname);

        if (auth->authops->request_init)
              ret = auth->authops->request_init (req, auth->authprivate);

        /* reset to auxgidlarge during
           unsersialize if necessary */
        req->auxgids = req->auxgidsmall;
        req->auxgidlarge = NULL;
err:
        return ret;
}


int
rpcsvc_authenticate (rpcsvc_request_t *req)
{
        int                     ret = RPCSVC_AUTH_REJECT;
        rpcsvc_auth_t           *auth = NULL;
        int                     minauth = 0;

        if (!req)
                return ret;

        /* FIXME use rpcsvc_request_prog_minauth() */
        minauth = 0;
        if (minauth > rpcsvc_request_cred_flavour (req)) {
                gf_log (GF_RPCSVC, GF_LOG_WARNING, "Auth too weak");
                rpcsvc_request_set_autherr (req, AUTH_TOOWEAK);
                goto err;
        }

        auth = rpcsvc_auth_get_handler (req);
        if (!auth) {
                gf_log (GF_RPCSVC, GF_LOG_WARNING, "No auth handler found");
                goto err;
        }

        if (auth->authops->authenticate)
                ret = auth->authops->authenticate (req, auth->authprivate);

err:
        return ret;
}

int
rpcsvc_auth_array (rpcsvc_t *svc, char *volname, int *autharr, int arrlen)
{
        int             count      = 0;
        int             result     = RPCSVC_AUTH_REJECT;
        char           *srchstr    = NULL;
        int             ret        = 0;

        struct rpcsvc_auth_list *auth = NULL;
        struct rpcsvc_auth_list *tmp = NULL;

        if ((!svc) || (!autharr) || (!volname))
                return -1;

        memset (autharr, 0, arrlen * sizeof(int));
        if (list_empty (&svc->authschemes)) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "No authentication!");
                goto err;
        }

        list_for_each_entry_safe (auth, tmp, &svc->authschemes, authlist) {
                if (count >= arrlen)
                        break;

                result = gf_asprintf (&srchstr, "rpc-auth.%s.%s",
                                      auth->name, volname);
                if (result == -1) {
                        count = -1;
                        goto err;
                }

                ret = dict_get_str_boolean (svc->options, srchstr, 0xC00FFEE);
                GF_FREE (srchstr);

                switch (ret) {
                case _gf_true:
                        result = RPCSVC_AUTH_ACCEPT;
                        autharr[count] = auth->auth->authnum;
                        ++count;
                        break;
                case _gf_false:
                        result = RPCSVC_AUTH_REJECT;
                        break;
                default:
                        result = RPCSVC_AUTH_DONTCARE;
                }
        }

err:
        return count;
}

gid_t *
rpcsvc_auth_unix_auxgids (rpcsvc_request_t *req, int *arrlen)
{
        if ((!req) || (!arrlen))
                return NULL;

        /* In case of AUTH_NULL auxgids are not used */
        switch (req->cred.flavour) {
        case AUTH_UNIX:
        case AUTH_GLUSTERFS:
        case AUTH_GLUSTERFS_v2:
                break;
        default:
                gf_log ("rpc", GF_LOG_DEBUG, "auth type not unix or glusterfs");
                return NULL;
        }

        *arrlen = req->auxgidcount;
        if (*arrlen == 0)
                return NULL;

        return &req->auxgids[0];
}
